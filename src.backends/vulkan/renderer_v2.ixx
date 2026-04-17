module;

#include <vulkan/vulkan.h>
#include <cassert>

export module mo_yanxi.backend.vulkan.renderer;

import std;
import mo_yanxi.graphic_state_context;
export import mo_yanxi.graphic.draw.instruction.batch.frontend;
import mo_yanxi.graphic.draw.instruction.batch.backend.vulkan;

import :barrier_automatic;

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

	VkPipelineShaderStageCreateInfo resolver_shader_stage;
	graphic::draw::instruction::gpu_stride_config stride_config;
};

export struct renderer{
	//TODO change it to other value is not supported currently: attachments TODO
	static constexpr std::size_t frames_in_flight = 1;

	/** 命令录制上下文：作为 renderer 的内部结构体，拥有访问外部私有成员的权限 */
	struct command_recording_context{
		struct per_record_context_value{
			unsigned mask_depth{};
			unsigned current_pass_mask_depth{};
			gui::fx::render_target_mask current_pass_mask{};
			mask_usage current_pass_mask_usage{};
			bool is_rendering{};
			bool current_pass_msaa{};
		};

		struct breakpoint_process_params{
			const graphic::draw::instruction::state_transition_config::exported_entry& entry;
			graphics_context_trace& context_trace;
			gui::fx::pipeline_config& draw_cfg;
			per_record_context_value& ctx_val;

			void flush(command_recording_context& ctx, VkCommandBuffer buf) const{
				ctx.flush_pass_(buf, ctx_val);
			}
		};

	private:
		graphic::draw::record_context<> cache_descriptor_context_{};
		vk::cmd::dependency_gen cache_barrier_gen_{};

		vk::dynamic_rendering cache_rendering_config_{};

		std::vector<std::uint8_t> cache_attachment_enter_mark_{};
		std::vector<std::uint8_t> cache_mask_layer_enter_mark_{};

		graphics_context_trace cache_graphic_context_{};

		//TODO merge clear requests
		std::vector<VkClearAttachment> cache_clear_attachments_{};
		std::vector<VkClearRect> cache_clear_rects_{};
		attachment_sync_manager cache_sync_mgr_{};


		void flush_pass_(VkCommandBuffer cmd, per_record_context_value& ctx_val) {
			if(ctx_val.is_rendering){
				vkCmdEndRendering(cmd);
				ctx_val.is_rendering = false;
				cache_graphic_context_.set_rebind_required();
			}
		}

		void ensure_render_pass_(renderer& r, VkCommandBuffer cmd, const gui::fx::pipeline_config& draw_cfg, per_record_context_value& ctx_val);

	public:
		command_recording_context() = default;

		void resize(std::size_t draw_count, std::size_t blit_count){
			cache_attachment_enter_mark_.resize(draw_count);
			cache_sync_mgr_.resize(draw_count, blit_count);
			cache_mask_layer_enter_mark_.assign(1, {});
		}

		void record(renderer& r, VkCommandBuffer cmd);

		void cmd_draw_(renderer& r, VkCommandBuffer cmd, std::uint32_t index, const gui::fx::pipeline_config& arg, per_record_context_value& ctx_val);

		void blit_(renderer& r, gui::fx::blit_config cfg, VkCommandBuffer cmd);

