module;

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

export module renderer_v2;

export import instruction_draw.batch.v2;
import mo_yanxi.vk;
import mo_yanxi.vk.cmd;
import mo_yanxi.vk.util;
export import mo_yanxi.gui.renderer.frontend;
export import mo_yanxi.gui.draw_config;
export import mo_yanxi.backend.vulkan.attachment_manager;
export import mo_yanxi.backend.vulkan.pipeline_manager;
import std;

namespace mo_yanxi::backend::vulkan{
const graphic::draw::user_data_index_table table{
	std::in_place_type<gui::gui_reserved_user_data_tuple>
};

const graphic::draw::user_data_index_table table_non_vertex{
	std::in_place_type<std::tuple<gui::draw_config::ui_state, gui::draw_config::slide_line_config>>
};

struct ubo_blit_config{
	math::upoint2 offset;
	math::upoint2 _cap;
};

export
struct renderer_v2_create_info{
	vk::allocator_usage allocator_usage;
	VkCommandPool command_pool;
	VkSampler sampler;
	draw_attachment_create_info attachment_draw_config;
	blit_attachment_create_info attachment_blit_config;

	graphic_pipeline_create_config draw_pipe_config;
	compute_pipeline_create_config blit_pipe_config;
};

export
struct blit_descriptor{
	vk::descriptor_buffer buffer;

	[[nodiscard]] blit_descriptor() = default;
};

export
struct renderer_v2{
	vk::allocator_usage allocator_usage_{};

public:
	graphic::draw::instruction::batch_v2 batch{};

private:

	attachment_manager attachment_manager{};
	vk::dynamic_rendering rendering_config{};
	std::vector<VkFormat> color_attachment_formats{};

	graphic_pipeline_manager draw_pipeline_manager_{};

	vk::descriptor_layout blit_descriptor_layout_;
	vk::descriptor_buffer blit_descriptor_buffer_;
	vk::buffer blit_buffer_;
	compute_pipeline_manager blit_pipeline_manager_{};
	std::vector<blit_descriptor> blit_descriptors_{};

	vk::command_seq<> command_seq_draw_{};
	vk::command_seq<> command_seq_blit_{};

	//TODO optimize the fence
	vk::fence fence{};
	vk::command_buffer main_command_buffer{};
	vk::command_buffer blit_attachment_clear_and_init_command_buffer{};


