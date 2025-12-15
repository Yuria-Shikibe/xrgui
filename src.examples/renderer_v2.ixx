module;

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

export module renderer_v2;

export import instruction_draw.batch.v2;
import mo_yanxi.vk;
import mo_yanxi.vk.cmd;
import mo_yanxi.vk.util;
import mo_yanxi.gui.renderer.frontend;
import mo_yanxi.gui.draw_config;
import mo_yanxi.backend.vulkan.renderer.attachment_manager;
import std;

namespace mo_yanxi::backend::vulkan{
const graphic::draw::instruction::user_data_index_table table{
	std::in_place_type<gui::gui_reserved_user_data_tuple>
};

const graphic::draw::instruction::user_data_index_table table_non_vertex{
	std::in_place_type<std::tuple<gui::draw_config::ui_state, gui::draw_config::slide_line_config>>
};

struct ubo_blit_config{
	math::upoint2 offset;
	math::upoint2 _cap;
};

export
struct renderer_v2{
	vk::allocator_usage allocator_usage{};

	graphic::draw::instruction::batch_v2 batch{};


	backend::vulkan::attachment_manager attachment_manager{};


	vk::command_seq<> command_seq_draw_{};
	vk::command_seq<> command_seq_blit_{};


	vk::fence fence{};
	vk::command_buffer main_command_buffer{};

	vk::pipeline_layout pipeline_layout;
	vk::pipeline pipeline;

	vk::descriptor_layout blit_descriptor_layout_;
	vk::descriptor_buffer blit_descriptor_buffer_;
	vk::buffer blit_buffer_;

	vk::pipeline_layout blit_pipeline_layout_;
	vk::pipeline blit_pipeline_;

	vk::dynamic_rendering rendering_config{};
	std::vector<VkFormat> color_attachment_formats{};

	[[nodiscard]] explicit renderer_v2(
		const vk::allocator_usage& allocator_usage,
		VkCommandPool command_pool,
		VkSampler sampler
		)
		: allocator_usage(allocator_usage)
		, batch(allocator_usage, {}, table, table_non_vertex, sampler)
		, attachment_manager{allocator_usage, {
			{
				draw_attachment_config{.attachment = {VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT}},
				draw_attachment_config{.attachment = {VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT}},
			}
			}, {
				{
					attachment_config{VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_SRC_BIT},
					attachment_config{VK_FORMAT_R8G8B8A8_UNORM},
				}
			}}
		, command_seq_draw_(allocator_usage.get_device(), command_pool, 4, VK_COMMAND_BUFFER_LEVEL_SECONDARY)
		, command_seq_blit_(allocator_usage.get_device(), command_pool, 4, VK_COMMAND_BUFFER_LEVEL_SECONDARY)
		, blit_descriptor_layout_(allocator_usage.get_device(), VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT, [&](vk::descriptor_layout_builder& builder){
			builder.push_seq(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);

			for (const auto & attachment : attachment_manager.get_draw_config().attachments){
				builder.push_seq(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT);
			}

			for (const auto & attachment : attachment_manager.get_blit_config().attachments){
				builder.push_seq(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT);
			}
		})
		, blit_descriptor_buffer_(allocator_usage, blit_descriptor_layout_, blit_descriptor_layout_.binding_count())
		, blit_buffer_(allocator_usage, {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = sizeof(ubo_blit_config) + sizeof(VkDispatchIndirectCommand),
			.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
		}, {.usage = VMA_MEMORY_USAGE_GPU_ONLY})
	{
		{
			(void)vk::descriptor_mapper{blit_descriptor_buffer_}.set_uniform_buffer(0, blit_buffer_.get_address(), sizeof(ubo_blit_config));
		}
		main_command_buffer = vk::command_buffer{allocator_usage.get_device(), command_pool};
		fence = {allocator_usage.get_device(), true};
		create_pipe();

		rendering_config = attachment_manager.get_dynamic_rendering(false);
		for (const auto & cfg : attachment_manager.get_draw_config().attachments){
			color_attachment_formats.push_back(cfg.attachment.format);
		}
	}

	void resize(VkExtent2D extent){
		attachment_manager.resize(extent);
		rendering_config = attachment_manager.get_dynamic_rendering(false);
		vk::descriptor_mapper mapper{blit_descriptor_buffer_};

		for (const auto & [idx, draw_attachment] : attachment_manager.get_draw_attachments() | std::views::enumerate){
			mapper.set_storage_image(1 + idx, draw_attachment.get_image_view());
		}

		for (const auto & [idx, draw_attachment] : attachment_manager.get_blit_attachments() | std::views::enumerate){
			mapper.set_storage_image(1 + attachment_manager.get_draw_attachments().size() + idx, draw_attachment.get_image_view());
		}


	}

	void create_pipe_binding_cmd(std::uint32_t index){
		VkCommandBufferInheritanceRenderingInfo inheritanceRenderingInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO,
			.colorAttachmentCount = static_cast<std::uint32_t>(color_attachment_formats.size()),
			.pColorAttachmentFormats = color_attachment_formats.data(),
			.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
		};

		const vk::scoped_recorder recorder{command_seq_draw_[index], VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT, inheritanceRenderingInfo};

