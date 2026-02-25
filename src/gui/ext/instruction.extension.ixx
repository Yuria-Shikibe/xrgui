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
struct nine_patch_draw{
	const image_nine_region* nine_region;
	math::raw_frect region;
	graphic::color color;

	FORCE_INLINE friend renderer_frontend& operator<<(renderer_frontend& renderer, const nine_patch_draw& instr) {
		assert(instr.nine_region != nullptr);
		auto coords = instr.nine_region->get_row_coords(instr.region);
		auto uvs = instr.nine_region->get_row_uvs();
		for(int i = 0; i < 3; ++i){
			renderer.push(graphic::draw::instruction::row_patch{
				.generic = {.image = instr.nine_region->image_view->view},
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
	const image_nine_region* nine_region;
	math::raw_frect region;
	graphic::draw::instruction::quad_vert_color color;

	FORCE_INLINE friend renderer_frontend& operator<<(renderer_frontend& renderer, const nine_patch_draw_vert_color& instr) {
		assert(instr.nine_region != nullptr);
		auto& nine_region = *instr.nine_region;
		auto coords = nine_region.get_row_coords(instr.region);
		auto uvs = nine_region.get_row_uvs();

		auto [CLB, CLT] = nine_region.interpolate_middle_row_values(instr.color.v00, instr.color.v01, instr.region.extent.y);
		auto [CRB, CRT] = nine_region.interpolate_middle_row_values(instr.color.v10, instr.color.v11, instr.region.extent.y);

		renderer.push(graphic::draw::instruction::row_patch{
			.generic = {.image = nine_region.image_view->view},
			.coords = coords[0],
			.uvs = uvs[0],
			.vert_color = graphic::draw::instruction::quad_vert_color{instr.color.v00, instr.color.v10, CLB, CRB}
		});
		renderer.push(graphic::draw::instruction::row_patch{
			.generic = {.image = nine_region.image_view->view},
			.coords = coords[1],
			.uvs = uvs[1],
			.vert_color = graphic::draw::instruction::quad_vert_color{CLB, CRB, CLT, CRT}
		});
		renderer.push(graphic::draw::instruction::row_patch{
			.generic = {.image = nine_region.image_view->view},
			.coords = coords[2],
			.uvs = uvs[2],
			.vert_color = graphic::draw::instruction::quad_vert_color{CLT, CRT, instr.color.v01, instr.color.v11}
		});
		return renderer;
	}
};

}