		bool process_breakpoints_(
			renderer& r,
			breakpoint_process_params params,
			VkCommandBuffer buffer);
	private:
		static gui::fx::render_target_mask make_render_target_mask(
			const graphic_pipeline_option& current_pipe_option, const gui::fx::pipeline_config& config,
			unsigned bits) noexcept{
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

	vk::descriptor_buffer mask_descriptor_buffer_{};

	// vk::sampler_descriptor_heap sampler_descriptor_heap_{};

	vk::pipeline_layout resolver_pipeline_layout_{};
	vk::pipeline resolver_pipeline_{};

public:
	// vk::resource_descriptor_heap resource_descriptor_heap{};

	static constexpr std::uint32_t get_heap_dynamic_image_section() noexcept{
		return 1;
	}

private:
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
			  return graphic::draw::instruction::hardware_limit_config{};
		  }(), table, table_non_vertex)
		  , batch_device(allocator_usage_, batch_host, create_info.stride_config, frames_in_flight)
		  , attachment_manager_{
			  allocator_usage_, std::move(create_info.attachment_draw_config),
			  std::move(create_info.attachment_blit_config)
		  }
		  , draw_pipeline_manager_(allocator_usage_, std::move(create_info.draw_pipe_config),
		                           batch_device.get_gfx_descriptor_set_layout(), attachment_manager_.get_draw_config())
		  , resolver_pipeline_layout_(allocator_usage_.get_device(), 0, {batch_device.get_cs_descriptor_set_layout()})
		  , resolver_pipeline_(
			  allocator_usage_.get_device(),
			  resolver_pipeline_layout_,
			  VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
			  create_info.resolver_shader_stage
		  ),
		sampler_(create_info.sampler){

		record_ctx_.resize(attachment_manager_.get_draw_attachments().size(),
		                   attachment_manager_.get_blit_attachments().size());

		initialize_frames(create_info.command_pool);
		initialize_blit_resources(create_info);

		// sampler_descriptor_heap_ = {
		// 		allocator_usage_, {
		// 			vk::preset::ui_texture_sampler,
		// 			vk::preset::default_blit_sampler,
		// 			vk::preset::default_texture_sampler,
		// 		},
		// 		false
		// 	};

		std::array<vk::heap_section, 5> sections{};

		sections[0] = {
				attachment_manager_.get_blit_attachment_count() +
				attachment_manager_.get_draw_attachment_count(),
				vk::heap_section_type::image
			};

		sections[get_heap_dynamic_image_section()] = {
				128,
				vk::heap_section_type::image
			};

		for(unsigned i = 0; i < frames_in_flight; ++i){
			sections[get_heap_dynamic_image_section() + 1 + i] = {
					graphic::draw::instruction::get_required_buffer_descriptor_count_per_frame(batch_host),
					vk::heap_section_type::buffer
				};
		}


		// resource_descriptor_heap = {allocator_usage_, sections};
	}

	/** 窗口缩放时重置附件与描述符绑定 */
	void resize(VkExtent2D extent);

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
					[](renderer& r, graphic::draw::instruction::instruction_head h, const std::byte* d) static{
						return r.batch_host.push_instr(h, d);
					},
					[](renderer& r, std::span<const graphic::draw::instruction::instruction_head> h,
					   const std::byte* d) static{
						return r.batch_host.push_instr_batch(h, d);
					},
					[](renderer& r, auto cfg, auto f, auto ecfg, auto p) static{
						r.batch_host.push_state(cfg, f, ecfg, p);
					}
				}
			};
	}

	VkCommandBuffer get_valid_cmd_buf() const noexcept{
		return frames_[current_frame_index_].main_command_buffer;
	}

	VkFence get_fence() const noexcept{ return frames_[current_frame_index_].fence; }

	vk::image_handle get_base() const noexcept{
		return attachment_manager_.get_blit_attachments()[0];
	}

	auto get_blit_attachments() const noexcept{
		return attachment_manager_.get_blit_attachments();
	}

	vk::image_handle get_draw_base() const noexcept{
		return attachment_manager_.get_blit_attachments()[0];
	}

private:
	void update_mask_depth_(unsigned mask_depth){
		if(!attachment_manager_.update_mask_depth(mask_depth))return;
		mask_descriptor_buffer_ = vk::descriptor_buffer{
			allocator_usage_,
			draw_pipeline_manager_.get_mask_descriptor_set_layout(),
			draw_pipeline_manager_.get_mask_descriptor_set_layout().binding_count(),
			mask_depth};

		vk::descriptor_mapper mapper{mask_descriptor_buffer_};
		for(unsigned i = 0; i < mask_depth; ++i){
			mapper.set_image(0,
				attachment_manager_.get_mask_image_views()[i], i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, nullptr, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
		}
	}

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
					 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
					 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
					 // 【核心修复7】初始化阶段直接转换为 GENERAL
					 VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
					 vk::image::default_image_subrange);
		}
		for(const auto& img : attachment_manager_.get_multisample_attachments()){
			dep.push(img.get_image(), VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
					 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
					 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
					 // 【核心修复8】初始化阶段直接转换为 GENERAL
					 VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
					 vk::image::default_image_subrange);
		}
		dep.apply(recorder);
	}

	const compute_pipeline_blit_inout_config& get_blit_inout_config(const gui::fx::blit_config& cfg){
		return cfg.use_default_inouts()
			       ? blit_pipeline_manager_.get_pipelines()[cfg.pipe_info.pipeline_index].option.inout
			       : blit_pipeline_manager_.get_inout_defines()[cfg.pipe_info.inout_define_index];
	}
};
} // namespace mo_yanxi::backend::vulkan
