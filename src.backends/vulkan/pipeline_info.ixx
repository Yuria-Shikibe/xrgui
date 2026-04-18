module;

#include <vulkan/vulkan.h>

export module mo_yanxi.backend.vulkan.pipeline_info;

import std;
import mo_yanxi.vk.vertex_consteval_def;


namespace mo_yanxi::backend::vulkan{

export struct gui_vertex_mock{
	float position[3];
	std::uint32_t timeline_index;
	float color[4];
	float uv[2];
	std::uint32_t _cap[2];
};

export struct gui_primitive_mock{
	std::uint32_t texture_index;
	std::uint32_t draw_mode;
	float sdf_expand;
};

export constexpr inline VkPipelineVertexInputStateCreateInfo vertex_def_desc = vk::make_vertex_input_state<
	vk::vertex_attribute{&gui_vertex_mock::position, VK_FORMAT_R32G32B32_SFLOAT},
	vk::vertex_attribute{&gui_vertex_mock::timeline_index, VK_FORMAT_R32_UINT},
	vk::vertex_attribute{&gui_vertex_mock::color, VK_FORMAT_R32G32B32A32_SFLOAT},
	vk::vertex_attribute{&gui_vertex_mock::uv, VK_FORMAT_R32G32_SFLOAT}
>();

}
