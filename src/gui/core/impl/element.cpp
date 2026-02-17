module;

#include <cassert>

#ifndef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
#include <beman/inplace_vector.hpp>
#endif

module mo_yanxi.gui.infrastructure;

import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.gui.draw.compound;
import mo_yanxi.gui.draw.fringe;

#ifdef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
import <beman/inplace_vector.hpp>;
#endif

namespace mo_yanxi::gui{
void style::debug_elem_drawer::draw_layer_impl(const elem& element, math::frect region, float opacityScl,
	gfx_config::layer_param layer_param) const{
	switch(layer_param.layer_index){
	case 0 : draw(element, region, opacityScl);
		break;
	case 1 : draw_background(element, region, opacityScl);
		break;
	default : std::unreachable();
	}
}

void style::debug_elem_drawer::draw(const elem& element, rect region, float opacityScl) const{

	auto cregion = element.clip_to_content_bound(region);

	element.get_scene().renderer().push(graphic::draw::instruction::rect_aabb_outline{
		.v00 = cregion.vert_00(),
		.v11 = cregion.vert_11(),
		.stroke = 1,
		.vert_color = graphic::colors::YELLOW.copy().set_a(.1f)
	});

	using namespace graphic;
	using namespace graphic::draw::instruction;
	color c = colors::gray;
	/*if(element.cursor_state().pressed){
		c = colors::aqua;
	}else */if(element.cursor_state().focused){
		c = colors::white;
	}else if(element.cursor_state().inbound){
		c = colors::light_gray;
	}
	c.set_a(.75f);
	float f1 = element.cursor_state().get_factor_of(&cursor_states::time_inbound);
	float f2 = element.cursor_state().get_factor_of(&cursor_states::time_pressed);
	float f3 = element.cursor_state().get_factor_of(&cursor_states::time_focus);

	float light = (element.is_toggled() ? 1.6f : 1.f) * (element.is_disabled() ? .5f : 1.f);

	draw::quad_group vc{
		c.mul_a(opacityScl).set_light(light),
		c.create_lerp(colors::ACID.to_light(2), f1).mul_a(opacityScl).set_light(light),
		c.create_lerp(colors::ORANGE.to_light(2), f2).mul_a(opacityScl).set_light(light),
		c.create_lerp(colors::CRIMSON.to_light(2), f3).mul_a(opacityScl).set_light(light),
	};

	auto vcb = vc;
	vcb *= color{.1, .1, .1, 1};


	region.shrink(1.f);
	element.get_scene().renderer().push(rect_aabb_outline{
			.v00 = region.vert_00(),
			.v11 = region.vert_11(),
			.stroke = {2},
			.vert_color = {vc}
		});

	if(element.cursor_state().inbound){
		auto pos = util::transform_scene2local(element, element.get_scene().get_cursor_pos());
		auto pos_abs = pos + element.pos_abs();

		auto hit_region = rect{pos_abs, 5 + util::get_nest_depth(&element) * 9.f};

		element.get_scene().renderer().push(rect_aabb_outline{
			.v00 = hit_region.vert_00(),
			.v11 = hit_region.vert_11(),
			.stroke = {2},
			.vert_color = colors::LIME.copy().set_a(.8f)
		});

		auto seg = math::rect::get_closest_vertex_pair(region, hit_region);

		fx::fringe::line_context ctx{std::in_place_type<beman::inplace_vector::inplace_vector<line_node, 12>>};

		fx::compound::dash_line(seg, {8.0, 6.0, 24.0, 6.0}, [&](math::section<math::vec2> s){
			ctx.push(s.from, 1, colors::LIME.copy().set_a(.8f));
			ctx.push(s.to, 1, colors::LIME.copy().set_a(.8f));
			static constexpr float stroke = .5f;
			ctx.add_fringe_cap(stroke, stroke);
			ctx.dump_fringe_inner(element.renderer(), line_segments{}, stroke);
			ctx.dump_fringe_outer(element.renderer(), line_segments{}, stroke);
			ctx.dump_mid(element.renderer(), line_segments{});
			ctx.clear();
		});
	}

	if(element.update_flag.is_self_update_required()){
		element.renderer().push(poly_partial{
			.pos = region.vert_00() ,
			.segments = 4,
			.radius = {3, 5},
			.range = {static_cast<float>(element.get_scene().get_current_time() / 60.f), 1},
			.color = {colors::aqua}
		});
	}

	region.scl_size(.25f, .25f);

	// if(element.draw_flag.is_draw_required()){
	auto opct = element.draw_flag.get_debug_count();
		element.renderer().push(triangle{
			.p0 = region.vert_00(),
			.p1 = region.vert_10(),
			.p2 = region.vert_01(),
			.c0 = colors::pale_green.copy_set_a(opct * .25f),
			.c1 = colors::pale_green.copy_set_a(opct * .25f),
			.c2 = colors::pale_green.copy_set_a(opct * .25f)

		});
	// }
}

void style::debug_elem_drawer::draw_background(const elem& element, math::frect region, float opacityScl) const{
	using namespace graphic;

	element.get_scene().renderer().push(draw::instruction::rect_aabb{
			.v00 = region.vert_00(),
			.v11 = region.vert_11(),
			.vert_color = {colors::dark_gray.create_lerp({0, 0, 0, 1}, .85f).copy().mul_a(opacityScl)}
		});
}

tooltip::align_config elem::tooltip_get_align_config() const{
	tooltip::align_config cfg{
			tooltip_create_config.layout_info.follow,
			tooltip_create_config.layout_info.attach_point_tooltip
		};
	switch(tooltip_create_config.layout_info.follow){
	case tooltip::anchor_type::owner :{
		auto pos = align::get_vert(tooltip_create_config.layout_info.attach_point_spawner, extent());
		pos = util::transform_local2scene(*this, pos);
		cfg.pos = pos;
		break;
	}

	default : break;
	}
	return cfg;
}

bool elem::tooltip_spawner_contains(math::vec2 cursorPos) const noexcept{
	return rect{extent()}.contains_loose(util::transform_scene2local(*this, cursorPos));
}

void elem::draw_layer(const rect clipSpace, gfx_config::layer_param_pass_t param) const{
	draw_style(param);
}

bool elem::update(float delta_in_ticks){

	if(sleep)return false;

	for(float actionDelta = delta_in_ticks; !actions.empty();){
		const auto& current = actions.front();

		actionDelta = current->update(actionDelta, *this);

		if(actionDelta >= 0) [[unlikely]] {
			actions.pop_front();
		} else{
			break;
		}
	}

	return true;
}

void elem::clear_scene_references() noexcept{
	assert(scene_ != nullptr);
	scene_->drop_(this);
}

void elem::notify_layout_changed(propagate_mask propagation){
	if(check_propagate_satisfy(propagation, propagate_mask::local)) layout_state.notify_self_changed();

	if(parent_){
		const bool force_upper = check_propagate_satisfy(propagation, propagate_mask::force_upper);
		if(force_upper || (check_propagate_satisfy(propagation, propagate_mask::super) && layout_state.is_broadcastable(propagate_mask::super))){
			if(parent_->layout_state.notify_children_changed(force_upper)){
				if(parent_->layout_state.intercept_lower_to_isolated){
					parent_->notify_isolated_layout_changed();
				}else{
					parent_->notify_layout_changed(propagation - propagate_mask::child);
				}
			}
		}
	}

	if(check_propagate_satisfy(propagation, propagate_mask::child) && layout_state.is_broadcastable(propagate_mask::child)){
		for(auto&& element : children()){
			if(element->layout_state.notify_parent_changed()){
				element->notify_layout_changed(propagation - propagate_mask::super);
			}
		}
	}
}

void elem::notify_isolated_layout_changed(){
	layout_state.notify_self_changed();
	get_scene().notify_isolated_layout_update(this);
}

bool elem::update_abs_src(math::vec2 parent_content_src) noexcept{
	return util::try_modify(absolute_pos_, parent_content_src + relative_pos_);
}

bool elem::contains(const math::vec2 pos_relative) const noexcept{
	return bound_rel().contains_loose(pos_relative) &&
		(!parent() || parent()->parent_contain_constrain(parent()->transform_from_content_space(pos_relative)));
}

bool elem::contains_self(const math::vec2 pos_relative, const float margin) const noexcept{
	return bound_rel().expand(margin, margin).contains_loose(pos_relative);
}

bool elem::parent_contain_constrain(const math::vec2 pos_relative) const noexcept{
	return (!parent() || parent()->parent_contain_constrain(parent()->transform_from_content_space(pos_relative)));
}

bool elem::is_focused_scroll() const noexcept{
	assert(scene_ != nullptr);
	return scene_->focus_cursor_ == this;

}

bool elem::is_focused_key() const noexcept{
	assert(scene_ != nullptr);
	return scene_->focus_key_ == this;
}

bool elem::is_focused() const noexcept{
	assert(scene_ != nullptr);
	return scene_->focus_cursor_ == this;
}

bool elem::is_inbounded() const noexcept{
	assert(scene_ != nullptr);
	return std::ranges::contains(scene_->get_inbounds(), this);
}

void elem::set_focused_scroll(const bool focus) noexcept{
	if(!focus && !is_focused_scroll()) return;
	this->scene_->focus_scroll_ = focus ? this : nullptr;
}

void elem::set_focused_key(const bool focus) noexcept{
	if(!focus && !is_focused_key()) return;
	this->scene_->focus_key_ = focus ? this : nullptr;
}

void elem::update_altitude_(altitude_t height){
	if(layer_altitude_ == height)return;
	scene_->layer_altitude_record_.erase(layer_altitude_);
	layer_altitude_ = height;
	scene_->layer_altitude_record_.insert(layer_altitude_);
	for (const auto & child : children()){
		child->update_altitude_(height + 1);
	}
}

void elem::init_altitude_(altitude_t height){
	layer_altitude_ = height;
	scene_->layer_altitude_record_.insert(layer_altitude_);
}

void elem::relocate_scene_(struct gui::scene* scene) noexcept{
	for (auto && child : children()){
		child->relocate_scene_(scene);
	}

	scene_ = scene;
}

events::op_afterwards util::thoroughly_esc(elem* where) noexcept{
	while(where){
		if(where->on_esc() == events::op_afterwards::intercepted){
			return events::op_afterwards::intercepted;
		}
		where = where->parent();
	}
	return events::op_afterwards::fall_through;
}
}
