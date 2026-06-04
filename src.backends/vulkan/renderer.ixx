module;

#include <vulkan/vulkan.h>
#include <cassert>

export module mo_yanxi.backend.vulkan.renderer;

import std;
import mo_yanxi.graphic_state_context;
export import mo_yanxi.graphic.image_view_registry;
export import mo_yanxi.graphic.g2d.batch.frontend;
import mo_yanxi.graphic.g2d.batch.backend.vulkan;

import mo_yanxi.vk.sync_processor;

import mo_yanxi.vk;
import mo_yanxi.vk.cmd;
import mo_yanxi.vk.util;
export import mo_yanxi.gui.renderer.frontend;
export import mo_yanxi.gui.fx.config;
export import mo_yanxi.backend.vulkan.attachment_manager;
export import mo_yanxi.backend.vulkan.pipeline_manager;
export import mo_yanxi.backend.vulkan.renderer.components;

namespace mo_yanxi::backend::vulkan{

template <typename T>
constexpr T mask(unsigned N) noexcept{
	return N >= std::numeric_limits<std::make_unsigned_t<T>>::digits ? ~T{0} : (static_cast<T>(1) << N) - 1;
}

// --- 内部初始化与工具函数 ---
graphic::g2d::hardware_limit_config query_hardware_limits(vk::allocator_usage& usage){
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
const graphic::g2d::data_layout_table table{
		std::in_place_type<gui::gui_reserved_user_data_tuple>
	};

const graphic::g2d::data_layout_table table_non_vertex{
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
	graphic::g2d::gpu_stride_config stride_config;
};

export struct renderer{
	//TODO change it to other value is not supported currently: attachments TODO
	static constexpr std::size_t frames_in_flight = 3;

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

		struct section_state_apply_params{
			const graphic::g2d::section_state_delta_set::exported_entry& entry;
			graphics_context_trace& context_trace;
			gui::fx::pipeline_config& draw_cfg;
			per_record_context_value& ctx_val;

			void flush(command_recording_context& ctx, VkCommandBuffer buf) const{
				ctx.flush_pass_(buf, ctx_val);
			}
		};

	private:
		graphic::g2d::record_context<> cache_descriptor_context_{};
		vk::sync::sync_barrier_batch cache_barrier_gen_{};

		vk::dynamic_rendering cache_rendering_config_{};

		std::vector<std::uint8_t> cache_attachment_enter_mark_{};
		std::vector<std::uint8_t> cache_mask_layer_enter_mark_{};

		graphics_context_trace cache_graphic_context_{};

		static constexpr unsigned kMaxShaderStages = 6;
		std::array<std::vector<std::byte>, kMaxShaderStages> cache_push_constants_{};

		static constexpr unsigned stage_index(VkShaderStageFlags flags) noexcept{
			unsigned idx = 0;
			while(flags >>= 1) ++idx;
			return idx < kMaxShaderStages ? idx : 0;
		}

		//TODO merge clear requests
		std::vector<VkClearAttachment> cache_clear_attachments_{};
		std::vector<VkClearRect> cache_clear_rects_{};
		vk::sync::sync_processor cache_sync_mgr_{};
		std::vector<vk::sync::image_slot> draw_attachment_slots_{};
		std::vector<vk::sync::image_slot> blit_attachment_slots_{};
		std::vector<vk::sync::image_slot> mask_attachment_slots_{};


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
		void rebuild_sync_resources_(renderer& r);

		void resize(std::size_t draw_count, std::size_t blit_count){
			cache_attachment_enter_mark_.resize(draw_count);
			cache_mask_layer_enter_mark_.assign(1, {});
			draw_attachment_slots_.resize(draw_count);
			blit_attachment_slots_.resize(blit_count);
		}

		void record(renderer& r, VkCommandBuffer cmd);

		void cmd_draw_(renderer& r, VkCommandBuffer cmd, std::uint32_t index, const gui::fx::pipeline_config& arg, per_record_context_value& ctx_val);

		void blit_(renderer& r, gui::fx::blit_config cfg, VkCommandBuffer cmd);

		bool apply_section_state_(
			renderer& r,
			const section_state_apply_params& params,
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

private:
	vk::allocator_usage allocator_usage_{};
	std::unique_ptr<graphic::image_view_registry> image_view_registry_{std::make_unique<graphic::image_view_registry>()};
	graphic::sampler_descriptor_index default_sampler_index_{graphic::auto_sampler_index};

public:
	/** 绘图指令批处理器 */
	graphic::g2d::draw_list_context batch_host{};

	graphic::g2d::batch_vulkan_executor batch_device{};

private:
	attachment_manager attachment_manager_{};
	graphic_pipeline_manager draw_pipeline_manager_{};
	renderer_blit_resources blit_resources_{};

	vk::descriptor_buffer mask_descriptor_buffer_{};

	// vk::sampler_descriptor_heap sampler_descriptor_heap_{};

	renderer_instruction_resolver instruction_resolver_{};

public:
	// vk::resource_descriptor_heap resource_descriptor_heap{};

	static constexpr std::uint32_t get_heap_dynamic_image_section() noexcept{
		return 1;
	}

	[[nodiscard]] graphic::image_view_registry& get_image_view_registry() noexcept{
		return *image_view_registry_;
	}

	[[nodiscard]] const graphic::image_view_registry& get_image_view_registry() const noexcept{
		return *image_view_registry_;
	}

	[[nodiscard]] graphic::sampler_descriptor_index get_default_sampler_index() const noexcept{
		return default_sampler_index_;
	}

private:
	renderer_frame_ring<frames_in_flight> frames_{};

	vk::command_buffer blit_attachment_clear_and_init_command_buffer{};
	bool mask_attachment_states_invalidated_{};

	command_recording_context record_ctx_{};

public:
	[[nodiscard]] explicit(false) renderer(
		renderer_create_info&& create_info
	)
		: allocator_usage_(create_info.allocator_usage)
		  , default_sampler_index_{image_view_registry_->register_sampler(create_info.sampler)}
		  , batch_host([&]{
			  VkPhysicalDeviceMeshShaderPropertiesEXT meshProperties{
					  VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT
				  };
			  VkPhysicalDeviceProperties2 prop{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &meshProperties};
			  vkGetPhysicalDeviceProperties2(allocator_usage_.get_physical_device(), &prop);
			  return graphic::g2d::hardware_limit_config{};
		  }(), table, table_non_vertex, *image_view_registry_)
		  , batch_device(allocator_usage_, batch_host, create_info.stride_config, frames_in_flight)
		  , attachment_manager_{
			  allocator_usage_, std::move(create_info.attachment_draw_config),
			  std::move(create_info.attachment_blit_config)
		  }
		  , draw_pipeline_manager_(allocator_usage_, std::move(create_info.draw_pipe_config),
		                           batch_device.get_gfx_descriptor_set_layout(), attachment_manager_.get_draw_config())
		  , blit_resources_(allocator_usage_, create_info.blit_pipe_config)
		  , instruction_resolver_(
			  allocator_usage_.get_device(),
			  batch_device.get_cs_descriptor_set_layout(),
			  create_info.resolver_shader_stage
		  ){

		record_ctx_.resize(attachment_manager_.get_draw_attachments().size(),
		                   attachment_manager_.get_blit_attachments().size());
		record_ctx_.rebuild_sync_resources_(*this);

		initialize_frames(create_info.command_pool);

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
					graphic::g2d::get_required_buffer_descriptor_count_per_frame(batch_host),
					vk::heap_section_type::buffer
				};
		}


		// resource_descriptor_heap = {allocator_usage_, sections};
	}

	/** 窗口缩放时重置附件与描述符绑定 */
	void resize(VkExtent2D extent);

	/** 上传渲染数据并切换帧上下文 */
	void upload(){
		frames_.advance();
		auto& frame = frames_.current_frame();
		try{
			if(frame.external_submit_fence){
				// The external submitter owns this fence. In the swapchain-output path,
				// context waits and resets it before this frame slot is reused.
				frame.external_submit_fence = VK_NULL_HANDLE;
			} else{
				frame.fence.wait_and_reset();
			}
			batch_device.upload(batch_host, *image_view_registry_, frames_.current_index());
		} catch(...){
			if(!frame.external_submit_fence){
				frame.fence.reset();
			}
			frame.external_submit_fence = VK_NULL_HANDLE;
		}
	}

	/** 录制当前帧的主指令缓冲 */
	void create_command(){
		vk::scoped_recorder recorder{
				frames_.current_command_buffer(), VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
			};
		record_ctx_.record(*this, recorder);
	}

	gui::renderer_frontend create_frontend() noexcept{
		return gui::renderer_frontend{
				table, table_non_vertex, {
					*this,
					[](renderer& r, graphic::g2d::instruction_head h, const std::byte* d) static{
						return r.batch_host.push_instr(h, d);
					},
					[](renderer& r, std::span<const graphic::g2d::instruction_head> h,
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
		return frames_.current_command_buffer();
	}

	const vk::fence& get_fence() const noexcept{ return frames_.current_fence(); }
	vk::fence& get_fence() noexcept{ return frames_.current_fence(); }

	void set_current_external_submit_fence(const VkFence fence) noexcept{
		frames_.current_frame().external_submit_fence = fence;
	}

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
		mask_attachment_states_invalidated_ = true;
		mask_descriptor_buffer_ = vk::descriptor_buffer{
			allocator_usage_,
			draw_pipeline_manager_.get_mask_descriptor_set_layout(),
			draw_pipeline_manager_.get_mask_descriptor_set_layout().binding_count(),
			mask_depth};

		vk::descriptor_mapper mapper{mask_descriptor_buffer_};
		for(unsigned i = 0; i < mask_depth; ++i){
			mapper.set_image(0,
				attachment_manager_.get_mask_image_views()[i], i, VK_IMAGE_LAYOUT_GENERAL, nullptr, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
		}
	}

	void initialize_frames(VkCommandPool pool){
		frames_.initialize(allocator_usage_.get_device(), pool);
		blit_attachment_clear_and_init_command_buffer = vk::command_buffer{
				allocator_usage_.get_device(), pool, VK_COMMAND_BUFFER_LEVEL_SECONDARY
			};
	}

	void create_blit_clear_and_init_cmd() const{
		vk::scoped_recorder recorder{
				blit_attachment_clear_and_init_command_buffer, VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT, true
			};
		mo_yanxi::backend::vulkan::record_renderer_attachment_clear_and_init_command(
			recorder,
			attachment_manager_);
	}
};
} // namespace mo_yanxi::backend::vulkan
