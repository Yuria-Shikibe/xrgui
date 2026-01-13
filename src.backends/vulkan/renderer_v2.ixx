module;

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

export module mo_yanxi.backend.vulkan.renderer;

import mo_yanxi.graphic.draw.instruction.batch.frontend;
import mo_yanxi.graphic.draw.instruction.batch.backend.vulkan;


import mo_yanxi.vk;
import mo_yanxi.vk.cmd;
import mo_yanxi.vk.util;
export import mo_yanxi.gui.renderer.frontend;
export import mo_yanxi.gui.draw_config;
export import mo_yanxi.backend.vulkan.attachment_manager;
export import mo_yanxi.backend.vulkan.pipeline_manager;
import std;

namespace mo_yanxi::backend::vulkan{
const graphic::draw::data_layout_table table{
	std::in_place_type<gui::gui_reserved_user_data_tuple>
};

const graphic::draw::data_layout_table table_non_vertex{
	std::in_place_type<std::tuple<gui::draw_config::ui_state, gui::draw_config::slide_line_config>>
};

export
struct renderer_create_info{
	vk::allocator_usage allocator_usage;
	VkCommandPool command_pool;
	VkSampler sampler;
	draw_attachment_create_info attachment_draw_config;
	blit_attachment_create_info attachment_blit_config;

	graphic_pipeline_create_config draw_pipe_config;
	compute_pipeline_create_config blit_pipe_config;
};

struct attachment_state {
	VkPipelineStageFlags2 stage_mask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
	VkAccessFlags2 access_mask = VK_ACCESS_2_NONE;
	VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

export
struct renderer{
	static constexpr std::size_t frames_in_flight = 3;
	vk::allocator_usage allocator_usage_{};

public:
	graphic::draw::instruction::draw_list_context batch_host{};
	graphic::draw::instruction::batch_vulkan_executor batch_device{};

private:

	attachment_manager attachment_manager{};
	vk::dynamic_rendering rendering_config{};
	// std::vector<VkFormat> color_attachment_formats{};

	graphic_pipeline_manager draw_pipeline_manager_{};

	compute_pipeline_manager blit_pipeline_manager_{};
	std::vector<vk::descriptor_buffer> blit_default_inout_descriptors_{};
	std::vector<vk::descriptor_buffer> blit_specified_inout_descriptors_{};

	vk::command_seq<> command_seq_draw_{};
	vk::command_seq<> command_seq_blit_{};

	//TODO optimize the fence
	std::vector<vk::fence> fences_{};
	std::vector<vk::command_buffer> main_command_buffers_{};
	std::uint32_t current_frame_index_{frames_in_flight - 1};

	vk::command_buffer blit_attachment_clear_and_init_command_buffer{};


	std::vector<gui::draw_mode_param> cache_draw_param_stack_{};
	std::vector<std::uint8_t> cache_attachment_enter_mark_{};
	graphic::draw::record_context<> cache_record_context_{};

	std::vector<attachment_state> draw_attachment_states_{};
	std::vector<attachment_state> blit_attachment_states_{};
	vk::cmd::dependency_gen cache_barrier_gen_{};

