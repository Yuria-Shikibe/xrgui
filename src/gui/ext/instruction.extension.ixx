module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.fx.instruction_extension;

export import mo_yanxi.gui.renderer.frontend;
export import mo_yanxi.gui.image_regions;
export import mo_yanxi.graphic.draw.instruction;
export import mo_yanxi.graphic.color;

namespace mo_yanxi::gui::fx{
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
					          ? instr.patch->get_ortho_draw_coords(instr.region)
					          : instr.patch->get_ortho_draw_coords_transposed(instr.region),
				.uvs = instr.patch->get_uvs(),
				.vert_color = {instr.color},
				.flags = instr.flags
			});
		
		return renderer;
	}
};

export
struct nine_patch_draw{
	const image_nine_region* patch;
	math::raw_frect region;
	graphic::color color;

	FORCE_INLINE friend renderer_frontend& operator<<(renderer_frontend& renderer, const nine_patch_draw& instr) {
		assert(instr.patch != nullptr);
		auto coords = instr.patch->get_row_coords(instr.region);
		auto uvs = instr.patch->get_row_uvs();
		for(int i = 0; i < 3; ++i){
			renderer.push(graphic::draw::instruction::row_patch{
				.generic = {.image = instr.patch->image_view->view},
				.coords = coords[i],
				.uvs = uvs[i],
				.vert_color = {instr.color}
			});
		}
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
