module;

#include <cassert>

module mo_yanxi.gui.elem.scroll_pane;

import mo_yanxi.graphic.draw.instruction;

namespace mo_yanxi::gui{
bool scroll_pane::update(const float delta_in_ticks){
	if(!elem::update(delta_in_ticks))return false;


	{//scroll update
		scrollVelocity.lerp_inplace(scrollTargetVelocity, delta_in_ticks * VelocitySensitivity);
		scrollTargetVelocity.lerp_inplace({}, delta_in_ticks * VelocityDragSensitivity);


		if(util::try_modify(
			scroll.base,
			math::fma(scrollVelocity, delta_in_ticks, scroll.base).clamp_xy({}, scrollable_extent()) * get_vel_clamp())){
			scroll.resume();
			updateChildrenAbsSrc();
		}
	}

	// prevRatio = getScrollRatio(scroll.base);

	if(item)item->update(delta_in_ticks);

	return true;
}

void scroll_pane::draw_background_impl(rect clipSpace) const{
	elem::draw_background_impl(clipSpace);

	auto& r = get_scene().renderer();

	const bool enableHori = is_hori_scroll_enabled();
	const bool enableVert = is_vert_scroll_enabled();

	assert(item);
	if(enableHori || enableVert){
		scissor_guard guard{r, {get_viewport()}};
		transform_guard transform_guard{r, math::mat3{}.idt().set_translation(-scroll.temp)};
		item->draw_background(clipSpace.move(scroll.temp));
	}else{
		item->draw_background(clipSpace);
	}
}

void scroll_pane::draw_content_impl(rect clipSpace) const{
	draw_style();

	auto& r = get_scene().renderer();

	const bool enableHori = is_hori_scroll_enabled();
	const bool enableVert = is_vert_scroll_enabled();

	assert(item);
	if(enableHori || enableVert){
		scissor_guard guard{r, {get_viewport()}};
		transform_guard transform_guard{r, math::mat3{}.idt().set_translation(-scroll.temp)};
		item->draw(clipSpace.move(scroll.temp));
	}else{
		item->draw(clipSpace);
	}

	if(enableHori){
		float shrink = scroll_bar_stroke_ * .25f;
		auto rect = get_hori_bar_rect().shrink(2).move_y(boarder().bottom * .0 + shrink);
		rect.add_height(-shrink);

		r.push(graphic::draw::instruction::rectangle_ortho{
			.v00 = rect.vert_00(),
			.v11 = rect.vert_11(),
			.vert_color = graphic::colors::gray
		});
	}

	if(enableVert){
		float shrink = scroll_bar_stroke_ * .25f;
		auto rect = get_vert_bar_rect().shrink(2).move_x(boarder().right * .0 + shrink);
		rect.add_width(-shrink);

		r.push(graphic::draw::instruction::rectangle_ortho{
			.v00 = rect.vert_00(),
			.v11 = rect.vert_11(),
			.vert_color = graphic::colors::gray
		});	}
}

void scroll_pane::update_item_layout(){
	assert(item != nullptr);

	//TODO merge these two method?
	deduced_set_child_fill_parent(*item);

	math::bool2 fill_mask{};

	switch(layout_policy_){
	case layout::layout_policy::hori_major:
		fill_mask = {true, false};
		break;
	case layout::layout_policy::vert_major:
		fill_mask = {false, true};
		break;
	case layout::layout_policy::none:
		fill_mask = {false, false};
		break;
	default: std::unreachable();
	}
	util::set_fill_parent(*item, content_extent(), fill_mask, !fill_mask);

	auto bound = item->restriction_extent;

	using namespace layout;

	item->set_prefer_extent(get_viewport_extent());
	if(auto sz = item->pre_acquire_size(bound)){
		bool need_self_relayout = false;

		if(bar_caps_size){
			bool need_elem_relayout = false;
			switch(layout_policy_){
			case layout_policy::hori_major :{
				if(sz->y > content_height()){
					bound.set_width(math::clamp_positive(bound.potential_width() - scroll_bar_stroke_));
					need_elem_relayout = true;
				}

				if(restriction_extent.width_pending() && sz->x > content_width()){
					need_self_relayout = true;
				}

				break;
			}
			case layout_policy::vert_major :{
				if(sz->x > content_width()){
					bound.set_height(math::clamp_positive(bound.potential_height() - scroll_bar_stroke_));
					need_elem_relayout = true;
				}

				if(restriction_extent.height_pending() && sz->y > content_height()){
					need_self_relayout = true;
				}
				break;
			}
			default: break;
			}

			if(need_elem_relayout){
				auto b = bound;
				b.apply(content_extent());
				item->set_prefer_extent(b.potential_extent());
				if(auto s = item->pre_acquire_size(bound)) sz = s;
			}
		}

		item->resize(*sz, propagate_mask::local | propagate_mask::child);

		if(need_self_relayout){
			auto elemSz = item->extent();
			switch(layout_policy_){
			case layout_policy::hori_major :{
				if(elemSz.x > content_width()){
					//width resize
					elemSz.y = content_height();
					elemSz.x += static_cast<float>(bar_caps_size) * scroll_bar_stroke_;
				}
				break;
			}
			case layout_policy::vert_major :{
				if(elemSz.y > content_height()){
					//height resize
					elemSz.x = content_width();
					elemSz.y += static_cast<float>(bar_caps_size) * scroll_bar_stroke_;
				}
				break;
			}
			default: break;
			}

			elemSz += boarder().extent();
			resize(elemSz);
		}
	}

	item->layout_elem();
}

void scroll_pane::deduced_set_child_fill_parent(elem& element) const noexcept{
	using namespace layout;
	element.restriction_extent = extent_by_external;
	switch(layout_policy_){
	case layout_policy::hori_major :{
		element.set_fill_parent({true, false}, propagate_mask::none);
		element.restriction_extent.set_width(content_width());
		break;
	}
	case layout_policy::vert_major :{
		element.set_fill_parent({false, true}, propagate_mask::none);
		element.restriction_extent.set_height(content_height());
		break;
	}
	case layout_policy::none:
		element.set_fill_parent({false, false}, propagate_mask::none);
		break;
	default: std::unreachable();
	}
}
}
