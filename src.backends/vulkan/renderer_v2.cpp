module;

#include <vulkan/vulkan.h>
#include <cassert>

module mo_yanxi.backend.vulkan.renderer;

namespace mo_yanxi::backend::vulkan{
using mo_yanxi::vk::sync::image_state;
using mo_yanxi::vk::sync::sync_barrier_batch;

namespace{
constexpr image_state make_image_state(
	const VkPipelineStageFlags2 stage_mask,
	const VkAccessFlags2 access_mask,
	const VkImageLayout layout) noexcept{
	return image_state{{stage_mask, access_mask}, layout};
}

constexpr auto draw_write_state = make_image_state(
	VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
	VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
	VK_IMAGE_LAYOUT_GENERAL
);

constexpr auto draw_sample_state = make_image_state(
	VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
	VK_ACCESS_2_SHADER_READ_BIT,
	VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
);

constexpr auto blit_rw_state = make_image_state(
	VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
	VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
	VK_IMAGE_LAYOUT_GENERAL
);

constexpr auto blit_write_state = make_image_state(
	VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
	VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
	VK_IMAGE_LAYOUT_GENERAL
);

constexpr auto mask_write_state = make_image_state(
	VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
	VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
	VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
);

constexpr auto mask_read_state = make_image_state(
	VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
	VK_ACCESS_2_SHADER_READ_BIT,
	VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
);

VkImageSubresourceRange make_mask_layer_range(const std::uint32_t layer) noexcept{
	return {
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = 0,
		.levelCount = 1,
		.baseArrayLayer = layer,
		.layerCount = 1,
	};
}
}

void renderer::command_recording_context::rebuild_sync_resources_(renderer& r){
	cache_sync_mgr_.clear_resources();
	cache_sync_mgr_.reset_transient();
	cache_sync_mgr_.reserve_resources(
		r.attachment_manager_.get_draw_attachments().size() + r.attachment_manager_.get_blit_attachments().size() +
			r.attachment_manager_.get_mask_image_views().size(),
		0
	);

	draw_attachment_slots_.clear();
	blit_attachment_slots_.clear();
	mask_attachment_slots_.clear();
	draw_attachment_slots_.reserve(r.attachment_manager_.get_draw_attachments().size());
	blit_attachment_slots_.reserve(r.attachment_manager_.get_blit_attachments().size());
	mask_attachment_slots_.reserve(r.attachment_manager_.get_mask_image_views().size());

	for(std::size_t i = 0; i < r.attachment_manager_.get_draw_attachments().size(); ++i){
		draw_attachment_slots_.push_back(cache_sync_mgr_.register_image(draw_write_state));
	}
	for(std::size_t i = 0; i < r.attachment_manager_.get_blit_attachments().size(); ++i){
		blit_attachment_slots_.push_back(cache_sync_mgr_.register_image(blit_rw_state));
	}
	for(std::size_t i = 0; i < r.attachment_manager_.get_mask_image_views().size(); ++i){
		mask_attachment_slots_.push_back(cache_sync_mgr_.register_image(mask_read_state));
	}
}

void renderer::command_recording_context::ensure_render_pass_(
	renderer& r, VkCommandBuffer cmd,
	const gui::fx::pipeline_config& draw_cfg,
	per_record_context_value& ctx_val){
	if(ctx_val.is_rendering) return;

	const auto& pipe_opt = r.draw_pipeline_manager_.get_pipelines()[draw_cfg.pipeline_index].option;
	const bool is_msaa = pipe_opt.enables_multisample && r.attachment_manager_.enables_multisample();
	const auto target_mask = get_render_target(draw_cfg, pipe_opt);

	cache_barrier_gen_.clear();

	target_mask.for_each_popbit([&](unsigned idx){
		(void)cache_sync_mgr_.transition_image(
			cache_barrier_gen_,
			draw_attachment_slots_[idx],
			r.attachment_manager_.get_draw_attachments()[idx].get_image(),
			draw_write_state
		);
	});

	pipe_opt.input_attachments_mask.for_each_popbit([&](unsigned idx){
		(void)cache_sync_mgr_.transition_image(
			cache_barrier_gen_,
			draw_attachment_slots_[idx],
			r.attachment_manager_.get_draw_attachments()[idx].get_image(),
			draw_sample_state
		);
	});

	for(std::size_t idx = 0; idx < blit_attachment_slots_.size(); ++idx){
		(void)cache_sync_mgr_.transition_image(
			cache_barrier_gen_,
			blit_attachment_slots_[idx],
			r.attachment_manager_.get_blit_attachments()[idx].get_image(),
			blit_rw_state
		);
	}

	if(pipe_opt.mask_usage_type != mask_usage::ignore && mask_attachment_slots_.size() <= ctx_val.mask_depth){
		const auto old_size = mask_attachment_slots_.size();
		mask_attachment_slots_.resize(ctx_val.mask_depth + 1);
		for(std::size_t idx = old_size; idx < mask_attachment_slots_.size(); ++idx){
			mask_attachment_slots_[idx] = cache_sync_mgr_.register_image(mask_read_state);
		}
	}

	if(const auto& mask_image = r.attachment_manager_.get_mask_image().get_image(); mask_image.get() != VK_NULL_HANDLE){
		if(pipe_opt.mask_usage_type == mask_usage::write){
			(void)cache_sync_mgr_.transition_image(
				cache_barrier_gen_,
				mask_attachment_slots_[ctx_val.mask_depth],
				mask_image,
				mask_write_state,
				make_mask_layer_range(ctx_val.mask_depth)
			);

			if(ctx_val.mask_depth > 0){
				const auto read_layer = ctx_val.mask_depth - 1;
				(void)cache_sync_mgr_.transition_image(
					cache_barrier_gen_,
					mask_attachment_slots_[read_layer],
					mask_image,
					mask_read_state,
					make_mask_layer_range(read_layer)
				);
			}
		} else if(pipe_opt.mask_usage_type == mask_usage::read){
			(void)cache_sync_mgr_.transition_image(
				cache_barrier_gen_,
				mask_attachment_slots_[ctx_val.mask_depth],
				mask_image,
				mask_read_state,
				make_mask_layer_range(ctx_val.mask_depth)
			);
		}
	}

	cache_barrier_gen_.apply(cmd);

	VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	auto m_usage = pipe_opt.mask_usage_type;
	auto m_depth = ctx_val.mask_depth;

	if(m_usage == mask_usage::write){
		if(cache_mask_layer_enter_mark_[m_depth] == 0){
			loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			cache_mask_layer_enter_mark_[m_depth] = 1;
		}
	}

	r.attachment_manager_.configure_dynamic_rendering<32>(
		cache_rendering_config_,
		target_mask,
		{},
		is_msaa,
		pipe_opt.mask_usage_type, ctx_val.mask_depth, loadOp
	);

	std::size_t cur_slot = 0;

	target_mask.for_each_popbit([&](unsigned idx){
		auto& info = cache_rendering_config_.get_color_attachment_infos()[cur_slot++];
		info.loadOp = cache_attachment_enter_mark_[idx]
			              ? VK_ATTACHMENT_LOAD_OP_LOAD
			              : VK_ATTACHMENT_LOAD_OP_CLEAR;
		info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		cache_attachment_enter_mark_[idx] = true;
	});

	cache_rendering_config_.begin_rendering(cmd, r.attachment_manager_.get_screen_area());

	// 更新上下文状态
	ctx_val.is_rendering = true;
	ctx_val.current_pass_mask = target_mask;
	ctx_val.current_pass_msaa = is_msaa;
	ctx_val.current_pass_mask_usage = pipe_opt.mask_usage_type;
	ctx_val.current_pass_mask_depth = ctx_val.mask_depth;
}


void renderer::command_recording_context::record(renderer& r, VkCommandBuffer cmd){
	// 使用局部变量实例化单次记录的上下文状态
	per_record_context_value ctx_val{};

	for(auto depth_record : r.batch_host.get_tracker().get_depth_records()){
		if(depth_record.tag == std::to_underlying(gui::fx::state_type::mask_op)){
			r.update_mask_depth_(depth_record.max_depth + 1);
			cache_mask_layer_enter_mark_.assign(depth_record.max_depth + 1, {});
		}
	}

	std::ranges::fill(cache_mask_layer_enter_mark_, 0);

	cache_descriptor_context_.reset_binding_state();

	vkCmdExecuteCommands(cmd, 1, r.blit_attachment_clear_and_init_command_buffer.as_data());
	const auto section_count = r.batch_host.get_section_count();
	if(r.batch_host.get_valid_submit_groups().empty()) return;
	if(r.batch_device.is_frame_empty()) return;

	{
		r.resolver_pipeline_.bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);

		cache_descriptor_context_.clear();
		r.batch_device.load_cs_descriptors(cache_descriptor_context_, r.current_frame_index_);
		cache_descriptor_context_.prepare_bindings();
		cache_descriptor_context_(r.resolver_pipeline_layout_, cmd, 0, VK_PIPELINE_BIND_POINT_COMPUTE);
		r.batch_device.cmd_compute_resolve(cmd, r.current_frame_index_);

		cache_barrier_gen_.clear();
		cache_barrier_gen_.push_execution(
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
			VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT |
				VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
			VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT |
				VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT
		);
		cache_barrier_gen_.apply(cmd);
	}

