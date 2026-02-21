module;

#include <vulkan/vulkan.h>
#include <cassert>

export module mo_yanxi.backend.vulkan.renderer;

import std;
import mo_yanxi.graphic_state_context;
export import mo_yanxi.graphic.draw.instruction.batch.frontend;
import mo_yanxi.graphic.draw.instruction.batch.backend.vulkan;
import mo_yanxi.vk;
import mo_yanxi.vk.cmd;
import mo_yanxi.vk.util;
export import mo_yanxi.gui.renderer.frontend;
export import mo_yanxi.gui.fx.config;
export import mo_yanxi.backend.vulkan.attachment_manager;
export import mo_yanxi.backend.vulkan.pipeline_manager;

namespace mo_yanxi::backend::vulkan{
template <typename T>
constexpr T mask(unsigned N) noexcept{
	return N >= std::numeric_limits<std::make_unsigned_t<T>>::digits ? ~T{0} : (static_cast<T>(1) << N) - 1;
}

// --- 内部初始化与工具函数 ---
graphic::draw::instruction::hardware_limit_config query_hardware_limits(vk::allocator_usage& usage){
	VkPhysicalDeviceMeshShaderPropertiesEXT mesh_props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT};
	VkPhysicalDeviceProperties2 prop{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &mesh_props};
	vkGetPhysicalDeviceProperties2(usage.get_physical_device(), &prop);
	return {
			mesh_props.maxTaskWorkGroupCount[0], mesh_props.maxTaskWorkGroupSize[0], mesh_props.maxMeshOutputVertices,
			mesh_props.maxMeshOutputPrimitives
		};
}

gui::fx::render_target_mask get_render_target(const gui::fx::pipeline_config& param,
	const graphic_pipeline_option& data) noexcept{
	if(param.draw_targets.any()) return param.draw_targets;
	return data.default_target_attachments;
}

/** 预定义数据布局表 */
const graphic::draw::data_layout_table table{
		std::in_place_type<gui::gui_reserved_user_data_tuple>
	};

const graphic::draw::data_layout_table table_non_vertex{
		std::in_place_type<std::tuple<gui::fx::ui_state, gui::fx::slide_line_config>>
	};

export struct renderer_create_info{
	vk::allocator_usage allocator_usage;
	VkCommandPool command_pool;
	VkSampler sampler;
	draw_attachment_create_info attachment_draw_config;
	blit_attachment_create_info attachment_blit_config;
	graphic_pipeline_create_config draw_pipe_config;
	compute_pipeline_create_config blit_pipe_config;
};

/** 内部辅助：跟踪 Attachment 的当前状态以进行自动屏障转换 */
struct attachment_state{
	VkPipelineStageFlags2 stage_mask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
	VkAccessFlags2 access_mask = VK_ACCESS_2_NONE;
	VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

export struct renderer{
	static constexpr std::size_t frames_in_flight = 3;

	/** 命令录制上下文：作为 renderer 的内部结构体，拥有访问外部私有成员的权限 */
	struct command_recording_context{
		// --- 缓存与持久化追踪状态 ---
		std::vector<std::uint8_t> attachment_enter_mark;
		graphic::draw::record_context<> descriptor_context;
		std::vector<attachment_state> draw_attachment_states;
		std::vector<attachment_state> blit_attachment_states;
		vk::cmd::dependency_gen barrier_gen;
		vk::dynamic_rendering rendering_config;

		enum class blit_sync_state : std::uint8_t{
			none,
			pending_barrier // 待决状态：刚被 Blit 修改，等待下一个操作决定同步策略
		};

		std::vector<blit_sync_state> blit_attachment_sync_states;

		std::vector<VkClearAttachment> cache_clear_attachments_;
		std::vector<VkClearRect> cache_clear_rects_;

		command_recording_context() = default;

		void resize(std::size_t draw_count, std::size_t blit_count){
			attachment_enter_mark.resize(draw_count);
			draw_attachment_states.resize(draw_count);
			blit_attachment_states.resize(blit_count);
			blit_attachment_sync_states.resize(blit_count, blit_sync_state::none);
		}

