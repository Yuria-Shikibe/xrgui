module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.fx.instruction_extension;

import std;

export import mo_yanxi.gui.fx;
export import mo_yanxi.gui.renderer.frontend;
export import mo_yanxi.gui.image_regions;
export import mo_yanxi.graphic.draw.instruction;
export import mo_yanxi.graphic.color;

namespace mo_yanxi::gui::fx{
export
struct circle{
	math::vec2 pos;
	math::range radius;
	math::section<graphic::float4> color;

	explicit(false) operator graphic::draw::instruction::poly() const noexcept{
	 	return {
	 		.pos = pos,
			 .segments = (std::uint32_t)get_smooth_circle_vertex_count(radius.abs_max(), 1),
			 .radius = radius,
			 .color = color
		 };
	 }

	FORCE_INLINE friend renderer_frontend& operator<<(renderer_frontend& renderer, const circle& instr) {
		using namespace graphic::draw::instruction;
		renderer.push(poly(instr));

		return renderer;
	}
};

export
struct row_patch_draw{
	const image_row_patch* patch;
	math::raw_frect region;
	graphic::color color;
	graphic::draw::instruction::row_patch_flags flags;

	FORCE_INLINE friend renderer_frontend& operator<<(renderer_frontend& renderer, const row_patch_draw& instr) {
		using namespace graphic::draw::instruction;
		assert(instr.patch != nullptr);
		renderer.push(row_patch{
				.generic = {.image = instr.patch->get_image_view()},
				.coords = (instr.flags & row_patch_flags::transposed) == row_patch_flags{}
					          ? instr.patch->get_ortho_draw_coords_axis_scaled(instr.region)
					          : instr.patch->get_ortho_draw_coords_axis_scaled_transsrced(instr.region),
				.uvs = instr.patch->get_uvs(),
				.vert_color = {instr.color},
				.flags = instr.flags
			});

		return renderer;
	}
};

using nine_patch_coord_fn_mtpr_type = decltype(&image_nine_region::get_row_coords);

export
template <nine_patch_coord_fn_mtpr_type getter = &image_nine_region::get_row_coords>
struct nine_patch_draw{
	const image_nine_region* patch;
	math::raw_frect region;
	graphic::color color;

	template <std::invocable<graphic::draw::instruction::row_patch&&> Fn>
	FORCE_INLINE constexpr void for_each(Fn fn) const noexcept{
		assert(patch != nullptr);
		auto uvs = patch->get_row_uvs();
		auto coords = std::invoke(getter, patch, region);
		for(int i = 0; i < 3; ++i){
			std::invoke(fn, graphic::draw::instruction::row_patch{
				.generic = {.image = patch->image_view->view},
				.coords = coords[i],
				.uvs = uvs[i],
				.vert_color = {color}
			});
		}
	}

	FORCE_INLINE friend renderer_frontend& operator<<(renderer_frontend& renderer, const nine_patch_draw& instr) {
		instr.for_each([&] FORCE_INLINE (graphic::draw::instruction::row_patch&& patch){
			renderer.push(patch);
		});
		return renderer;
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

		std::invoke(fn, graphic::draw::instruction::row_patch{
			.generic = {.image = patch->image_view->view},
			.coords = coords[0],
			.uvs = uvs[0],
			.vert_color = {color}
		});

		std::invoke(fn, graphic::draw::instruction::rect_aabb{
			.generic = {.image = patch->image_view->view},
			.v00 = {coords[1][0], coords[1][4]},
			.v11 = {coords[1][1], coords[1][5]},
			.uv00 = {uvs[1][2], uvs[1][0]},
			.uv11 = {uvs[1][3], uvs[1][1]},
			.vert_color = {color}
		});

		std::invoke(fn, graphic::draw::instruction::rect_aabb{
			.generic = {.image = patch->image_view->view},
			.v00 = {coords[1][2], coords[1][4]},
			.v11 = {coords[1][3], coords[1][5]},
			.uv00 = {uvs[1][4], uvs[1][0]},
			.uv11 = {uvs[1][5], uvs[1][1]},
			.vert_color = {color}
		});

		std::invoke(fn, graphic::draw::instruction::row_patch{
			.generic = {.image = patch->image_view->view},
			.coords = coords[2],
			.uvs = uvs[2],
			.vert_color = {color}
		});
	}

	FORCE_INLINE friend renderer_frontend& operator<<(renderer_frontend& renderer, const nine_patch_hollow_draw& instr) {
		instr.for_each([&]<typename Instr> FORCE_INLINE (Instr&& patch){
			renderer.push(std::forward<Instr>(patch));
		});
		return renderer;
	}
};


export
struct nine_patch_draw_vert_color{
	const image_nine_region* patch;
	math::raw_frect region;
	graphic::draw::instruction::quad_vert_color color;

	FORCE_INLINE friend renderer_frontend& operator<<(renderer_frontend& renderer, const nine_patch_draw_vert_color& instr) {
		assert(instr.patch != nullptr);
		auto& patch = *instr.patch;
		auto coords = patch.get_row_coords(instr.region);
		auto uvs = patch.get_row_uvs();

		auto [CLB, CLT] = patch.interpolate_middle_row_values(instr.color.v00, instr.color.v01, instr.region.extent.y);
		auto [CRB, CRT] = patch.interpolate_middle_row_values(instr.color.v10, instr.color.v11, instr.region.extent.y);

		renderer.push(graphic::draw::instruction::row_patch{
			.generic = {.image = patch.image_view->view},
			.coords = coords[0],
			.uvs = uvs[0],
			.vert_color = graphic::draw::instruction::quad_vert_color{instr.color.v00, instr.color.v10, CLB, CRB}
		});
		renderer.push(graphic::draw::instruction::row_patch{
			.generic = {.image = patch.image_view->view},
			.coords = coords[1],
			.uvs = uvs[1],
			.vert_color = graphic::draw::instruction::quad_vert_color{CLB, CRB, CLT, CRT}
		});
		renderer.push(graphic::draw::instruction::row_patch{
			.generic = {.image = patch.image_view->view},
			.coords = coords[2],
			.uvs = uvs[2],
			.vert_color = graphic::draw::instruction::quad_vert_color{CLT, CRT, instr.color.v01, instr.color.v11}
		});
		return renderer;
	}
};

}