	cache_graphic_context_.reset();
	for(auto& c : cache_push_constants_) c.clear();
	gui::fx::pipeline_config draw_cfg{.pipeline_index = 0};

	const auto& initial_pipe_opt = r.draw_pipeline_manager_.get_pipelines()[draw_cfg.pipeline_index].option;
	cache_graphic_context_.update_pipeline(draw_cfg.pipeline_index, initial_pipe_opt);

	cache_sync_mgr_.reset_transient();
	for(const auto& slot : draw_attachment_slots_){
		cache_sync_mgr_.set_image_state(slot, draw_write_state);
	}
	for(const auto& slot : blit_attachment_slots_){
		cache_sync_mgr_.set_image_state(slot, blit_rw_state);
	}
	for(const auto& slot : mask_attachment_slots_){
		cache_sync_mgr_.set_image_state(slot, mask_read_state);
	}
	if(r.mask_attachment_states_invalidated_){
		for(const auto& slot : mask_attachment_slots_){
			cache_sync_mgr_.set_image_state(slot, {});
		}
		r.mask_attachment_states_invalidated_ = false;
	}
	std::ranges::fill(cache_attachment_enter_mark_, 0);
	cache_rendering_config_.clear_color_attachments();

	auto get_section_params = [&](const graphic::draw::instruction::section_state_delta_set::exported_entry& e) noexcept{
		return section_state_apply_params{
				e, cache_graphic_context_, draw_cfg, ctx_val
			};
	};

