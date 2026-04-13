module;

#include <vulkan/vulkan.h>

module mo_yanxi.backend.vulkan.renderer;

namespace mo_yanxi::backend::vulkan{
void renderer::command_recording_context::record(renderer& r, VkCommandBuffer cmd){
	vkCmdExecuteCommands(cmd, 1, r.blit_attachment_clear_and_init_command_buffer.as_data());
	const auto section_count = r.batch_host.get_submit_sections_count();
	if(r.batch_host.get_valid_submit_groups().empty()) return;
	if(r.batch_device.is_frame_empty()) return;

	{
		r.resolver_pipeline_.bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);

		cache_descriptor_context_.clear();
		r.batch_device.load_cs_descriptors(cache_descriptor_context_, r.current_frame_index_);
		cache_descriptor_context_.prepare_bindings();
		cache_descriptor_context_(r.resolver_pipeline_layout_, cmd, 0, VK_PIPELINE_BIND_POINT_COMPUTE);
		r.batch_device.cmd_compute_resolve(cmd, r.current_frame_index_);

		static constexpr VkMemoryBarrier2 memory_barrier{
				.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
				.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT |
				VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
				.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT |
				VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT
			};
		static constexpr VkDependencyInfo dep_info{
				.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
				.memoryBarrierCount = 1,
				.pMemoryBarriers = &memory_barrier
			};
		vkCmdPipelineBarrier2(cmd, &dep_info);
	}

	cache_graphic_context_.reset();
	gui::fx::pipeline_config draw_cfg{.pipeline_index = 0};
	bool is_rendering = false;
	gui::fx::render_target_mask current_pass_mask{};
	bool current_pass_msaa = false;

	cache_sync_mgr_.reset_barriers();
	std::ranges::fill(cache_attachment_enter_mark_, 0);
	cache_rendering_config_.clear_color_attachments();

	auto flush_pass = [&](){
		if(is_rendering){
			vkCmdEndRendering(cmd);
			is_rendering = false;
			cache_graphic_context_.set_rebind_required();
		}
	};

	const bool global_msaa = r.attachment_manager_.enables_multisample();

	for(unsigned i = 0; i < section_count; ++i){
		const auto& pipe_opt = r.draw_pipeline_manager_.get_pipelines()[draw_cfg.pipeline_index].option;
		const bool is_msaa = pipe_opt.enables_multisample && global_msaa;
		const auto target_mask = get_render_target(draw_cfg, pipe_opt);

		cache_barrier_gen_.clear();

		cache_sync_mgr_.sync_draw_targets(cache_barrier_gen_, target_mask, r.attachment_manager_.get_draw_attachments());

		if(is_rendering && (!cache_barrier_gen_.empty() || target_mask != current_pass_mask || is_msaa !=
			current_pass_msaa)){
			flush_pass();
		}

		if(!cache_barrier_gen_.empty()){
			cache_barrier_gen_.apply(cmd);
		}

		if(!is_rendering){
			cache_sync_mgr_.commit_pending_blit_to_graphics(cache_barrier_gen_, r.attachment_manager_.get_blit_attachments());
			cache_barrier_gen_.apply(cmd);

			r.attachment_manager_.configure_dynamic_rendering<32>(
				cache_rendering_config_,
				get_render_target(draw_cfg, pipe_opt),
				is_msaa
			);

			std::size_t slot = 0;

			target_mask.for_each_popbit([&](unsigned idx){
				auto& info = cache_rendering_config_.get_color_attachment_infos()[slot++];
				info.loadOp = cache_attachment_enter_mark_[idx]
					              ? VK_ATTACHMENT_LOAD_OP_LOAD
					              : VK_ATTACHMENT_LOAD_OP_CLEAR;
				cache_attachment_enter_mark_[idx] = true;
			});

			cache_rendering_config_.begin_rendering(cmd, r.attachment_manager_.get_screen_area());
			is_rendering = true;
			current_pass_mask = target_mask;
			current_pass_msaa = is_msaa;
		}

		bool requires_clear = false;

		if(r.batch_device.is_section_empty(i)){
			for(const auto& entry : r.batch_host.get_break_config_at(i).get_entries()){
				requires_clear |= process_breakpoints_(r, breakpoint_process_params{entry, cache_graphic_context_, draw_cfg, is_rendering}, cmd);
			}
		} else{
			cache_graphic_context_.apply(cmd, r.draw_pipeline_manager_);
			cmd_draw_(r, cmd, i, draw_cfg);

			for(const auto& entry : r.batch_host.get_break_config_at(i).get_entries()){
				requires_clear |= process_breakpoints_(r, breakpoint_process_params{entry, cache_graphic_context_, draw_cfg, is_rendering}, cmd);
			}
		}

		if(requires_clear){
			flush_pass();
			std::ranges::fill(cache_attachment_enter_mark_, 0);
		}
	}

	flush_pass();
}

void renderer::command_recording_context::cmd_draw_(renderer& r, VkCommandBuffer cmd, std::uint32_t index,
	const gui::fx::pipeline_config& arg){
	const auto& pc = r.draw_pipeline_manager_.get_pipelines()[arg.pipeline_index];

	cache_descriptor_context_.clear();
	r.batch_device.load_gfx_descriptors(cache_descriptor_context_, r.current_frame_index_);
	cache_descriptor_context_.prepare_bindings();
	cache_descriptor_context_(pc.pipeline_layout, cmd, index, VK_PIPELINE_BIND_POINT_GRAPHICS);

	r.batch_device.cmd_draw_direct(cmd, r.current_frame_index_, index);
}

