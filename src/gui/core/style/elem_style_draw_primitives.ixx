module;

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.style.elem_style_draw_primitives;

import std;
import mo_yanxi.math;
import mo_yanxi.react_flow.flexible_value;
export import mo_yanxi.gui.style.tree;
export import mo_yanxi.gui.style.palette;
export import mo_yanxi.gui.fx.instruction_extension;
import mo_yanxi.gui.infrastructure;
import mo_yanxi.graphic.g2d;
import mo_yanxi.gui.fx.compound;
import mo_yanxi.graphic.g2d.fringe;

namespace mo_yanxi::gui::style::primitives{

export
struct draw_empty{
	using target_type = elem;

	void operator()(const typed_draw_param<elem>& p) const{
	}
};

export
struct draw_debug_frame{
	using target_type = elem;

	void operator()(const typed_draw_param<elem>& p) const{
		const elem& element = p.subject();
		auto region = p->draw_bound;
		float opacityScl = p->opacity_scl;

		auto cregion = element.clip_to_content_bound(region);

		element.renderer().push(graphic::g2d::rect_aabb_outline{
			.v00 = cregion.vert_00(),
			.v11 = cregion.vert_11(),
			.stroke = 1,
			.vert_color = graphic::colors::YELLOW.copy().set_a(.1f)
		});

		using namespace graphic;
		using namespace graphic::g2d;
		color c = colors::gray;
		if(element.cursor_state().focused){
			c = colors::white;
		}else if(element.cursor_state().inbound){
			c = colors::light_gray;
		}
		c.set_a(.75f);
		float f1 = element.cursor_state().get_factor_of(&cursor_states::time_inbound);
		float f2 = element.cursor_state().get_factor_of(&cursor_states::time_pressed);
		float f3 = element.cursor_state().get_factor_of(&cursor_states::time_focus);

		float light = (element.is_toggled() ? 1.6f : 1.f) * (element.is_disabled() ? .5f : 1.f);

		graphic::g2d::quad_group vc{
			c.mul_a(opacityScl).set_light(light),
			c.create_lerp(colors::ACID.to_light(2), f1).mul_a(opacityScl).set_light(light),
			c.create_lerp(colors::ORANGE.to_light(2), f2).mul_a(opacityScl).set_light(light),
			c.create_lerp(colors::CRIMSON.to_light(2), f3).mul_a(opacityScl).set_light(light),
		};

		region.shrink(1.f);
		element.renderer().push(rect_aabb_outline{
				.v00 = region.vert_00(),
				.v11 = region.vert_11(),
				.stroke = {2},
				.vert_color = {vc}
			});

		if(element.cursor_state().inbound){
			auto pos = util::transform_scene2local(element, element.get_scene().get_cursor_pos());
			auto pos_abs = pos + element.pos_abs();

			auto hit_region = rect{pos_abs, 5 + util::get_nest_depth(&element) * 9.f};

			element.renderer().push(rect_aabb_outline{
				.v00 = hit_region.vert_00(),
				.v11 = hit_region.vert_11(),
				.stroke = {2},
				.vert_color = colors::LIME.copy().set_a(.8f)
			});

			auto seg = math::rect::get_closest_vertex_pair(region, hit_region);

			graphic::g2d::fringe::inplace_line_context<12> ctx{};

			fx::compound::dash_line(seg, {8.0, 6.0, 24.0, 6.0}, [&](math::section<math::vec2> s){
				ctx.push(s.from, 1, colors::LIME.copy().set_a(.8f));
				ctx.push(s.to, 1, colors::LIME.copy().set_a(.8f));
				static constexpr float stroke = .5f;
				ctx.add_fringe_cap(stroke, stroke);
				element.renderer() << ctx.fringe_inner(line_segments{}, stroke);
				element.renderer() << ctx.fringe_outer(line_segments{}, stroke);
				element.renderer() << ctx.mid(line_segments{});
				ctx.clear();
			});
		}

		region.scl_size(.25f, .25f);
	}
};

export
struct draw_debug_background{
	using target_type = elem;

	void operator()(const typed_draw_param<elem>& p) const{
		using namespace graphic;

		auto region = p->draw_bound;
		float opacityScl = p->opacity_scl;

		p.subject().renderer().push(graphic::g2d::rect_aabb{
				.v00 = region.vert_00(),
				.v11 = region.vert_11(),
				.vert_color = {colors::dark_gray.create_lerp({0, 0, 0, 1}, .85f).copy().mul_a(opacityScl)}
			});
	}
};

export
struct draw_filled_rect{
	using target_type = elem;
	palette pal{};

	void operator()(const typed_draw_param<elem>& p) const{
		const elem& e = p.subject();
		auto color = pal.on_instance(e).mul_a(p->opacity_scl);
		auto region = p->draw_bound;
		e.renderer().push(graphic::g2d::rect_aabb{
			.v00 = region.vert_00(),
			.v11 = region.vert_11(),
			.vert_color = {color}
		});
	}
};

}

namespace mo_yanxi::gui::style{

export
inline target_known_node_ptr<elem> make_empty_elem_style(){
	return make_tree_node_ptr(tree_leaf{primitives::draw_empty{}});
}

export
inline target_known_node_ptr<elem> make_debug_elem_style(){
	auto frame_leaf = tree_leaf{primitives::draw_debug_frame{}, std::in_place_type<elem>};
	auto background_leaf = tree_leaf{primitives::draw_debug_background{}, std::in_place_type<elem>};
	auto metrics_leaf = tree_metrics_leaf{[]{ return style_tree_metrics{.inset = default_border}; },
	                                      std::in_place_type<elem>};
	auto debug_tree = tree_tuple_fork{
		layer_router{style_config{0b01}, std::move(frame_leaf)},
		layer_router{style_config{0b10}, std::move(background_leaf)},
		std::move(metrics_leaf)
	};
	return make_tree_node_ptr(std::move(debug_tree));
}

}