	for(unsigned i = 0; i < section_count; ++i){
		bool requires_clear = false;

		if(r.batch_device.is_section_empty(i)){
			for(const auto& entry : r.batch_host.get_section_state_deltas(i).get_entries()){
				requires_clear |= apply_section_state_(r, get_section_params(entry), cmd);
			}
		} else{
			ensure_render_pass_(r, cmd, draw_cfg, ctx_val);

			cache_graphic_context_.apply(cmd, r.draw_pipeline_manager_.get_pipelines());
			cmd_draw_(r, cmd, i, draw_cfg, ctx_val);

			for(const auto& entry : r.batch_host.get_section_state_deltas(i).get_entries()){
				requires_clear |= apply_section_state_(r, get_section_params(entry), cmd);
			}
		}

		if(requires_clear){
			flush_pass_(cmd, ctx_val);
			std::ranges::fill(cache_attachment_enter_mark_, 0);
		}
	}

	flush_pass_(cmd, ctx_val);
}

bool renderer::command_recording_context::apply_section_state_(
	renderer& r, const section_state_apply_params& params, VkCommandBuffer buffer){
	auto& cur_pipe = r.draw_pipeline_manager_.get_pipelines()[params.draw_cfg.pipeline_index];
	using namespace gui::fx;
	switch(static_cast<state_type>(params.entry.tag.major)){
	case state_type::blit :{
		params.flush(*this, buffer);
		auto cfg = params.entry.as<blit_config>();
		blit_(r, cfg, buffer);
		return !cfg.reserve_original;
	}
	case state_type::pipe :{
		auto param = params.entry.as<pipeline_config>();
		if(param.use_fallback_pipeline()) param.pipeline_index = params.draw_cfg.pipeline_index;
		params.draw_cfg = param;

		const auto& pipe_opt = r.draw_pipeline_manager_.get_pipelines()[params.draw_cfg.pipeline_index].option;
		params.context_trace.update_pipeline(params.draw_cfg.pipeline_index, pipe_opt);
		for(auto& c : cache_push_constants_) c.clear();

		if(params.ctx_val.is_rendering){
			const bool is_new_msaa = pipe_opt.enables_multisample && r.attachment_manager_.enables_multisample();
			const auto new_target = get_render_target(params.draw_cfg, pipe_opt);
			if(new_target != params.ctx_val.current_pass_mask || is_new_msaa != params.ctx_val.current_pass_msaa ||
				pipe_opt.mask_usage_type !=
				params.ctx_val.current_pass_mask_usage){
				flush_pass_(buffer, params.ctx_val);
			}
		}
		return false;
	}
	case state_type::split:{
		params.flush(*this, buffer);
		return false;
	}
	case state_type::push_constant :{
		const auto flags = static_cast<VkShaderStageFlags>(params.entry.tag.minor);
		const auto stg_idx = stage_index(flags);
		auto& cache = cache_push_constants_[stg_idx];
		const auto off = params.entry.logical_offset;
		const auto sz  = params.entry.payload.size();

		if(off + sz <= cache.size() && std::memcmp(cache.data() + off, params.entry.payload.data(), sz) == 0){
			break;
		}
		if(off + sz > cache.size()) cache.resize(off + sz, std::byte{0});
		std::memcpy(cache.data() + off, params.entry.payload.data(), sz);

		vkCmdPushConstants(buffer, cur_pipe.pipeline_layout, flags, off, sz,
		                   params.entry.payload.data());
		break;
	}
	case state_type::set_color_blend_enable :{
		auto param = params.entry.as<blend_enable_flag>();
		auto mask = make_render_target_mask(cur_pipe.option, params.draw_cfg, params.entry.tag.minor);
		mask.for_each_popbit([&](unsigned i){ params.context_trace.set_blend_enable(i, param); });
		break;
	}
	case state_type::set_color_blend_equation :{
		auto param = params.entry.as<blend_equation>();
		auto mask = make_render_target_mask(cur_pipe.option, params.draw_cfg, params.entry.tag.minor);
		mask.for_each_popbit([&](unsigned i){ params.context_trace.set_blend_equation(i, param); });
		break;
	}
	case state_type::set_color_write_mask :{
		auto param = params.entry.as<blend_write_mask_type>();
		auto mask = make_render_target_mask(cur_pipe.option, params.draw_cfg, params.entry.tag.minor);
		mask.for_each_popbit([&](unsigned i){ params.context_trace.set_blend_write_mask(i, param); });
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
		ensure_render_pass_(r, buffer, params.draw_cfg, params.ctx_val);
		auto mask = make_render_target_mask(cur_pipe.option, params.draw_cfg, params.entry.tag.minor);
		cache_clear_attachments_.clear();
		cache_clear_rects_.clear();

		if(params.entry.payload.size() == sizeof(color_clear_value)){
			auto param = params.entry.as<color_clear_value>();
			mask.for_each_popbit([&](unsigned i){
				cache_clear_attachments_.push_back({
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.colorAttachment = i,
						.clearValue = param
					});
			});
			VkClearRect rect{
					.rect = {{}, r.attachment_manager_.get_extent()},
					.baseArrayLayer = 0,
					.layerCount = 1
				};
			vkCmdClearAttachments(buffer, cache_clear_attachments_.size(), cache_clear_attachments_.data(), 1, &rect);
		} else{
			throw std::runtime_error{"not impl"};
		}
		break;
	}
	case state_type::fill_color_other_lazy :{
		render_target_mask mask{params.entry.tag.minor};
		mask.for_each_popbit([&](unsigned i){
			if(i < cache_attachment_enter_mark_.size()){
				cache_attachment_enter_mark_[i] = 0;
			}
		});
		break;
	}
	case state_type::mask_op :{
		if(cur_pipe.option.mask_usage_type == mask_usage::write){
			params.flush(*this, buffer);
		}

		if([[maybe_unused]] bool is_push = params.entry.tag.minor){
			++params.ctx_val.mask_depth;
			cache_mask_layer_enter_mark_[params.ctx_val.mask_depth] = 0;
		} else{
			assert(params.ctx_val.mask_depth != 0);
			--params.ctx_val.mask_depth;
		}

		if(params.ctx_val.is_rendering && params.ctx_val.mask_depth != params.ctx_val.current_pass_mask_depth){
			flush_pass_(buffer, params.ctx_val);
		}
		break;
	}

	default : throw std::runtime_error{"not implemented"};
	}
	return false;
}

