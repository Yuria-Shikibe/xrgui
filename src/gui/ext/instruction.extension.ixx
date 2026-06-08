module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.fx.instruction_extension;

import std;

export import mo_yanxi.gui.fx;
export import mo_yanxi.gui.renderer.frontend;
export import mo_yanxi.gui.image_regions;
export import mo_yanxi.graphic.g2d;
export import mo_yanxi.graphic.color;

namespace mo_yanxi::gui::fx{

export
struct circle{
	math::vec2 pos;
	math::range radius;
	math::section<graphic::float4> color;

	explicit(false) operator graphic::g2d::poly() const noexcept{
	 	return {
	 		.pos = pos,
			 .segments = (std::uint32_t)get_smooth_circle_vertex_count(radius.abs_max(), 1),
			 .radius = radius,
			 .color = color
	 	 };
	 }

	FORCE_INLINE void operator()(graphic::g2d::emit_t emit, auto& sink) const {
		emit(sink, graphic::g2d::poly(*this));
	}
};

export
struct row_patch_draw{
	const image_row_patch* patch;
	math::raw_frect region;
	graphic::color color;
	graphic::g2d::row_patch_flags flags;

	FORCE_INLINE void operator()(graphic::g2d::emit_t emit, auto& sink) const {
		using namespace graphic::g2d;
		assert(patch != nullptr);
		emit(sink, row_patch{
				.generic = {.image = patch->texture_binding()},
				.coords = (flags & row_patch_flags::transposed) == row_patch_flags{}
					          ? patch->get_ortho_draw_coords_axis_scaled(region)
					          : patch->get_ortho_draw_coords_axis_scaled_transsrced(region),
				.uvs = patch->get_uvs(),
				.vert_color = {color},
				.flags = flags
			});
	}
};

using nine_patch_coord_fn_mtpr_type = decltype(&image_nine_region::get_axes);

[[nodiscard]] FORCE_INLINE constexpr graphic::g2d::nine_patch make_nine_patch_instruction(
	const image_nine_region& patch,
	const nine_patch_draw_axes& axes,
	const graphic::g2d::quad_vert_color& color,
	const graphic::g2d::nine_patch_flags flags = {}) noexcept{
	return {
		.generic = {.image = patch.texture_binding()},
		.x = axes.x,
		.y = axes.y,
		.uvx = axes.uvx,
		.uvy = axes.uvy,
		.vert_color = color,
		.flags = flags
	};
}

export
template <nine_patch_coord_fn_mtpr_type getter = &image_nine_region::get_axes>
struct nine_patch_draw{
	const image_nine_region* patch;
	math::raw_frect region;
	graphic::color color;

	template <std::invocable<graphic::g2d::nine_patch&&> Fn>
	FORCE_INLINE constexpr void for_each(Fn fn) const noexcept{
		assert(patch != nullptr);
		auto axes = std::invoke(getter, patch, region);
		std::invoke(fn, make_nine_patch_instruction(*patch, axes, {color}));
	}

	FORCE_INLINE void operator()(graphic::g2d::emit_t emit, auto& sink) const {
		for_each([&] FORCE_INLINE (graphic::g2d::nine_patch&& patch){
			emit(sink, patch);
		});
	}
};

export
template <nine_patch_coord_fn_mtpr_type getter = &image_nine_region::get_axes>
struct nine_patch_hollow_draw{
	const image_nine_region* patch;
	math::raw_frect region;
	graphic::color color;

	template <std::invocable<graphic::g2d::nine_patch&&> Fn>
	FORCE_INLINE constexpr void for_each(Fn fn) const noexcept{
		assert(patch != nullptr);
		auto axes = std::invoke(getter, patch, region);

		std::invoke(fn, make_nine_patch_instruction(
			*patch, axes, {color}, graphic::g2d::nine_patch_flags::hollow));
	}

	FORCE_INLINE void operator()(graphic::g2d::emit_t emit, auto& sink) const {
		for_each([&] FORCE_INLINE (graphic::g2d::nine_patch&& patch){
			emit(sink, patch);
		});
	}
};


export
struct nine_patch_draw_vert_color{
	const image_nine_region* patch;
	math::raw_frect region;
	graphic::g2d::quad_vert_color color;

	FORCE_INLINE void operator()(graphic::g2d::emit_t emit, auto& sink) const {
		assert(patch != nullptr);
		auto& img_patch = *patch;
		auto axes = img_patch.get_axes(region);

		emit(sink, make_nine_patch_instruction(img_patch, axes, color));
	}
};

}