		pipeline.bind(recorder, VK_PIPELINE_BIND_POINT_GRAPHICS);

		const VkRect2D region = attachment_manager.get_screen_area();

		vk::cmd::set_viewport(recorder, region);
		vk::cmd::set_scissor(recorder, region);
		batch.record_command_bind_and_draw(pipeline_layout, recorder, index);
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
	void record_cmd(VkCommandBuffer command_buffer){
		const auto cmd_sz = batch.get_submit_sections_count();
		if(command_seq_draw_.size() < cmd_sz){
			command_seq_draw_.reset(cmd_sz, VK_COMMAND_BUFFER_LEVEL_SECONDARY);
		}
		for(unsigned i = 0; i < cmd_sz; ++i){
			create_pipe_binding_cmd(i);
		}

		vk::cmd::dependency_gen dependency;
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

		for (const auto & draw_attachment : attachment_manager.get_blit_attachments()){
			dependency.push(draw_attachment.get_image(),
				VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
				VK_ACCESS_2_NONE,
				VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_GENERAL,
				vk::image::default_image_subrange
			);
		}

		dependency.apply(command_buffer);

		const VkRect2D region = attachment_manager.get_screen_area();

		rendering_config.set_color_attachment_load_op(VK_ATTACHMENT_LOAD_OP_CLEAR);
		auto ctx = batch.record_command_set_up_non_vertex_buffer(command_buffer);
		for(unsigned i = 0; i < cmd_sz; ++i){
			//TODO check if there are actual dispatch...
			bool requiresClear = false;
			if(i + 1 < cmd_sz){
				auto& cfg = batch.get_break_config_at(i);
				batch.record_command_advance_non_vertex_buffer(ctx, command_buffer, i);

				requiresClear = cfg.clear_draw_after_break;
				for (const auto & e : cfg.get_entries()){
					requiresClear = process_breakpoints(e, command_buffer) || requiresClear;
				}

			}

			rendering_config.begin_rendering(command_buffer, region, VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT);
			vkCmdExecuteCommands(command_buffer, 1, std::to_address(command_seq_draw_.begin()) + i);
			vkCmdEndRendering(command_buffer);

			rendering_config.set_color_attachment_load_op(requiresClear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD);
		}

	}

	/**
	 *
	 * @return clear required
	 */
	bool process_breakpoints(const graphic::draw::instruction::breakpoint_entry& entry, VkCommandBuffer buffer) const{
		switch(entry.flag){
		case gui::draw_state_index_deduce_v<gui::blit_config> :{
			auto& data = entry.as<gui::blit_config>();
			const auto dispatches = (data.blit_region.extent + math::usize2{15, 15}) / math::usize2{16, 16};
			std::byte buf[sizeof(ubo_blit_config) + sizeof(VkDispatchIndirectCommand)];
			std::construct_at(reinterpret_cast<ubo_blit_config*>(buf), ubo_blit_config{
				.offset = data.blit_region.src,
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

			blit_pipeline_.bind(buffer, VK_PIPELINE_BIND_POINT_COMPUTE);
			blit_descriptor_buffer_.bind_to(buffer, VK_PIPELINE_BIND_POINT_COMPUTE, blit_pipeline_layout_, 0);
			vkCmdDispatchIndirect(buffer, blit_buffer_, sizeof(ubo_blit_config));
			dependency.apply(buffer);

			return true;
		}
		case gui::draw_state_index_deduce_v<gui::draw_mode_param> :{
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

	void create_pipe(){

		const auto shader_spv_path = std::filesystem::current_path().append("assets/shader/spv").make_preferred();
		vk::shader_module shader_draw{allocator_usage.get_device(), shader_spv_path / "ui.draw_v2.spv"};
		vk::shader_module shader_blit{allocator_usage.get_device(), shader_spv_path / "ui.blit.basic.spv"};

		{
			vk::graphic_pipeline_template gtp{};
			gtp.set_shaders({
				shader_draw.get_create_info(VK_SHADER_STAGE_MESH_BIT_EXT, "main_mesh"),
				shader_draw.get_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, "main_frag"),
			});
			gtp.push_color_attachment_format(VK_FORMAT_R8G8B8A8_UNORM, vk::blending::alpha_blend);
			gtp.push_color_attachment_format(VK_FORMAT_R8G8B8A8_UNORM, vk::blending::alpha_blend);

			const auto [set0, set1] = batch.get_descriptor_set_layout();

			pipeline_layout = vk::pipeline_layout{allocator_usage.get_device(), 0, {set0, set1}};
			pipeline = vk::pipeline{
				allocator_usage.get_device(), pipeline_layout,
				VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
				gtp
			};
		}

		{
			blit_pipeline_layout_ = {allocator_usage.get_device(), 0, {blit_descriptor_layout_}};
			blit_pipeline_ = vk::pipeline{
					allocator_usage.get_device(), blit_pipeline_layout_, VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
					shader_blit.get_create_info(VK_SHADER_STAGE_COMPUTE_BIT)
				};
		}
	}

	vk::image_handle get_base() const noexcept{
		return attachment_manager.get_blit_attachments()[0];
	}
};

}