void renderer::command_recording_context::cmd_draw_(
	renderer& r,
	VkCommandBuffer cmd,
	std::uint32_t index,
	const gui::fx::pipeline_config& arg,
	per_record_context_value& ctx_val){
	const auto& pc = r.draw_pipeline_manager_.get_pipelines()[arg.pipeline_index];

	cache_descriptor_context_.clear();
	r.batch_device.load_gfx_descriptors(cache_descriptor_context_, r.current_frame_index_);
	auto& pipe_opt = r.draw_pipeline_manager_.get_pipelines()[arg.pipeline_index];
	auto& pipe = r.draw_pipeline_manager_.get_input_attachment_mock_descriptor()[arg.pipeline_index];
	if(pipe.buffer){
		cache_descriptor_context_.push(2, pipe.buffer);
	}

	switch(pipe_opt.option.mask_usage_type){
	case mask_usage::ignore : break;
	case mask_usage::read : cache_descriptor_context_.push(
			2 + !!pipe.buffer, r.mask_descriptor_buffer_,
			0,
			r.mask_descriptor_buffer_.get_chunk_size() * ctx_val.mask_depth);
		break;
	case mask_usage::write :
		assert(ctx_val.mask_depth > 0 && "Cannot write to the root mask layer (Layer 0)");

		cache_descriptor_context_.push(
			2 + !!pipe.buffer, r.mask_descriptor_buffer_,
			0,
			r.mask_descriptor_buffer_.get_chunk_size() * (ctx_val.mask_depth - 1));
		break;
	}

	cache_descriptor_context_.prepare_bindings();
	cache_descriptor_context_(pc.pipeline_layout, cmd, index, VK_PIPELINE_BIND_POINT_GRAPHICS);

	r.batch_device.cmd_draw_direct(cmd, r.current_frame_index_, index);
}

