module;

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.region_drawable;

export import mo_yanxi.math.rect_ortho;
import mo_yanxi.allocator_aware_unique_ptr;
export import mo_yanxi.graphic.color;
export import mo_yanxi.gui.assets.image_regions;
export import mo_yanxi.gui.renderer.frontend;
import mo_yanxi.gui.alloc;
import align;

import std;




namespace mo_yanxi::gui{

export
struct image_display_style{
	align::scale scaling{align::scale::fit};
	align::pos align{align::pos::center};
	// style::palette palette{style::general_palette};
};

export
struct drawable_base{
	virtual ~drawable_base() = default;

	virtual void draw(renderer_frontend& renderer, const math::raw_frect& region, const graphic::color& color_scl) const = 0;

	[[nodiscard]] virtual math::optional_vec2<float> get_preferred_extent() const noexcept{
		return math::nullopt_vec2<float>;
	}
};

export
struct styled_drawable{
	image_display_style style{};
	allocator_aware_poly_unique_ptr<drawable_base, mr::heap_allocator<drawable_base>> drawable{};
};

namespace component{


template <typename Dst, typename ...Args>
concept any_convertible_to = (... || std::convertible_to<const Args&, Dst>);

export
template <typename ...Ts>
struct base{
	[[nodiscard]] constexpr explicit(false) operator graphic::draw::quad_group<graphic::color>() const noexcept requires(!any_convertible_to<graphic::draw::quad_group<graphic::color>, Ts...>){
		return graphic::draw::quad_group{graphic::colors::white};
	}

	[[nodiscard]] constexpr explicit(false) operator fx::primitive_draw_mode() const noexcept  requires(!any_convertible_to<fx::primitive_draw_mode, Ts...>){
		return fx::primitive_draw_mode{};
	}

};

export
struct EMPTY_BASE vertex_color{

	graphic::draw::quad_group<graphic::color> color;

	[[nodiscard]] constexpr explicit(false) operator graphic::draw::quad_group<graphic::color>() const noexcept{
		return color;
	}

};

export
struct EMPTY_BASE draw_mode{
	fx::primitive_draw_mode mode;

	[[nodiscard]] constexpr explicit(false) operator fx::primitive_draw_mode() const noexcept{
		return mode;
	}
};

export
template<typename ...Comps>
struct EMPTY_BASE combined_components : Comps..., base<Comps...>{

};


}


}
