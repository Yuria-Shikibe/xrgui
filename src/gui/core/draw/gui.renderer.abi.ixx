module;

#include <vulkan/vulkan.h>

export module mo_yanxi.gui.renderer.abi;

export import mo_yanxi.gui.fx.config;

import mo_yanxi.graphic.color;
import mo_yanxi.math;
import mo_yanxi.math.matrix3;
import mo_yanxi.math.rect_ortho;
import mo_yanxi.math.vector2;
import mo_yanxi.vk.util.uniform;
import std;

namespace mo_yanxi::gui{

export
struct scissor_gpu_{
	math::vec2 src{};
	math::vec2 dst{};

	//TODO margin is never used
	float margin{};

	std::uint32_t cap[3];
	constexpr friend bool operator==(const scissor_gpu_& lhs, const scissor_gpu_& rhs) noexcept = default;
};

export
struct scissor_raw_{
	math::frect rect{};
	float margin{};

	[[nodiscard]] scissor_raw_ intersection_with(const scissor_raw_& other) const noexcept{
		return {rect.intersection_with(other.rect), margin};
	}

	void uniform(const math::mat3& mat) noexcept{
		if(rect.area() < 0.05f){
			rect = {};
			return;
		}

		auto src = rect.get_src();
		auto dst = rect.get_end();
		src *= mat;
		dst *= mat;

		rect = {tags::from_vertex, src, dst};
	}

	constexpr friend bool operator==(const scissor_raw_& lhs, const scissor_raw_& rhs) noexcept = default;

	constexpr explicit(false) operator scissor_gpu_() const noexcept{
		return scissor_gpu_{rect.get_src(), rect.get_end(), margin};
	}
};

export
struct alignas(16) ubo_screen_info{
	using tag_vertex_only = void;
	vk::padded_mat3 screen_to_uniform;
};

export
struct alignas(16) ubo_layer_info{
	using tag_vertex_only = void;
	vk::padded_mat3 element_to_screen;
	scissor_gpu_ scissor;
};

export
struct alignas(32) accumulated_state{
	using tag_vertex_only = void;

	graphic::color overlay_color;
	graphic::color base_mult;
};

export
using gui_reserved_user_data_tuple = std::tuple<ubo_screen_info, ubo_layer_info, accumulated_state>;

}
