module;

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.region_drawable.derives;

export import mo_yanxi.gui.region_drawable;


import mo_yanxi.graphic.draw.instruction;
import std;


namespace mo_yanxi::gui{

export
template <typename ...Components>
struct drawable_image final : drawable_base{
	assets::image_region_borrow image_region;
	ADAPTED_NO_UNIQUE_ADDRESS component::combined_components<Components...> components;

	[[nodiscard]] drawable_image() = default;

	template <typename RegionTy>
		requires (std::constructible_from<assets::image_region_borrow, RegionTy&&>)
	[[nodiscard]] explicit(std::convertible_to<RegionTy&&, assets::image_region_borrow>) drawable_image(
		RegionTy&& image_region,
		const component::combined_components<Components...>& components = {})
	: image_region(std::forward<RegionTy>(image_region)),
	components(components){
	}

	void draw(renderer_frontend& renderer, const math::raw_frect& region,
		const graphic::color& color_scl) const override;
};

export
template <typename ...Components>
struct drawable_row_patch final : drawable_base{
	assets::row_patch image_region;
	ADAPTED_NO_UNIQUE_ADDRESS component::combined_components<Components...> components;

	[[nodiscard]] drawable_row_patch() = default;

	template <typename RegionTy>
		requires (std::constructible_from<assets::row_patch, RegionTy&&>)
	[[nodiscard]] explicit(std::convertible_to<RegionTy&&, assets::row_patch>) drawable_row_patch(
		RegionTy&& image_region,
		const component::combined_components<Components...>& components = {})
	: image_region(std::forward<RegionTy>(image_region)),
	components(components){
	}

	void draw(renderer_frontend& renderer, const math::raw_frect& region,
		const graphic::color& color_scl) const override;
};


using namespace graphic::draw::instruction;
template <typename ... Components>
void drawable_image<Components...>::draw(renderer_frontend& renderer, const math::raw_frect& region,
	const graphic::color& color_scl) const{
	const primitive_draw_mode mode = components;
	graphic::draw::quad_group<graphic::color> vcolor = components;
	vcolor *= color_scl;

	renderer.push(rect_aabb{
		.generic = {
			.image = {.view = image_region->view},
			.mode = {std::to_underlying(mode)},
		},
		.v00 = region.vert_00(),
		.v11 = region.vert_11(),
		.uv00 = image_region->uv.v00(),
		.uv11 = image_region->uv.v11(),
		.vert_color = vcolor
	});

}

template <typename ... Components>
void drawable_row_patch<Components...>::draw(renderer_frontend& renderer, const math::raw_frect& region,
	const graphic::color& color_scl) const{
	const primitive_draw_mode mode = components;
	graphic::draw::quad_group<graphic::color> vcolor = components;
	vcolor *= color_scl;

	renderer.push(graphic::draw::instruction::row_patch{
		.generic = {
			.image = {image_region.get_image_view()},
			.mode = {std::to_underlying(mode)},
		},
		.coords = image_region.get_ortho_draw_coords(region.src, region.extent),
		.uvs = image_region.get_uvs(),
		.vert_color = vcolor
	});
}

}