		void reset_barriers(){
			for(auto& s : draw_attachment_states){
				s = {
						VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
						VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
					};
			}
			for(auto& s : blit_attachment_states){
				s = {
						VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
						VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL
					};
			}
			std::ranges::fill(blit_attachment_sync_states, blit_sync_state::none);
		}

		static void ensure_attachment_state(vk::cmd::dependency_gen& dep, attachment_state& state, VkImage image,
			VkPipelineStageFlags2 next_stage, VkAccessFlags2 next_access, VkImageLayout next_layout){
			const bool layout_changed = state.layout != next_layout;

			// 优化：如果是连续的颜色附件写入且没有布局切换，可以利用栅格化顺序跳过屏障
			const bool can_skip_color = !layout_changed &&
				(state.stage_mask == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT && next_stage ==
					VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT) &&
				(state.access_mask == VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT && next_access ==
					VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
			if(can_skip_color) return;

			// 优化：如果是只读到只读的转换，且没有布局切换，也可以跳过
			auto is_write = [](VkAccessFlags2 a){
				return a & (VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
					VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT |
					VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
			};

			if(!layout_changed && !is_write(state.access_mask) && !is_write(next_access)) return;

			dep.push(image, state.stage_mask, state.access_mask, next_stage, next_access, state.layout, next_layout,
				vk::image::default_image_subrange);
			state = {next_stage, next_access, next_layout};
		}

		void commit_pending_blit_events(renderer& r, VkCommandBuffer cmd){
			vk::cmd::dependency_gen dep;
			for(std::size_t idx = 0; idx < blit_attachment_sync_states.size(); ++idx){
				if(blit_attachment_sync_states[idx] == blit_sync_state::pending_barrier){
					dep.push(r.attachment_manager_.get_blit_attachments()[idx].get_image(),
						VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
						VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
						VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, vk::image::default_image_subrange);

					// 状态跃迁：屏障已提前提交，Graphic 管线运行期间 GPU 将自然消化该同步依赖
					blit_attachment_sync_states[idx] = blit_sync_state::none;
				}
			}
			dep.apply(cmd);
		}

		/** 核心录制逻辑：将外部状态转换为局部临时变量 */
		void record(renderer& r, VkCommandBuffer cmd){
			vkCmdExecuteCommands(cmd, 1, r.blit_attachment_clear_and_init_command_buffer.as_data());
			const auto section_count = r.batch_host.get_submit_sections_count();
			if(r.batch_host.get_valid_submit_groups().empty()) return;

			// --- 录制命令期的临时上下文全部转换为局部变量 ---
			graphics_context_trace graphic_context{};
			graphic_context.invalidate();
			gui::fx::pipeline_config draw_cfg{.pipeline_index = 0};
			bool is_rendering = false;
			gui::fx::render_target_mask current_pass_mask{};
			bool current_pass_msaa = false;

			// 状态重置
			reset_barriers();
			std::ranges::fill(attachment_enter_mark, 0);
			rendering_config.clear_color_attachments();

			auto flush_pass = [&](){
				if(is_rendering){
					vkCmdEndRendering(cmd);
					is_rendering = false;
					graphic_context.set_rebind_required();
				}
			};

			const bool global_msaa = r.attachment_manager_.enables_multisample();

			for(unsigned i = 0; i < section_count; ++i){
				const auto& pipe_opt = r.draw_pipeline_manager_.get_pipelines()[draw_cfg.pipeline_index].option;
				const bool is_msaa = pipe_opt.enables_multisample && global_msaa;

				const auto target_mask = get_render_target(draw_cfg, pipe_opt);

				// 1. 自动同步检查：如果目标 Attachment 状态改变，则生成屏障
				barrier_gen.clear();
				target_mask.for_each_popbit([&](unsigned idx){
					ensure_attachment_state(barrier_gen, draw_attachment_states[idx],
						r.attachment_manager_.get_draw_attachments()[idx].get_image(),
						VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
						VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
				});

				// 2. 渲染通道切换判断：如果屏障生效、目标变更或 MSAA 切换，则必须重启 Rendering
				if(is_rendering && (!barrier_gen.empty() || target_mask != current_pass_mask || is_msaa !=
					current_pass_msaa)){
					flush_pass();
				}

				if(!barrier_gen.empty()){
					barrier_gen.apply(cmd);
				}

				// 3. 开始新的 Rendering Pass
				if(!is_rendering){
					// 【优化核心】：在启动图形管线前，将所有积累的待决 Blit 转换为屏障提前发射
					commit_pending_blit_events(r, cmd);

					r.attachment_manager_.configure_dynamic_rendering<32>(
						rendering_config,
						draw_cfg.draw_targets.none() ? pipe_opt.default_target_attachments : draw_cfg.draw_targets,
						is_msaa
					);

					std::size_t slot = 0;

					target_mask.for_each_popbit([&](unsigned idx){
						auto& info = rendering_config.get_color_attachment_infos()[slot++];
						info.loadOp = attachment_enter_mark[idx]
							              ? VK_ATTACHMENT_LOAD_OP_LOAD
							              : VK_ATTACHMENT_LOAD_OP_CLEAR;
						attachment_enter_mark[idx] = true;
					});

					rendering_config.begin_rendering(cmd, r.attachment_manager_.get_screen_area());
					is_rendering = true;
					current_pass_mask = target_mask;
					current_pass_msaa = is_msaa;
				}

				bool requires_clear = false;

				if(r.batch_device.is_section_empty(i)){
					for(const auto& entry : r.batch_host.get_break_config_at(i).get_entries()){
						requires_clear |= process_breakpoints_(r, entry, graphic_context, draw_cfg, cmd, flush_pass);
					}
				} else{
					graphic_context.apply(cmd, r.draw_pipeline_manager_);

					// 5. 执行 Draw Call（仅包含描述符绑定和绘制）
					cmd_draw_(r, cmd, i, draw_cfg);

					// 6. 处理断点（如管线变更或触发 Blit）
					for(const auto& entry : r.batch_host.get_break_config_at(i).get_entries()){
						requires_clear |= process_breakpoints_(r, entry, graphic_context, draw_cfg, cmd, flush_pass);
					}
				}

				if(requires_clear){
					flush_pass();
					std::ranges::fill(attachment_enter_mark, 0);
				}
			}

			flush_pass();
		}

		void cmd_draw_(renderer& r, VkCommandBuffer cmd, std::uint32_t index, const gui::fx::pipeline_config& arg){
			// 获取 Pipeline Layout 仅用于描述符绑定
			const auto& pc = r.draw_pipeline_manager_.get_pipelines()[arg.pipeline_index];

			descriptor_context.clear();
			r.batch_device.load_descriptors(descriptor_context, r.current_frame_index_);
			descriptor_context.prepare_bindings();
			descriptor_context(pc.pipeline_layout, cmd, index, VK_PIPELINE_BIND_POINT_GRAPHICS);
			r.batch_device.cmd_draw(cmd, index, r.current_frame_index_);
		}

		/** 处理 Blit 操作：Compute Shader 实现的图像处理 */
		void blit_(renderer& r, gui::fx::blit_config cfg, VkCommandBuffer cmd){
			cfg.get_clamped_to_positive();
			const auto& inout = r.get_blit_inout_config(cfg);

			barrier_gen.clear();

			// 1. 处理来自 Draw Attachment (Graphic Pipeline) 的输入
			for(const auto& e : inout.get_input_entries()){
				ensure_attachment_state(barrier_gen, draw_attachment_states[e.resource_index],
					r.attachment_manager_.get_draw_attachments()[e.resource_index].get_image(),
					VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
					VK_IMAGE_LAYOUT_GENERAL);
			}

			// 2. 消费阶段：为 Compute 独占的 Blit Attachment 处理依赖
			for(const auto& e : inout.get_output_entries()){
				const auto idx = e.resource_index;
				if(blit_attachment_sync_states[idx] == blit_sync_state::pending_barrier){
					// 场景 B：两个 Blit 背靠背执行，直接应用 Pipeline Barrier
					barrier_gen.push(r.attachment_manager_.get_blit_attachments()[idx].get_image(),
						VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
						VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
						VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, vk::image::default_image_subrange);
					blit_attachment_sync_states[idx] = blit_sync_state::none;
				}
			}

			// 应用积累的常规屏障（这会一并提交输入附件的屏障和场景 B 产生的屏障）
			if(!barrier_gen.empty()){
				barrier_gen.apply(cmd);
			}

			// 3. 绑定管线、描述符并执行分发
			auto& pc = r.blit_pipeline_manager_.get_pipelines()[cfg.pipe_info.pipeline_index];
			pc.pipeline.bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);

			math::upoint2 offset = cfg.blit_region.src.as<unsigned>();
			vkCmdPushConstants(cmd, pc.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(offset), &offset);

			const VkDescriptorBufferBindingInfoEXT db_info =
				cfg.use_default_inouts()
					? r.blit_default_inout_descriptors_[cfg.pipe_info.pipeline_index]
					: r.blit_specified_inout_descriptors_[cfg.pipe_info.inout_define_index];

			descriptor_context.clear();
			descriptor_context.push(0, db_info);

			r.blit_pipeline_manager_.append_descriptor_buffers(descriptor_context, cfg.pipe_info.pipeline_index);
			descriptor_context.prepare_bindings();
			descriptor_context(pc.pipeline_layout, cmd, 0, VK_PIPELINE_BIND_POINT_COMPUTE);

			auto dispatches = cfg.get_dispatch_groups();
			vkCmdDispatch(cmd, dispatches.x, dispatches.y, 1);

			// 4. 生产阶段：为刚修改过的 Blit Attachment 埋入“待决状态”
			for(const auto& e : inout.get_output_entries()){
				const auto idx = e.resource_index;
				blit_attachment_sync_states[idx] = blit_sync_state::pending_barrier;
			}
		}

		template <typename F>
		bool process_breakpoints_(
			renderer& r,
			const graphic::draw::instruction::state_transition_config::exported_entry& entry,
			graphics_context_trace& context_trace,
			gui::fx::pipeline_config& draw_cfg,
			VkCommandBuffer buffer,
			F&& flush_callback){
			auto& cur_pipe = r.draw_pipeline_manager_.get_pipelines()[draw_cfg.pipeline_index];
			using namespace gui::fx;
			switch(static_cast<state_type>(entry.tag.major)){
			case state_type::blit :{
				flush_callback();

				// Blit 必须在渲染通道外
				blit_(r, entry.as<blit_config>(), buffer);
				//TODO separate clean request and blit?
				return true;
			}
			case state_type::pipe :{
				auto param = entry.as<pipeline_config>();

				param.pipeline_index = draw_cfg.pipeline_index;
				draw_cfg = param;
				context_trace.update_pipeline(draw_cfg.pipeline_index);
				return false;
			}
			case state_type::push_constant :{
				const auto flags = static_cast<VkShaderStageFlags>(entry.tag.minor);

				vkCmdPushConstants(buffer, cur_pipe.pipeline_layout, flags, entry.logical_offset, entry.payload.size(),
					entry.payload.data());
				break;
			}
			case state_type::set_color_blend_enable :{
				auto param = entry.as<blend_enable_flag>();
				auto mask = make_render_target_mask(cur_pipe.option, draw_cfg, entry.tag.minor);

				mask.for_each_popbit([&](unsigned i){
					context_trace.set_blend_enable(i, param);
				});
				break;
			}
			case state_type::set_color_blend_equation :{
				auto param = entry.as<blend_equation>();
				auto mask = make_render_target_mask(cur_pipe.option, draw_cfg, entry.tag.minor);
				mask.for_each_popbit([&](unsigned i){
					context_trace.set_blend_equation(i, param);
				});
				break;

			}
			case state_type::set_color_write_mask :{
				auto param = entry.as<blend_write_mask_type>();
				auto mask = make_render_target_mask(cur_pipe.option, draw_cfg, entry.tag.minor);
				mask.for_each_popbit([&](unsigned i){
					context_trace.set_blend_write_mask(entry.tag.minor, param);
				});
				break;

			}
			case state_type::set_scissor :{
				auto param = entry.as<scissor>();
				context_trace.set_scissor(param);
				break;
			}
			case state_type::set_viewport :{
				auto param = entry.as<viewport>();
				context_trace.set_viewport(param);
				break;

			}
			case state_type::fill_color :{
				auto mask = make_render_target_mask(cur_pipe.option, draw_cfg, entry.tag.minor);
				cache_clear_attachments_.clear();
				cache_clear_rects_.clear();

				if(entry.payload.size() == sizeof(color_clear_value)){
					//only color clear value is set, make full screen clear.
					auto param = entry.as<color_clear_value>();

					// vkCmdClearAttachments()
					mask.for_each_popbit([&](unsigned i){
						cache_clear_attachments_.push_back({
								.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
								.colorAttachment = i,
								.clearValue = param
							});
					});
					VkClearRect rect{
							.rect = {
								{},
								r.attachment_manager_.get_extent()
							},
							.baseArrayLayer = 0,
							.layerCount = 1
						};

					vkCmdClearAttachments(buffer, cache_clear_attachments_.size(), cache_clear_attachments_.data(), 1,
						&rect);
				} else{
					throw std::runtime_error{"not impl"};
					//TODO
				}

				break;
			}
			default : break;
			}
			return false;

		}

	private:
		gui::fx::render_target_mask make_render_target_mask(graphic_pipeline_option& current_pipe_option,
			const gui::fx::pipeline_config& config, unsigned bits) const noexcept{
			return (bits ? gui::fx::render_target_mask{bits} : gui::fx::render_target_mask{~0U})
				& gui::fx::render_target_mask{
					mask<std::uint32_t>(get_render_target(config, current_pipe_option).popcount())
				};
		}
	};

	struct frame_data{
		vk::fence fence{};

		vk::command_buffer main_command_buffer{};
		frame_data() = default;

		frame_data(VkDevice device, VkCommandPool pool)
			: fence(device, true)
			, main_command_buffer(device, pool){
		}
	};
private:
	vk::allocator_usage allocator_usage_{};

public:
	/** 绘图指令批处理器 */
	graphic::draw::instruction::draw_list_context batch_host{};

	graphic::draw::instruction::batch_vulkan_executor batch_device{};

private:
	attachment_manager attachment_manager_{};
	graphic_pipeline_manager draw_pipeline_manager_{};
	compute_pipeline_manager blit_pipeline_manager_{};

	vk::sampler_descriptor_heap sampler_descriptor_heap_{};
	vk::resource_descriptor_heap resource_descriptor_heap_{};

	std::vector<vk::descriptor_buffer> blit_default_inout_descriptors_{};
	std::vector<vk::descriptor_buffer> blit_specified_inout_descriptors_{};

	std::array<frame_data, frames_in_flight> frames_{};
	std::uint32_t current_frame_index_{};

	vk::command_buffer blit_attachment_clear_and_init_command_buffer{};

	command_recording_context record_ctx_{};
	VkSampler sampler_{};

public:
	[[nodiscard]] explicit(false) renderer(
		renderer_create_info&& create_info
	)
		: allocator_usage_(create_info.allocator_usage)
		, batch_host([&]{
			VkPhysicalDeviceMeshShaderPropertiesEXT meshProperties{
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT
				};
			VkPhysicalDeviceProperties2 prop{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &meshProperties};
			vkGetPhysicalDeviceProperties2(allocator_usage_.get_physical_device(), &prop);
			return graphic::draw::instruction::hardware_limit_config{
					.max_group_count = meshProperties.maxTaskWorkGroupCount[0],
					.max_group_size = meshProperties.maxTaskWorkGroupSize[0],
					.max_vertices_per_group = meshProperties.maxMeshOutputVertices,
					.max_primitives_per_group = meshProperties.maxMeshOutputPrimitives
				};
		}(), table, table_non_vertex)
		, batch_device(allocator_usage_, batch_host, frames_in_flight)
		, attachment_manager_{
			allocator_usage_, std::move(create_info.attachment_draw_config),
			std::move(create_info.attachment_blit_config)
		}
		, draw_pipeline_manager_(allocator_usage_, create_info.draw_pipe_config,
			batch_device.get_descriptor_set_layout(), attachment_manager_.get_draw_config())
		, sampler_(create_info.sampler){
		record_ctx_.resize(attachment_manager_.get_draw_attachments().size(),
			attachment_manager_.get_blit_attachments().size());

		initialize_frames(create_info.command_pool);
		initialize_blit_resources(create_info);

		sampler_descriptor_heap_ = {allocator_usage_, {
			vk::preset::ui_texture_sampler,
			vk::preset::default_blit_sampler,
			vk::preset::default_texture_sampler,
		}, false};

		resource_descriptor_heap_ = {
				allocator_usage_, {
					//image descriptors
					//TODO support input attachments?

					vk::heap_section{
						attachment_manager_.get_blit_attachment_count() +
							attachment_manager_.get_draw_attachment_count(),
						vk::heap_section_type::image
					},
					//batch buffers
					vk::heap_section{
						batch_device.get_required_buffer_descriptor_count(batch_host),
						vk::heap_section_type::buffer
					},
					//dynamic batch used images
					vk::heap_section{
						128,
						vk::heap_section_type::image
					}
				}
			};
	}

	/** 窗口缩放时重置附件与描述符绑定 */
	void resize(VkExtent2D extent){
		attachment_manager_.resize(extent);
		// 更新状态追踪数组大小
		record_ctx_.resize(attachment_manager_.get_draw_attachments().size(),
			attachment_manager_.get_blit_attachments().size());

		auto update_descriptor = [this](vk::descriptor_buffer& db, const compute_pipeline_blit_inout_config& cfg){
			vk::descriptor_mapper mapper{db};
			for(const auto& in : cfg.get_input_entries()){
				(void)mapper.set_image(in.binding, {
						nullptr, attachment_manager_.get_draw_attachments()[in.resource_index].get_image_view(),
						VK_IMAGE_LAYOUT_GENERAL
					}, 0, in.type);

			}
			for(const auto& out : cfg.get_output_entries()){
				(void)mapper.set_image(out.binding, {
						nullptr, attachment_manager_.get_blit_attachments()[out.resource_index].get_image_view(),
						VK_IMAGE_LAYOUT_GENERAL
					}, 0, out.type);
			}
		};

		// 重新绑定所有 Blit 相关的描述符
		for(auto&& [db, pipe] : std::views::zip(blit_default_inout_descriptors_,
			    blit_pipeline_manager_.get_pipelines())){
			update_descriptor(db, pipe.option.inout);

		}
		for(auto&& [db, inout] : std::views::zip(blit_specified_inout_descriptors_,
			    blit_pipeline_manager_.get_inout_defines())){
			update_descriptor(db, inout);
		}

		create_blit_clear_and_init_cmd();

	}

	/** 上传渲染数据并切换帧上下文 */
	void upload(){
		current_frame_index_ = (current_frame_index_ + 1) % frames_in_flight;
		try{
			frames_[current_frame_index_].fence.wait_and_reset();
			batch_device.upload(batch_host, sampler_, current_frame_index_);
		} catch(...){
			frames_[current_frame_index_].fence.reset();

		}
	}

	/** 录制当前帧的主指令缓冲 */
	void create_command(){
		vk::scoped_recorder recorder{
				frames_[current_frame_index_].main_command_buffer, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
			};
		record_ctx_.record(*this, recorder);
	}

	gui::renderer_frontend create_frontend() noexcept{
		return gui::renderer_frontend{
				table, table_non_vertex, {
					*this,
					[](renderer& r, auto h, const std::byte* d) static{ return r.batch_host.push_instr(h, d);

					},
					[](renderer& r, auto cfg, auto f, auto ecfg, auto p) static{
						r.batch_host.push_state(cfg, f, ecfg, p);

					},
					[](renderer& r, auto head, std::span<const std::byte> payload, unsigned target_offset) static{

					}
				}
			};
	}

	VkCommandBuffer get_valid_cmd_buf() const noexcept{ return frames_[current_frame_index_].main_command_buffer;

	}
	VkFence get_fence() const noexcept{ return frames_[current_frame_index_].fence; }

	vk::image_handle get_base() const noexcept{
		return attachment_manager_.get_blit_attachments()[0];
	}

	vk::image_handle get_draw_base() const noexcept{
		return attachment_manager_.get_blit_attachments()[0];

	}

private:
	void initialize_frames(VkCommandPool pool){
		for(auto& f : frames_) f = frame_data(allocator_usage_.get_device(), pool);
		blit_attachment_clear_and_init_command_buffer = vk::command_buffer{
				allocator_usage_.get_device(), pool, VK_COMMAND_BUFFER_LEVEL_SECONDARY
			};

	}

	void initialize_blit_resources(const renderer_create_info& info){
		blit_pipeline_manager_ = compute_pipeline_manager(allocator_usage_, info.blit_pipe_config);
		// 分配并初始化默认/特定 Blit 描述符缓冲
		auto pipes = blit_pipeline_manager_.get_pipelines();
		blit_default_inout_descriptors_.reserve(pipes.size());

		for(std::size_t i = 0; i < pipes.size(); ++i){
			auto& layout = blit_pipeline_manager_.get_inout_layouts()[i];
			blit_default_inout_descriptors_.emplace_back(allocator_usage_, layout, layout.binding_count());
		}

		auto inouts = blit_pipeline_manager_.get_inout_defines();
		blit_specified_inout_descriptors_.reserve(inouts.size());

		for(const auto& def : inouts){
			vk::descriptor_layout layout{
					allocator_usage_.get_device(), VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
					def.make_layout_builder()
				};
			blit_specified_inout_descriptors_.emplace_back(allocator_usage_, layout, layout.binding_count());
		}
	}

	void create_blit_clear_and_init_cmd() const{
		vk::scoped_recorder recorder{
				blit_attachment_clear_and_init_command_buffer, VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT, true
			};
		vk::cmd::dependency_gen dep;

		// 初始化所有附件到初始布局并清空 Blit 目标
		for(const auto& img : attachment_manager_.get_blit_attachments()){
			dep.push(img.get_image(), VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
				VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, vk::image::default_image_subrange);
		}
		dep.apply(recorder);

		VkClearColorValue black{};

		for(const auto& img : attachment_manager_.get_blit_attachments()){
			vkCmdClearColorImage(recorder, img.get_image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1,
				&vk::image::default_image_subrange);
			dep.push(img.get_image(), VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, vk::image::default_image_subrange);

		}

		// 初始化绘制附件
		for(const auto& img : attachment_manager_.get_draw_attachments()){
			dep.push(img.get_image(), VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
				VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, vk::image::default_image_subrange);

		}
		for(const auto& img : attachment_manager_.get_multisample_attachments()){
			dep.push(img.get_image(), VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
				VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, vk::image::default_image_subrange);
		}
		dep.apply(recorder);

	}

	const compute_pipeline_blit_inout_config& get_blit_inout_config(const gui::fx::blit_config& cfg){
		return cfg.use_default_inouts()
			       ?

			       blit_pipeline_manager_.get_pipelines()[cfg.pipe_info.pipeline_index].option.inout
			       : blit_pipeline_manager_.get_inout_defines()[cfg.pipe_info.inout_define_index];
	}
};
} // namespace mo_yanxi::backend::vulkan
