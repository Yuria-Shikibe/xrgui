module;

#include <vulkan/vulkan.h>

export module renderer_v2;

export import instruction_draw.batch.v2;
import mo_yanxi.vk;
import mo_yanxi.vk.cmd;
import mo_yanxi.vk.util;
import mo_yanxi.gui.renderer.frontend;
import std;

namespace mo_yanxi::graphic{
const graphic::draw::instruction::user_data_index_table table{
	std::in_place_type<gui::gui_reserved_user_data_tuple>
};
export
struct renderer_v2{
	vk::allocator_usage allocator_usage{};

	draw::instruction::batch_v2 batch{};

	vk::color_attachment attachment_base{};
	// vk::color_attachment attachment_back{};

	vk::pipeline_layout pipeline_layout;
	vk::pipeline pipeline;
	vk::command_buffer command_buffer;

	[[nodiscard]] explicit renderer_v2(
		const vk::allocator_usage& allocator_usage
		)
		: allocator_usage(allocator_usage)
		, batch(allocator_usage, {}, table, nullptr)
	{
		create_pipe();
	}

	void resize(VkExtent2D extent){
		attachment_base.resize(extent);
	}

	void record_cmd(){
		vk::dynamic_rendering dynamic_rendering{};
		dynamic_rendering.push_color_attachment(
			attachment_base.get_image_view(),
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_ATTACHMENT_LOAD_OP_CLEAR,
			VK_ATTACHMENT_STORE_OP_STORE
		);

		vk::scoped_recorder recorder{command_buffer, VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT};

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

		dynamic_rendering.begin_rendering(command_buffer, {{}, attachment_base.get_image().get_extent2()});
		batch.record_command(pipeline_layout, command_buffer);
		vkCmdEndRendering(command_buffer);
	}

	// gui::renderer_frontend create_frontend() noexcept {
	// 	using namespace graphic::draw::instruction;
	// 	return gui::renderer_frontend{
	// 		table, batch_backend_interface{
	// 			*this,
	// 			[](renderer_v2& b, std::size_t size) static{
	// 				return b.batch.acquire(size);
	// 			},
	// 			[](renderer_v2& b) static{
	// 				b.batch_.consume_all();
	// 			},
	// 			[](renderer_v2& b) static{
	// 				b.batch_.wait_all();
	// 			},
	// 			state_handlers
	// 		}
	// 	};
	// }

	void create_pipe(){
		const auto shader_spv_path = std::filesystem::current_path().append("assets/shader/spv").make_preferred();
		vk::shader_module shader_module{allocator_usage.get_device(), shader_spv_path / "ui.draw_v2.spv"};
		vk::graphic_pipeline_template gtp{};
		gtp.set_shaders({
			shader_module.get_create_info(VK_SHADER_STAGE_MESH_BIT_EXT, "main_mesh"),
			shader_module.get_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, "main_frag"),
		});
		gtp.push_color_attachment_format(VK_FORMAT_R8G8B8A8_UNORM, vk::blending::alpha_blend);

		pipeline_layout = vk::pipeline_layout{allocator_usage.get_device(), 0, {batch.get_descriptor_set_layout()}};
		pipeline = vk::pipeline{
			allocator_usage.get_device(), pipeline_layout,
			VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
			gtp
		};
	}
};

}