	VkSampler sampler_{};

public:
	[[nodiscard]] explicit(false) renderer(
		renderer_create_info&& create_info
	)
		: allocator_usage_(create_info.allocator_usage)
		, batch_host([&]{
			VkPhysicalDeviceMeshShaderPropertiesEXT meshProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT};
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
		, attachment_manager{
			allocator_usage_, std::move(create_info.attachment_draw_config), std::move(create_info.attachment_blit_config)
		}
		, draw_pipeline_manager_(allocator_usage_, create_info.draw_pipe_config, batch_device.get_descriptor_set_layout(), attachment_manager.get_draw_config())
		, command_seq_draw_(allocator_usage_.get_device(), create_info.command_pool, 4,
			VK_COMMAND_BUFFER_LEVEL_SECONDARY)
		, command_seq_blit_(allocator_usage_.get_device(), create_info.command_pool, 4,
			VK_COMMAND_BUFFER_LEVEL_SECONDARY)
		, sampler_(create_info.sampler){

		fences_.reserve(frames_in_flight);
		main_command_buffers_.reserve(frames_in_flight);
		for(std::size_t i = 0; i < frames_in_flight; ++i){
			fences_.emplace_back(allocator_usage_.get_device(), true);
			main_command_buffers_.emplace_back(allocator_usage_.get_device(), create_info.command_pool);
		}

		blit_attachment_clear_and_init_command_buffer = vk::command_buffer{allocator_usage_.get_device(), create_info.command_pool, VK_COMMAND_BUFFER_LEVEL_SECONDARY};

		cache_attachment_enter_mark_.resize(attachment_manager.get_draw_attachments().size());


		blit_pipeline_manager_ = compute_pipeline_manager( allocator_usage_, create_info.blit_pipe_config);


		{
			auto sz = blit_pipeline_manager_.get_pipelines();
			blit_default_inout_descriptors_.reserve(sz.size());
			for(std::size_t i = 0; i < sz.size(); ++i){
				auto& b = blit_default_inout_descriptors_.emplace_back();
				b = vk::descriptor_buffer{allocator_usage_, blit_pipeline_manager_.get_inout_layouts()[i], blit_pipeline_manager_.get_inout_layouts()[i].binding_count()};
			}
		}

		{
			const auto inouts = blit_pipeline_manager_.get_inout_defines();
			blit_specified_inout_descriptors_.reserve(inouts.size());
			for (const auto & entries : inouts){
				vk::descriptor_layout layout{allocator_usage_.get_device(), VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT, entries.make_layout_builder()};
				blit_default_inout_descriptors_.push_back({allocator_usage_, layout, layout.binding_count()});
			}
		}

		draw_attachment_states_.resize(attachment_manager.get_draw_attachments().size());
		blit_attachment_states_.resize(attachment_manager.get_blit_attachments().size());
	}

	void resize(VkExtent2D extent){
		attachment_manager.resize(extent);
		draw_attachment_states_.resize(attachment_manager.get_draw_attachments().size());
		blit_attachment_states_.resize(attachment_manager.get_blit_attachments().size());

		auto make_desciptor_from_inout_def = [this](vk::descriptor_buffer& blit_descriptor, const compute_pipeline_blit_inout_config& cfg){
			vk::descriptor_mapper mapper{blit_descriptor};
			for (const auto & input_entry : cfg.get_input_entries()){
				mapper.set_image(input_entry.binding, {
					.sampler = nullptr,
					.imageView = attachment_manager.get_draw_attachments()[input_entry.resource_index].get_image_view(),
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL
				}, 0, input_entry.type);
			}

			for (const auto & output_entry : cfg.get_output_entries()){
				mapper.set_image(output_entry.binding, {
					.sampler = nullptr,
					.imageView = attachment_manager.get_blit_attachments()[output_entry.resource_index].get_image_view(),
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL
				}, 0, output_entry.type);
			}
		};

		for(auto&& [blit_descriptor, pipe] :
		    std::views::zip(blit_default_inout_descriptors_, blit_pipeline_manager_.get_pipelines())){
			const auto& option = pipe.option;
			make_desciptor_from_inout_def(blit_descriptor, option.inout);
		}

		for(auto&& [blit_descriptor, inout] : std::views::zip(blit_specified_inout_descriptors_, blit_pipeline_manager_.get_inout_defines())){
			make_desciptor_from_inout_def(blit_descriptor, inout);
		}

		create_blit_clear_and_init_cmd();
	}

	void wait_fence() {
		current_frame_index_ = (current_frame_index_ + 1) % frames_in_flight;
		fences_[current_frame_index_].wait_and_reset();
	}

	void create_command(){
		{
			vk::scoped_recorder recorder{main_command_buffers_[current_frame_index_], VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
			record_cmd(recorder);
		}
	}

	void upload(){
		wait_fence();
		batch_device.upload(batch_host, sampler_, current_frame_index_);
	}

	VkCommandBuffer get_valid_cmd_buf() const noexcept{
		return main_command_buffers_[current_frame_index_];
	}

	VkFence get_fence() const noexcept{
		return fences_[current_frame_index_];
	}

private:
	void create_pipe_binding_cmd(VkCommandBuffer cmdbuf, std::uint32_t index, const gui::draw_mode_param& arg){
		auto& pipe_config = draw_pipeline_manager_.get_pipelines()[arg.pipeline_index];

		pipe_config.pipeline.bind(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS);

		struct constant{
			std::uint32_t mode_flag;
		};
		const constant c{std::to_underlying(arg.mode)};

		vkCmdPushConstants(cmdbuf, pipe_config.pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(constant), &c);

		const VkRect2D region = attachment_manager.get_screen_area();
		vk::cmd::set_viewport(cmdbuf, region);
		vk::cmd::set_scissor(cmdbuf, region);

		cache_record_context_.clear();
		batch_device.load_descriptors(cache_record_context_, current_frame_index_);
		cache_record_context_.prepare_bindings();
		cache_record_context_(pipe_config.pipeline_layout, cmdbuf, index, VK_PIPELINE_BIND_POINT_GRAPHICS);

		batch_device.cmd_draw(cmdbuf, index, current_frame_index_);
	}

	void reset_barrier_context(){
		for(auto& state : draw_attachment_states_){
			state = {
				.stage_mask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				.access_mask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
				.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
			};
		}
		for(auto& state : blit_attachment_states_){
			state = {
				.stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.access_mask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				.layout = VK_IMAGE_LAYOUT_GENERAL
			};
		}
	}

	static void ensure_attachment_state(
		vk::cmd::dependency_gen& dep,
		attachment_state& state,
		VkImage image,
		VkPipelineStageFlags2 new_stage,
		VkAccessFlags2 new_access,
		VkImageLayout new_layout)
	{
		const bool layout_change = state.layout != new_layout;
		const bool is_read_only = (new_access & (VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT)) == 0;
		const bool was_read_only = (state.access_mask & (VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT)) == 0;

		if (!layout_change && is_read_only && was_read_only) {
			return;
		}

		dep.push(image, state.stage_mask, state.access_mask, new_stage, new_access, state.layout, new_layout, vk::image::default_image_subrange);

		state.stage_mask = new_stage;
		state.access_mask = new_access;
		state.layout = new_layout;
	}

	void record_cmd(VkCommandBuffer command_buffer){
		const auto cmd_sz = batch_host.get_submit_sections_count();

		vkCmdExecuteCommands(command_buffer, 1, blit_attachment_clear_and_init_command_buffer.as_data());

		reset_barrier_context();

		auto submit_span = batch_host.get_valid_submit_groups();
		if(submit_span.empty()) [[unlikely]] {
			return;
		}

		const VkRect2D region = attachment_manager.get_screen_area();

		std::ranges::fill(cache_attachment_enter_mark_, false);
		cache_draw_param_stack_.resize(1, {.pipeline_index = {}});
		configure_rendering_info(cache_draw_param_stack_.back());
		rendering_config.set_color_attachment_load_op(VK_ATTACHMENT_LOAD_OP_CLEAR);

		{
			const auto mask = get_current_target(cache_draw_param_stack_.back());
			mask.for_each_popbit([&](unsigned i){
				cache_attachment_enter_mark_[i] = true;
			});
		}

		cache_barrier_gen_.clear();

		//TODO optimize empty submit group
		for(unsigned i = 0; i < cmd_sz; ++i){

			// Ensure draw attachments are in correct state
			{
				const auto mask = get_current_target(cache_draw_param_stack_.back());
				mask.for_each_popbit([&](unsigned aIdx){
					ensure_attachment_state(
						cache_barrier_gen_,
						draw_attachment_states_[aIdx],
						attachment_manager.get_draw_attachments()[aIdx].get_image(),
						VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
						VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
						VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
					);
				});
				cache_barrier_gen_.apply(command_buffer);
			}

			rendering_config.begin_rendering(command_buffer, region/*, VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT*/);

			create_pipe_binding_cmd(command_buffer, i, cache_draw_param_stack_.back());

			vkCmdEndRendering(command_buffer);

			bool requiresClear = false;
			auto& cfg = batch_host.get_break_config_at(i);
			requiresClear = cfg.clear_draw_after_break;
			for (const auto & e : cfg.get_entries()){
				requiresClear = process_breakpoints(e, command_buffer) || requiresClear;
			}

			if(requiresClear){
				std::ranges::fill(cache_attachment_enter_mark_, 0);
			}

			const auto mask = get_current_target(cache_draw_param_stack_.back());
			std::size_t curIdx{};
			mask.for_each_popbit([&](unsigned aIdx){
				const bool first_enter = !std::exchange(cache_attachment_enter_mark_[aIdx], true);
				rendering_config.get_color_attachment_infos()[curIdx].loadOp = first_enter ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
				++curIdx;
			});

		}

	}

	/**
	 *
	 * @return clear required
	 */
	bool process_breakpoints(const graphic::draw::instruction::state_transition_entry& entry, VkCommandBuffer buffer) {
		switch(entry.flag){
		case gui::draw_state_index_deduce_v<gui::blit_config> :{
			blit(entry.as<gui::blit_config>(), buffer);

			return true;
		}
		case gui::draw_state_index_deduce_v<gui::draw_mode_param> :{
			auto param = entry.as<gui::draw_mode_param>();
			if(param.mode == gui::draw_mode::COUNT_or_fallback){
				cache_draw_param_stack_.pop_back();
				param = cache_draw_param_stack_.back();
			}else{
				if(param.pipeline_index == std::numeric_limits<std::uint32_t>::max()){
					param.pipeline_index = cache_draw_param_stack_.back().pipeline_index;
				}
				cache_draw_param_stack_.push_back(param);
			}
			configure_rendering_info(param);

			return false;
		}
		default : break;
		}
		return false;
	}

	const compute_pipeline_blit_inout_config& get_blit_inout_config(const gui::blit_config& cfg) {
		if (cfg.use_default_inouts()) {
			return blit_pipeline_manager_.get_pipelines()[cfg.pipeline_index].option.inout;
		} else {
			return blit_pipeline_manager_.get_inout_defines()[cfg.inout_define_index];
		}
	}

	void blit(gui::blit_config cfg, VkCommandBuffer buffer){
		cfg.get_clamped_to_positive();
			const auto dispatches = cfg.get_dispatch_groups();

			// Reuse barrier_gen_cache_ which is a member variable
			// Note: record_cmd clears it before loop and after apply.
			// When blit is called from process_breakpoints, cache should be empty.

			const auto& inout_cfg = get_blit_inout_config(cfg);

			for (const auto& entry : inout_cfg.get_input_entries()) {
				ensure_attachment_state(
					cache_barrier_gen_,
					draw_attachment_states_[entry.resource_index],
					attachment_manager.get_draw_attachments()[entry.resource_index].get_image(),
					VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
					VK_IMAGE_LAYOUT_GENERAL
				);
			}

			for (const auto& entry : inout_cfg.get_output_entries()) {
				ensure_attachment_state(
					cache_barrier_gen_,
					blit_attachment_states_[entry.resource_index],
					attachment_manager.get_blit_attachments()[entry.resource_index].get_image(),
					VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
					VK_IMAGE_LAYOUT_GENERAL
				);
			}

			cache_barrier_gen_.apply(buffer);

			auto& pipe_config = blit_pipeline_manager_.get_pipelines()[cfg.pipeline_index];
			pipe_config.pipeline.bind(buffer, VK_PIPELINE_BIND_POINT_COMPUTE);

			const math::upoint2 offset = cfg.blit_region.src.as<unsigned>();
			vkCmdPushConstants(buffer, pipe_config.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(offset), &offset);

			VkDescriptorBufferBindingInfoEXT info;
			if(cfg.use_default_inouts()){
				info = blit_default_inout_descriptors_[cfg.pipeline_index];
			}else{
				if(!blit_pipeline_manager_.is_inout_compatible(cfg.pipeline_index, cfg.inout_define_index)){
					throw std::runtime_error("Incompatible blit pipeline inout spec");
				}

				info = blit_specified_inout_descriptors_[cfg.inout_define_index];
			}

			cache_record_context_.clear();
			cache_record_context_.push(0, info);
			blit_pipeline_manager_.append_descriptor_buffers(cache_record_context_, 0);
			cache_record_context_.prepare_bindings();
			cache_record_context_(pipe_config.pipeline_layout, buffer, 0, VK_PIPELINE_BIND_POINT_COMPUTE);

			vkCmdDispatch(buffer, dispatches.x, dispatches.y, 1);

			// We apply pending barriers if any (though typically dispatch doesn't add any new ones unless we want to, but previous apply handled them)
			cache_barrier_gen_.apply(buffer);
	}
public:

	gui::renderer_frontend create_frontend() noexcept {
		using namespace graphic::draw::instruction;
		return gui::renderer_frontend{
			table, table_non_vertex, batch_backend_interface{
				*this,
				[](renderer& b, instruction_head head, const std::byte* data) static{
					return b.batch_host.push_instr(head, data);
				},
				[](renderer& b) static{},
				[](renderer& b) static{},
				[](renderer& b, state_push_config config, std::uint32_t flag, std::span<const std::byte> payload) static{
					b.batch_host.push_state(config, flag, payload);
				},
			}
		};
	}

	vk::image_handle get_base() const noexcept{
		return attachment_manager.get_blit_attachments()[0];
	}

	vk::image_handle get_draw_base() const noexcept{
		return attachment_manager.get_blit_attachments()[0];
	}

private:
	void create_blit_clear_and_init_cmd() const{
		vk::scoped_recorder recorder{blit_attachment_clear_and_init_command_buffer, VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT, true};

		vk::cmd::dependency_gen dependency;

		for (const auto & draw_attachment : attachment_manager.get_blit_attachments()){
			dependency.push(draw_attachment.get_image(),
				VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
				VK_ACCESS_2_NONE,
				VK_PIPELINE_STAGE_2_CLEAR_BIT,
				VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				vk::image::default_image_subrange
			);
		}

		dependency.apply(recorder);

		constexpr VkClearColorValue c{};

		for (const auto & draw_attachment : attachment_manager.get_blit_attachments()){
			vkCmdClearColorImage(recorder, draw_attachment.get_image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &c, 1, &vk::image::default_image_subrange);

			dependency.push(draw_attachment.get_image(),
			VK_PIPELINE_STAGE_2_CLEAR_BIT,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_GENERAL,
				vk::image::default_image_subrange
			);
		}

		for (const auto & draw_attachment : attachment_manager.get_draw_attachments()){
			dependency.push(draw_attachment.get_image(),
				VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
				VK_ACCESS_2_NONE,
				VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				vk::image::default_image_subrange
			);
		}

		for (const auto & draw_attachment : attachment_manager.get_multisample_attachments()){
			dependency.push(draw_attachment.get_image(),
				VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
				VK_ACCESS_2_NONE,
				VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				vk::image::default_image_subrange
			);
		}


		dependency.apply(recorder);
	}

	gui::draw_config::render_target_mask get_current_target(const gui::draw_mode_param& param) const noexcept{
		const auto& pipes = draw_pipeline_manager_.get_pipelines()[param.pipeline_index].option;

		return param.draw_targets.any()
				   ? param.draw_targets
				   : pipes.is_partial_target()
				   ? pipes.default_target_attachments
				   : gui::draw_config::render_target_mask{~0U};
	}

	void configure_rendering_info(const gui::draw_mode_param& param){
		const auto& pipes = draw_pipeline_manager_.get_pipelines()[param.pipeline_index].option;
		attachment_manager.configure_dynamic_rendering<32>(
			rendering_config,
			param.draw_targets.none() ? pipes.default_target_attachments : param.draw_targets,
			pipes.enables_multisample && attachment_manager.enables_multisample());


	}
};

}
