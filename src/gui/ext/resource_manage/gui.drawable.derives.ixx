module;

#include <mo_yanxi/adapted_attributes.hpp>

//TODO remove this after pipeline transition
#include <vulkan/vulkan.h>

export module mo_yanxi.gui.region_drawable.derives;

export import mo_yanxi.gui.region_drawable;


import mo_yanxi.graphic.draw.instruction;
import std;


namespace mo_yanxi::gui{

export
template <typename ...Components>
struct drawable_image final : drawable_base{
	image_region_borrow image_region;
	ADAPTED_NO_UNIQUE_ADDRESS component::combined_components<Components...> components;

	[[nodiscard]] drawable_image() = default;

	template <typename RegionTy>
		requires (std::constructible_from<image_region_borrow, RegionTy&&>)
	[[nodiscard]] explicit(std::convertible_to<RegionTy&&, image_region_borrow>) drawable_image(
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
struct icon final : drawable_base{
	image_region_borrow image_region;
	ADAPTED_NO_UNIQUE_ADDRESS component::combined_components<Components...> components;

	[[nodiscard]] icon() = default;

	template <typename RegionTy>
		requires (std::constructible_from<image_region_borrow, RegionTy&&>)
	[[nodiscard]] explicit(std::convertible_to<RegionTy&&, image_region_borrow>) icon(
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
	image_row_patch image_region;
	ADAPTED_NO_UNIQUE_ADDRESS component::combined_components<Components...> components;

	[[nodiscard]] drawable_row_patch() = default;

	template <typename RegionTy>
		requires (std::constructible_from<image_row_patch, RegionTy&&>)
	[[nodiscard]] explicit(std::convertible_to<RegionTy&&, image_row_patch>) drawable_row_patch(
		RegionTy&& image_region,
		const component::combined_components<Components...>& components = {})
	: image_region(std::forward<RegionTy>(image_region)),
	components(components){
	}

	void draw(renderer_frontend& renderer, const math::raw_frect& region,
		const graphic::color& color_scl) const override;
};

template <typename Fn>
concept canvas_drawer = std::invocable<const Fn&, renderer_frontend&, const math::raw_frect&, const graphic::color&>;

export
template <canvas_drawer Fn = std::function<void(renderer_frontend&, const math::raw_frect&, const graphic::color&)>>
struct drawable_canvas final : drawable_base{
	Fn drawer;
	fx::primitive_draw_mode draw_mode;

	drawable_canvas() = default;

	template <typename F>
	explicit(false) drawable_canvas(F&& drawer, const fx::primitive_draw_mode draw_mode = {})
		: drawer(std::forward<F>(drawer)),
		draw_mode(draw_mode){
	}

	void draw(renderer_frontend& renderer, const math::raw_frect& region,
		const graphic::color& color_scl) const override{
		std::invoke(drawer, renderer, region, color_scl);
	}
};

export
template <canvas_drawer Fn>
drawable_canvas(Fn&& fn) -> drawable_canvas<std::decay_t<Fn>>;








using namespace graphic::draw::instruction;



template <typename ... Components>
void drawable_image<Components...>::draw(renderer_frontend& renderer, const math::raw_frect& region,
	const graphic::color& color_scl) const{
	const fx::primitive_draw_mode mode = components;
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

template <typename... Components>
void icon<Components...>::draw(renderer_frontend& renderer, const math::raw_frect& region,
	const graphic::color& color_scl) const{
	if(!component::draw_switch{components})return;

	const fx::primitive_draw_mode mode = components;
	graphic::draw::quad_group<graphic::color> vcolor;
	vcolor = components;
	vcolor *= color_scl;

	[[maybe_unused]] state_guard guard{};

	if constexpr(mo_yanxi::is_any_of<component::batch_draw_mode, Components...>){
		fx::batch_draw_mode bm = components;
		guard = {renderer, bm};
	}

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
	const fx::primitive_draw_mode mode = components;
	graphic::draw::quad_group<graphic::color> vcolor = components;
	vcolor *= color_scl;
	[[maybe_unused]] state_guard guard{};

	if constexpr (mo_yanxi::is_any_of<component::batch_draw_mode, Components...>){
		fx::batch_draw_mode bm = components;
		guard = {renderer, bm};
	}

	renderer.push(graphic::draw::instruction::row_patch{
		.generic = {
			.image = {image_region.get_image_view()},
			.mode = {std::to_underlying(mode)},
		},
		.coords = image_region.get_ortho_draw_coords_axis_scaled({region.src, region.extent}),
		.uvs = image_region.get_uvs(),
		.vert_color = vcolor
	});
}

}