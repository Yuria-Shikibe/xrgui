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

using nine_patch_coord_fn_mtpr_type = decltype(&image_nine_region::get_row_coords);

export
template <nine_patch_coord_fn_mtpr_type getter = &image_nine_region::get_row_coords>
struct nine_patch_draw{
	const image_nine_region* patch;
	math::raw_frect region;
	graphic::color color;

	template <std::invocable<graphic::g2d::row_patch&&> Fn>
	FORCE_INLINE constexpr void for_each(Fn fn) const noexcept{
		assert(patch != nullptr);
		auto uvs = patch->get_row_uvs();
		auto coords = std::invoke(getter, patch, region);
		for(int i = 0; i < 3; ++i){
			std::invoke(fn, graphic::g2d::row_patch{
				.generic = {.image = patch->texture_binding()},
				.coords = coords[i],
				.uvs = uvs[i],
				.vert_color = {color}
			});
		}
	}

	FORCE_INLINE void operator()(graphic::g2d::emit_t emit, auto& sink) const {
		for_each([&] FORCE_INLINE (graphic::g2d::row_patch&& patch){
			emit(sink, patch);
		});
	}
};

export
template <nine_patch_coord_fn_mtpr_type getter = &image_nine_region::get_row_coords>
struct nine_patch_hollow_draw{
	const image_nine_region* patch;
	math::raw_frect region;
	graphic::color color;

	template <typename Fn>
	FORCE_INLINE constexpr void for_each(Fn fn) const noexcept{
		assert(patch != nullptr);
		auto uvs = patch->get_row_uvs();
		auto coords = std::invoke(getter, patch, region);

		std::invoke(fn, graphic::g2d::row_patch{
			.generic = {.image = patch->texture_binding()},
			.coords = coords[0],
			.uvs = uvs[0],
			.vert_color = {color}
		});

		std::invoke(fn, graphic::g2d::rect_aabb{
			.generic = {.image = patch->texture_binding()},
			.v00 = {coords[1][0], coords[1][4]},
			.v11 = {coords[1][1], coords[1][5]},
			.uv00 = {uvs[1][2], uvs[1][0]},
			.uv11 = {uvs[1][3], uvs[1][1]},
			.vert_color = {color}
		});

		std::invoke(fn, graphic::g2d::rect_aabb{
			.generic = {.image = patch->texture_binding()},
			.v00 = {coords[1][2], coords[1][4]},
			.v11 = {coords[1][3], coords[1][5]},
			.uv00 = {uvs[1][4], uvs[1][0]},
			.uv11 = {uvs[1][5], uvs[1][1]},
			.vert_color = {color}
		});

		std::invoke(fn, graphic::g2d::row_patch{
			.generic = {.image = patch->texture_binding()},
			.coords = coords[2],
			.uvs = uvs[2],
			.vert_color = {color}
		});
	}

	FORCE_INLINE void operator()(graphic::g2d::emit_t emit, auto& sink) const {
		for_each([&]<typename Instr> FORCE_INLINE (Instr&& patch){
			emit(sink, std::forward<Instr>(patch));
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
		auto coords = img_patch.get_row_coords(region);
		auto uvs = img_patch.get_row_uvs();

		auto [CLB, CLT] = img_patch.interpolate_middle_row_values(color[0], color[2], region.extent.y);
		auto [CRB, CRT] = img_patch.interpolate_middle_row_values(color[1], color[3], region.extent.y);

		emit(sink, graphic::g2d::row_patch{
			.generic = {.image = img_patch.texture_binding()},
			.coords = coords[0],
			.uvs = uvs[0],
			.vert_color = graphic::g2d::quad_vert_color{color[0], color[1], CLB, CRB}
		});
		emit(sink, graphic::g2d::row_patch{
			.generic = {.image = img_patch.texture_binding()},
			.coords = coords[1],
			.uvs = uvs[1],
			.vert_color = graphic::g2d::quad_vert_color{CLB, CRB, CLT, CRT}
		});
		emit(sink, graphic::g2d::row_patch{
			.generic = {.image = img_patch.texture_binding()},
			.coords = coords[2],
			.uvs = uvs[2],
			.vert_color = graphic::g2d::quad_vert_color{CLT, CRT, color[2], color[3]}
		});
	}
};

}