	std::vector<gui::draw_mode_param> cache_draw_param_stack_{};
	std::vector<std::uint8_t> cache_attachment_enter_mark_{};
	graphic::draw::record_context<> cache_record_context_{};

public:
	[[nodiscard]] explicit(false) renderer_v2(
		renderer_v2_create_info&& create_info
	)
		: allocator_usage_(create_info.allocator_usage)
		, batch(allocator_usage_, {}, table, table_non_vertex, create_info.sampler)
		, attachment_manager{
			allocator_usage_, std::move(create_info.attachment_draw_config), std::move(create_info.attachment_blit_config)
		}
		, draw_pipeline_manager_(allocator_usage_, create_info.draw_pipe_config, batch.get_descriptor_set_layout(), attachment_manager.get_draw_config())
		, blit_descriptor_layout_(allocator_usage_.get_device(),
			VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT, [&](vk::descriptor_layout_builder& builder){
				builder.push_seq(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
			})
		, blit_descriptor_buffer_(allocator_usage_, blit_descriptor_layout_, blit_descriptor_layout_.binding_count())
		, blit_buffer_(allocator_usage_, {
				.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
				.size = sizeof(ubo_blit_config) + sizeof(VkDispatchIndirectCommand),
				.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
			}, {.usage = VMA_MEMORY_USAGE_GPU_ONLY})
		, command_seq_draw_(allocator_usage_.get_device(), create_info.command_pool, 4,
			VK_COMMAND_BUFFER_LEVEL_SECONDARY)
		, command_seq_blit_(allocator_usage_.get_device(), create_info.command_pool, 4,
			VK_COMMAND_BUFFER_LEVEL_SECONDARY){
		{
			(void)vk::descriptor_mapper{blit_descriptor_buffer_}.set_uniform_buffer(0, blit_buffer_.get_address(),
				sizeof(ubo_blit_config));
		}
		main_command_buffer = vk::command_buffer{allocator_usage_.get_device(), create_info.command_pool};
		blit_attachment_clear_and_init_command_buffer = vk::command_buffer{allocator_usage_.get_device(), create_info.command_pool, VK_COMMAND_BUFFER_LEVEL_SECONDARY};
		fence = {allocator_usage_.get_device(), true};

		for(const auto& cfg : attachment_manager.get_draw_config().attachments){
			color_attachment_formats.push_back(cfg.attachment.format);
		}

		cache_attachment_enter_mark_.resize(attachment_manager.get_draw_attachments().size());
		auto view = std::views::single(blit_descriptor_layout_.get());
		blit_pipeline_manager_ = compute_pipeline_manager(
			allocator_usage_,
			create_info.blit_pipe_config,
			std::views::repeat(view, create_info.blit_pipe_config.size()));


		auto sz = blit_pipeline_manager_.get_pipelines();
		blit_descriptors_.reserve(sz.size());
		for(std::size_t i = 0; i < sz.size(); ++i){
			auto& b = blit_descriptors_.emplace_back();
			b.buffer = vk::descriptor_buffer{allocator_usage_, blit_pipeline_manager_.get_inout_layouts()[i], blit_pipeline_manager_.get_inout_layouts()[i].binding_count()};
		}
	}

	void resize(VkExtent2D extent){
		attachment_manager.resize(extent);

		for(auto&& [blit_descriptor, pipe] :
		    std::views::zip(blit_descriptors_, blit_pipeline_manager_.get_pipelines())){
			const auto& option = pipe.option;
			vk::descriptor_mapper mapper{blit_descriptor.buffer};
			for (const auto & input_entry : option.inout.get_input_entries()){
				mapper.set_image(input_entry.binding, {
					.sampler = nullptr,
					.imageView = attachment_manager.get_draw_attachments()[input_entry.default_resource_index].get_image_view(),
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL
				}, 0, input_entry.type);
			}

			for (const auto & output_entry : option.inout.get_output_entries()){
				mapper.set_image(output_entry.binding, {
					.sampler = nullptr,
					.imageView = attachment_manager.get_blit_attachments()[output_entry.default_resource_index].get_image_view(),
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL
				}, 0, output_entry.type);
			}
		}

		create_blit_clear_and_init_cmd();
	}

	void create_command(){
		{
			fence.wait_and_reset();
			vk::scoped_recorder recorder{main_command_buffer, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
			record_cmd(recorder);
		}
	}

	VkCommandBuffer get_valid_cmd_buf() const noexcept{
		return main_command_buffer;
	}

	VkFence get_fence() const noexcept{
		return fence;
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
		batch.record_load_to_record_context(cache_record_context_);
		cache_record_context_.prepare_bindings();
		cache_record_context_(pipe_config.pipeline_layout, cmdbuf, 0, VK_PIPELINE_BIND_POINT_GRAPHICS);

		batch.record_command_draw(cmdbuf, index);
	}

	void record_cmd(VkCommandBuffer command_buffer){
		const auto cmd_sz = batch.get_submit_sections_count();

		vkCmdExecuteCommands(command_buffer, 1, blit_attachment_clear_and_init_command_buffer.as_data());

		auto submit_span = batch.get_valid_submit_groups();
		if(submit_span.empty()) [[unlikely]] {
			return;
		}

		const VkRect2D region = attachment_manager.get_screen_area();
		auto ctx = batch.record_command_set_up_non_vertex_buffer(command_buffer);

		std::ranges::fill(cache_attachment_enter_mark_, false);
		cache_draw_param_stack_.resize(1, {.pipeline_index = {}});
		configure_rendering_info(cache_draw_param_stack_.back());
		rendering_config.set_color_attachment_load_op(VK_ATTACHMENT_LOAD_OP_CLEAR);

		{
			const auto mask = get_current_target(cache_draw_param_stack_.back());
			for(std::size_t i = 0; i < std::min(mask.size(), attachment_manager.get_draw_attachments().size()); ++i){
				if(!mask[i])continue;
				cache_attachment_enter_mark_[i] = true;
			}
		}

		//TODO optimize empty submit group
		for(unsigned i = 0; i < cmd_sz; ++i){
			rendering_config.begin_rendering(command_buffer, region/*, VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT*/);

			create_pipe_binding_cmd(command_buffer, i, cache_draw_param_stack_.back());

			vkCmdEndRendering(command_buffer);

			bool requiresClear = false;
			auto& cfg = batch.get_break_config_at(i);
			requiresClear = cfg.clear_draw_after_break;
			for (const auto & e : cfg.get_entries()){
				requiresClear = process_breakpoints(e, command_buffer) || requiresClear;
			}

			batch.record_command_advance_non_vertex_buffer(ctx, command_buffer, i);
			if(requiresClear){
				std::ranges::fill(cache_attachment_enter_mark_, 0);
			}

			const auto mask = get_current_target(cache_draw_param_stack_.back());
			std::size_t curIdx{};
			for(std::size_t aIdx = 0; aIdx < std::min(mask.size(), attachment_manager.get_draw_attachments().size()); ++aIdx){
				if(!mask[aIdx])continue;
				const bool first_enter = !std::exchange(cache_attachment_enter_mark_[aIdx], true);
				rendering_config.get_color_attachment_infos()[curIdx].loadOp = first_enter ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
				++curIdx;
			}

		}

	}

	/**
	 *
	 * @return clear required
	 */
	bool process_breakpoints(const graphic::draw::instruction::breakpoint_entry& entry, VkCommandBuffer buffer) {
		switch(entry.flag){
		case gui::draw_state_index_deduce_v<gui::blit_config> :{
			auto cfg = entry.as<gui::blit_config>();
			if(cfg.blit_region.src.x < 0){
				cfg.blit_region.extent.x += cfg.blit_region.src.x;
				cfg.blit_region.src.x = 0;
				if(cfg.blit_region.extent.x < 0)cfg.blit_region.extent.x = 0;
			}
			if(cfg.blit_region.src.y < 0){
				cfg.blit_region.extent.y += cfg.blit_region.src.y;
				cfg.blit_region.src.y = 0;
				if(cfg.blit_region.extent.y < 0)cfg.blit_region.extent.y = 0;
			}

			const auto dispatches = (cfg.blit_region.extent.as<unsigned>() + math::usize2{15, 15}) / math::usize2{16, 16};
			std::byte buf[sizeof(ubo_blit_config) + sizeof(VkDispatchIndirectCommand)];
			std::construct_at(reinterpret_cast<ubo_blit_config*>(buf), ubo_blit_config{
				.offset = cfg.blit_region.src.as<unsigned>(),
			});
			std::construct_at(reinterpret_cast<VkDispatchIndirectCommand*>(buf + sizeof(ubo_blit_config)), VkDispatchIndirectCommand{
				dispatches.x, dispatches.y, 1
			});

			vk::cmd::dependency_gen dependency;
			dependency.push(blit_buffer_,
				VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
				VK_PIPELINE_STAGE_2_COPY_BIT,
				VK_ACCESS_2_TRANSFER_WRITE_BIT
				);

			dependency.apply(buffer, true);
			vkCmdUpdateBuffer(buffer, blit_buffer_, 0, sizeof(buf), buf);
			dependency.swap_stages();
			dependency.apply(buffer);

			for (const auto & draw_attachment : attachment_manager.get_draw_attachments()){
				dependency.push(draw_attachment.get_image(),
					VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
					VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
					VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
					VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					VK_IMAGE_LAYOUT_GENERAL,
					vk::image::default_image_subrange
				);
			}
			for (const auto & draw_attachment : attachment_manager.get_blit_attachments()){
				dependency.push(draw_attachment.get_image(),
					VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
					VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
					VK_IMAGE_LAYOUT_GENERAL,
					VK_IMAGE_LAYOUT_GENERAL,
					vk::image::default_image_subrange
				);
			}

			dependency.apply(buffer, true);
			dependency.image_memory_barriers.resize(attachment_manager.get_draw_attachments().size());
			dependency.swap_stages();

			auto& pipe_config = blit_pipeline_manager_.get_pipelines()[0];
			pipe_config.pipeline.bind(buffer, VK_PIPELINE_BIND_POINT_COMPUTE);

			cache_record_context_.clear();
			cache_record_context_.push(0, blit_descriptor_buffer_);
			cache_record_context_.push(1, blit_descriptors_[0].buffer);
			blit_pipeline_manager_.append_descriptor_buffers(cache_record_context_, 0);
			cache_record_context_.prepare_bindings();
			cache_record_context_(pipe_config.pipeline_layout, buffer, 0, VK_PIPELINE_BIND_POINT_COMPUTE);

			vkCmdDispatchIndirect(buffer, blit_buffer_, sizeof(ubo_blit_config));
			dependency.apply(buffer);

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
public:

	gui::renderer_frontend create_frontend() noexcept {
		using namespace graphic::draw::instruction;
		return gui::renderer_frontend{
			table, table_non_vertex, batch_backend_interface{
				*this,
				[](renderer_v2& b, std::span<const std::byte> data) static{
					return b.batch.push_instr(data);
				},
				[](renderer_v2& b) static{},
				[](renderer_v2& b) static{},
				[](renderer_v2& b, std::uint32_t flag, std::span<const std::byte> payload) static{
					b.batch.push_state(flag, payload);
				},
			}
		};
	}

	vk::image_handle get_base() const noexcept{
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

	std::bitset<32> get_current_target(const gui::draw_mode_param& param) const noexcept{
		const auto& pipes = draw_pipeline_manager_.get_pipelines()[param.pipeline_index].option;

		return param.draw_targets.any()
				   ? param.draw_targets
				   : pipes.is_partial_target()
				   ? pipes.default_target_attachments
				   : std::bitset<32>{~0U};
	}

	void configure_rendering_info(const gui::draw_mode_param& param){
		const auto& pipes = draw_pipeline_manager_.get_pipelines()[param.pipeline_index].option;
		attachment_manager.configure_dynamic_rendering(rendering_config,
			param.draw_targets.none() ? pipes.default_target_attachments : param.draw_targets, pipes.enables_multisample && attachment_manager.enables_multisample());


	}
};

}