void renderer::command_recording_context::blit_(renderer& r, gui::fx::blit_config cfg, VkCommandBuffer cmd){
	if(cfg.blit_region.extent == math::vectors::constant2<int>::max_vec2){
		assert(cfg.blit_region.src.is_zero());
		auto [w, h] = r.attachment_manager_.get_extent();
		cfg.blit_region.extent.set(w, h);
	} else{
		cfg.get_clamped_to_positive();
	}

	const auto& inout = r.get_blit_inout_config(cfg);

	cache_barrier_gen_.clear();
	for(const auto& e : inout.get_input_entries()){
		(void)cache_sync_mgr_.transition_image(
			cache_barrier_gen_,
			draw_attachment_slots_[e.resource_index],
			r.attachment_manager_.get_draw_attachments()[e.resource_index].get_image(),
			blit_rw_state
		);
	}
	for(const auto& e : inout.get_output_entries()){
		(void)cache_sync_mgr_.transition_image(
			cache_barrier_gen_,
			blit_attachment_slots_[e.resource_index],
			r.attachment_manager_.get_blit_attachments()[e.resource_index].get_image(),
			blit_rw_state
		);
	}
	cache_barrier_gen_.apply(cmd);


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

	for(const auto& e : inout.get_output_entries()){
		cache_sync_mgr_.set_image_state(blit_attachment_slots_[e.resource_index], blit_write_state);
	}
}

void renderer::resize(VkExtent2D extent){
	const bool attachments_recreated = attachment_manager_.resize(extent);
	if(attachments_recreated && !attachment_manager_.get_mask_image_views().empty()){
		mask_attachment_states_invalidated_ = true;
	}
	update_mask_depth_(1);

	record_ctx_.resize(attachment_manager_.get_draw_attachments().size(),
	                   attachment_manager_.get_blit_attachments().size());
	record_ctx_.rebuild_sync_resources_(*this);

	auto update_descriptor = [this](vk::descriptor_buffer& db, const compute_pipeline_blit_inout_config& cfg){
		vk::descriptor_mapper mapper{db};
		for(const auto& in : cfg.get_input_entries()){
			(void)mapper.set_image(in.binding, {
				                       nullptr,
				                       attachment_manager_.get_draw_attachments()[in.resource_index].
				                       get_image_view(),
				                       in.type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
					                       ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					                       : VK_IMAGE_LAYOUT_GENERAL
			                       }, 0, in.type);
		}
		for(const auto& out : cfg.get_output_entries()){
			(void)mapper.set_image(out.binding, {
				                       nullptr,
				                       attachment_manager_.get_blit_attachments()[out.resource_index].
				                       get_image_view(),
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


	for(auto&& [pipe, desc] : std::views::zip(draw_pipeline_manager_.get_pipelines(),
	                                          draw_pipeline_manager_.get_input_attachment_mock_descriptor())){
		if(!desc.buffer) continue;

		vk::descriptor_mapper m{desc.buffer};
		pipe.option.input_attachments_mask.for_each_popbit([&, idx = 0](unsigned i) mutable{
			m.set_image(idx, attachment_manager_.get_draw_attachments()[i].get_image_view(), 0,
			            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, nullptr, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
			++idx;
		});
	}

	{
		vk::descriptor_mapper mapper{mask_descriptor_buffer_};
		for(auto&& [i, mask_image_view] : attachment_manager_.get_mask_image_views() | std::views::enumerate){
			mapper.set_image(0, mask_image_view, i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, nullptr,
			                 VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
		}
	}


	create_blit_clear_and_init_cmd();
}
}