void renderer::command_recording_context::blit_(renderer& r, gui::fx::blit_config cfg, VkCommandBuffer cmd){
	cfg.get_clamped_to_positive();
	const auto& inout = r.get_blit_inout_config(cfg);

	cache_barrier_gen_.clear();
	cache_sync_mgr_.sync_blit_inputs(cache_barrier_gen_, inout.get_input_entries(),
	                          r.attachment_manager_.get_draw_attachments());
	cache_sync_mgr_.sync_blit_outputs(cache_barrier_gen_, inout.get_output_entries(),
	                           r.attachment_manager_.get_blit_attachments());
	if(!cache_barrier_gen_.empty()){
		cache_barrier_gen_.apply(cmd);
	}


	const auto& pc = r.blit_pipeline_manager_.get_pipelines()[cfg.pipe_info.pipeline_index];
	pc.pipeline.bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);

	math::upoint2 offset = cfg.blit_region.src.as<unsigned>();
	vkCmdPushConstants(cmd, pc.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(offset), &offset);

	const VkDescriptorBufferBindingInfoEXT db_info =
		cfg.use_default_inouts()
			? r.blit_default_inout_descriptors_[cfg.pipe_info.pipeline_index]
			: r.blit_specified_inout_descriptors_[cfg.pipe_info.inout_define_index];

	cache_descriptor_context_.clear();
	cache_descriptor_context_.push(0, db_info);

	r.blit_pipeline_manager_.append_descriptor_buffers(cache_descriptor_context_, cfg.pipe_info.pipeline_index);
	cache_descriptor_context_.prepare_bindings();
	cache_descriptor_context_(pc.pipeline_layout, cmd, 0, VK_PIPELINE_BIND_POINT_COMPUTE);

	auto dispatches = cfg.get_dispatch_groups();
	if(dispatches.area() > 0){
		vkCmdDispatch(cmd, dispatches.x, dispatches.y, 1);
	}

	cache_sync_mgr_.mark_blit_completed(inout.get_output_entries());
}

bool renderer::command_recording_context::process_breakpoints_(renderer& r, breakpoint_process_params params,
	VkCommandBuffer buffer){
	auto& cur_pipe = r.draw_pipeline_manager_.get_pipelines()[params.draw_cfg.pipeline_index];
	using namespace gui::fx;
	switch(static_cast<state_type>(params.entry.tag.major)){
	case state_type::blit :{
		params.flush(*this, buffer);

		// Blit 必须在渲染通道外
		blit_(r, params.entry.as<blit_config>(), buffer);
		//TODO separate clean request and blit?
		return true;
	}
	case state_type::pipe :{
		auto param = params.entry.as<pipeline_config>();

		if(param.use_fallback_pipeline()) param.pipeline_index = params.draw_cfg.pipeline_index;
		params.draw_cfg = param;
		params.context_trace.update_pipeline(params.draw_cfg.pipeline_index);
		return false;
	}
	case state_type::push_constant :{
		const auto flags = static_cast<VkShaderStageFlags>(params.entry.tag.minor);

		vkCmdPushConstants(buffer, cur_pipe.pipeline_layout, flags, params.entry.logical_offset, params.entry.payload.size(),
		                   params.entry.payload.data());
		break;
	}
	case state_type::set_color_blend_enable :{
		auto param = params.entry.as<blend_enable_flag>();
		auto mask = make_render_target_mask(cur_pipe.option, params.draw_cfg, params.entry.tag.minor);

		mask.for_each_popbit([&](unsigned i){
			params.context_trace.set_blend_enable(i, param);
		});
		break;
	}
	case state_type::set_color_blend_equation :{
		auto param = params.entry.as<blend_equation>();
		auto mask = make_render_target_mask(cur_pipe.option, params.draw_cfg, params.entry.tag.minor);
		mask.for_each_popbit([&](unsigned i){
			params.context_trace.set_blend_equation(i, param);
		});
		break;
	}
	case state_type::set_color_write_mask :{
		auto param = params.entry.as<blend_write_mask_type>();
		auto mask = make_render_target_mask(cur_pipe.option, params.draw_cfg, params.entry.tag.minor);
		mask.for_each_popbit([&](unsigned i){
			params.context_trace.set_blend_write_mask(i, param);
		});
		break;
	}
	case state_type::set_scissor :{
		auto param = params.entry.as<scissor>();
		params.context_trace.set_scissor(param);
		break;
	}
	case state_type::set_viewport :{
		auto param = params.entry.as<viewport>();
		params.context_trace.set_viewport(param);
		break;
	}
	case state_type::fill_color_local :{
		auto mask = make_render_target_mask(cur_pipe.option, params.draw_cfg, params.entry.tag.minor);
		cache_clear_attachments_.clear();
		cache_clear_rects_.clear();

		if(params.entry.payload.size() == sizeof(color_clear_value)){
			//only color clear value is set, make full screen clear.
			auto param = params.entry.as<color_clear_value>();

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
	case state_type::fill_color_other_lazy :{
		render_target_mask mask{params.entry.tag.minor};

		mask.for_each_popbit([&](unsigned i){
			if(i < cache_attachment_enter_mark_.size()){
				// 重置 enter_mark。
				// 下一次渲染通道切换并包含此附件时，
				// attachment_enter_mark[idx] 为 false，自然会使用 VK_ATTACHMENT_LOAD_OP_CLEAR
				cache_attachment_enter_mark_[i] = 0;
			}
		});
		break;
	}
	default : break;
	}
	return false;
}
}
