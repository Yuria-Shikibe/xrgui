module;

#include <vulkan/vulkan.h>

export module renderer_v2;

export import instruction_draw.batch.v2;
import mo_yanxi.vk;
import mo_yanxi.vk.cmd;
import mo_yanxi.vk.util;
import mo_yanxi.gui.renderer.frontend;
import mo_yanxi.gui.draw_config;
import std;

namespace mo_yanxi::graphic{
const graphic::draw::instruction::user_data_index_table table{
	std::in_place_type<gui::gui_reserved_user_data_tuple>
};

const graphic::draw::instruction::user_data_index_table table_non_vertex{
	std::in_place_type<std::tuple<gui::draw_config::ui_state, gui::draw_config::slide_line_config>>
};

export
struct renderer_v2{
	vk::allocator_usage allocator_usage{};

	draw::instruction::batch_v2 batch{};

	vk::color_attachment attachment_base{};
	// vk::color_attachment attachment_back{};

	vk::command_seq<> command_seq_pipelines_{};

	vk::fence fence{};
	vk::command_buffer main_command_buffer{};

	vk::pipeline_layout pipeline_layout;
	vk::pipeline pipeline;

	[[nodiscard]] explicit renderer_v2(
		const vk::allocator_usage& allocator_usage,
		VkCommandPool command_pool,
		VkSampler sampler
		)
		: allocator_usage(allocator_usage)
		, batch(allocator_usage, {}, table, table_non_vertex, sampler)
		, attachment_base(allocator_usage, {4, 4}, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
		, command_seq_pipelines_(allocator_usage.get_device(), command_pool, 4, VK_COMMAND_BUFFER_LEVEL_SECONDARY)
	{
		main_command_buffer = vk::command_buffer{allocator_usage.get_device(), command_pool};
		fence = {allocator_usage.get_device(), true};
		create_pipe();
	}

	void resize(VkExtent2D extent){
		attachment_base.resize(extent);
	}

	void create_pipe_binding_cmd(std::uint32_t index){
		VkCommandBufferInheritanceRenderingInfo inheritanceRenderingInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO};
		inheritanceRenderingInfo.colorAttachmentCount = 1;
		VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
		inheritanceRenderingInfo.pColorAttachmentFormats = &colorFormat;
		inheritanceRenderingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		vk::scoped_recorder recorder{command_seq_pipelines_[index], VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT, inheritanceRenderingInfo};

		pipeline.bind(recorder, VK_PIPELINE_BIND_POINT_GRAPHICS);

		VkRect2D region = {{}, attachment_base.get_image().get_extent2()};

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
		if(command_seq_pipelines_.size() < cmd_sz){
			command_seq_pipelines_.reset(cmd_sz, VK_COMMAND_BUFFER_LEVEL_SECONDARY);
		}
		for(unsigned i = 0; i < cmd_sz; ++i){
			create_pipe_binding_cmd(i);
		}


		vk::dynamic_rendering dynamic_rendering{};
		dynamic_rendering.push_color_attachment(
			attachment_base.get_image_view(),
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_ATTACHMENT_LOAD_OP_CLEAR,
			VK_ATTACHMENT_STORE_OP_STORE
		);


		vk::cmd::dependency_gen dependency;
		dependency.push(attachment_base.get_image(),
			VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
			VK_ACCESS_2_NONE,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			vk::image::default_image_subrange
		);
		dependency.apply(command_buffer);

		VkRect2D region = {{}, attachment_base.get_image().get_extent2()};

		auto ctx = batch.record_command_set_up_non_vertex_buffer(command_buffer);
		for(unsigned i = 0; i < cmd_sz; ++i){
			dynamic_rendering.begin_rendering(command_buffer, region, VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT);
			vkCmdExecuteCommands(command_buffer, 1, std::to_address(command_seq_pipelines_.begin()) + i);
			vkCmdEndRendering(command_buffer);

			batch.record_command_advance_non_vertex_buffer(ctx, command_buffer, i);

			//TODO decide if clean
			// if()

			dynamic_rendering.set_color_attachment_load_op(VK_ATTACHMENT_LOAD_OP_LOAD);
		}

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
				{}
			}
		};
	}

	void create_pipe(){
		const auto shader_spv_path = std::filesystem::current_path().append("assets/shader/spv").make_preferred();
		vk::shader_module shader_module{allocator_usage.get_device(), shader_spv_path / "ui.draw_v2.spv"};
		vk::graphic_pipeline_template gtp{};
		gtp.set_shaders({
			shader_module.get_create_info(VK_SHADER_STAGE_MESH_BIT_EXT, "main_mesh"),
			shader_module.get_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, "main_frag"),
		});
		gtp.push_color_attachment_format(VK_FORMAT_R8G8B8A8_UNORM, vk::blending::alpha_blend);

		const auto [set0, set1] = batch.get_descriptor_set_layout();

		pipeline_layout = vk::pipeline_layout{allocator_usage.get_device(), 0, {set0, set1}};
		pipeline = vk::pipeline{
			allocator_usage.get_device(), pipeline_layout,
			VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
			gtp
		};
	}

	vk::image_handle get_base() const noexcept{
		return attachment_base;
	}
};

}