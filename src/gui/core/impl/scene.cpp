module mo_yanxi.gui.infrastructure;

import std;

namespace mo_yanxi::gui{
scene::scene(scene&& other) noexcept: scene_base{std::move(other)}{
	root().reset_scene(this);
	inputs_.main_binds.set_context(std::ref(*this));
}

scene& scene::operator=(scene&& other) noexcept{
	if(this == &other) return *this;
	scene_base::operator =(std::move(other));
	root().reset_scene(this);
	inputs_.main_binds.set_context(std::ref(*this));
	return *this;
}

void scene::update(float delta_in_tick){
	react_flow_->update();
	tooltip_manager_.update(delta_in_tick, get_cursor_pos(), is_mouse_pressed());
	overlay_manager_.update(delta_in_tick);

	if(request_cursor_update_){
		update_cursor();
	}
	root().update(delta_in_tick);
}

void scene::draw(rect clip){
	renderer().init_projection();
	{
		gui::viewport_guard _{renderer(), region_};

		gui::mode_guard _m{renderer(), gui::draw_mode_param{gui::draw_mode::msdf}};
		const auto root_bound = region_.round<int>().max_src({});

		auto draw_elem = [&](const elem& e, rect region){
			e.draw(region);
			renderer().update_state(draw_mode_param{
				.mode = draw_mode::msdf,
				.draw_targets = 0b10
			});
			e.draw_background(region);
			const auto bound = region.round<int>();
			renderer().update_state(blit_config{
				.blit_region = {bound.src, bound.extent()}
			});
			renderer().update_state(draw_mode_param{
				.mode = draw_mode::msdf,
				.draw_targets = 0b01
			});
		};

		for (auto&& elem : tooltip_manager_.get_draw_sequence()){
			if(elem.belowScene){
				draw_elem(*elem.element, elem.element->bound_abs());
			}
		}

		draw_elem(*root_, clip);


		for (const auto & draw_sequence : overlay_manager_.get_draw_sequence()){
			draw_elem(*draw_sequence, draw_sequence->bound_abs());

		}

		for (auto&& elem : tooltip_manager_.get_draw_sequence()){
			if(!elem.belowScene){
				draw_elem(*elem.element, elem.element->bound_abs());
			}
		}
	}
	renderer().consume();

}

void scene::input_key(const input_handle::key_set key){
	inputs_.inform(key);

	if(key.action == input_handle::act::press && key.key_code == std::to_underlying(input_handle::key::esc)){


		if(on_esc() == events::op_afterwards::fall_through){
			update_cursor();
		}
	}else{
		elem* focus = focus_key_;

		//TODO fallback
		if(!focus) focus = focus_cursor_;
		if(!focus) return;
		focus->on_key_input(key);
	}
}

void scene::on_unicode_input(char32_t val) const{
	if(focus_key_){
		focus_key_->on_unicode_input(val);
	}
}

void scene::on_scroll(const math::vec2 scroll) const{
	events::scroll e{scroll, inputs_.main_binds.get_mode()};
	if(focus_scroll_){
		auto rst = focus_scroll_->on_scroll(e, {});
		if(rst == events::op_afterwards::intercepted)return;

		if(const auto itr = std::ranges::find(last_inbounds_, focus_cursor_); itr == last_inbounds_.end()){
			auto cur = std::reverse_iterator{itr};
			while(cur != last_inbounds_.rend()){
				std::span aboves{cur.base(), last_inbounds_.end()};
				if((*cur)->on_scroll(e, aboves) != events::op_afterwards::intercepted){
					++cur;
				}else{
					break;
				}
			}
		}
	}else{
		auto cur = last_inbounds_.rbegin();
		while(cur != last_inbounds_.rend()){
			std::span aboves{cur.base(), last_inbounds_.end()};
			if((*cur)->on_scroll(e, aboves) != events::op_afterwards::intercepted){
				++cur;
			}else{
				break;
			}
		}
	}
}

void scene::update_cursor(){
	request_cursor_update_ = false;

	if(!(focus_cursor_ && is_mouse_pressed() && focus_cursor_->is_focus_extended_by_mouse())){
		//TODO using double swap buffer to reduce heap allocation?
		mr::heap_vector<elem*> inbounds{get_heap_allocator()};

		//TODO tooltip & dialog window
		for (auto && activeTooltip : tooltip_manager_.get_active_tooltips() | std::views::reverse){
			if(tooltip_manager_.is_below_scene(activeTooltip.element.get()))continue;
			inbounds = util::dfs_find_deepest_element(activeTooltip.element.get(), get_cursor_pos(), get_heap_allocator<elem*>());
			if(!inbounds.empty())goto upt;
		}

		if(!overlay_manager_.empty()){
			auto& top = *std::ranges::rbegin(overlay_manager_);
			if(inbounds.empty()){
				inbounds = util::dfs_find_deepest_element(top.get(), get_cursor_pos(), get_heap_allocator<elem*>());
			}
		}else{
			if(inbounds.empty()){
				inbounds = util::dfs_find_deepest_element(&root(), get_cursor_pos(), get_heap_allocator<elem*>());
			}

			if(inbounds.empty()){
				for (auto && activeTooltip : tooltip_manager_.get_active_tooltips() | std::views::reverse){
					if(!tooltip_manager_.is_below_scene(activeTooltip.element.get()))continue;
					inbounds = util::dfs_find_deepest_element(activeTooltip.element.get(), get_cursor_pos(), get_heap_allocator<elem*>());
					if(!inbounds.empty())goto upt;
				}
			}
		}


		upt:

		update_inbounds(std::move(inbounds));
	}

	if(!focus_cursor_) return;

	const auto cursor_transformed = util::transform_from_root_to_current(last_inbounds_, get_cursor_pos());
	for(const auto& [i, state] : mouse_states_ | std::views::enumerate){
		if(!state.pressed) continue;

		auto src = util::transform_from_root_to_current(last_inbounds_, state.src);
		auto dst = cursor_transformed;
		const auto mode = inputs_.main_binds.get_mode();
		events::key_set key{static_cast<std::uint16_t>(i), input_handle::act::ignore, mode};

		auto cur = last_inbounds_.rbegin();
		while(cur != last_inbounds_.rend()){
			if((*cur)->on_drag({src, dst, key}) != events::op_afterwards::intercepted){
				src = (*cur)->transform_from_children(src);
				dst = (*cur)->transform_from_children(dst);
				++cur;
			}else{
				break;
			}
		}
	}

	focus_cursor_->on_cursor_moved(events::cursor_move{.src = inputs_.last_cursor_pos(), .dst = inputs_.cursor_pos()});
}

events::op_afterwards scene::on_esc(){
	if(overlay_manager_.on_esc() != events::op_afterwards::fall_through)return events::op_afterwards::intercepted;
	if(tooltip_manager_.on_esc() != events::op_afterwards::fall_through)return events::op_afterwards::intercepted;

	elem* focus = focus_key_;
	if(!focus) focus = focus_cursor_;

	return util::thoroughly_esc(focus);
}

void scene::resize(const math::frect region){
	if(util::try_modify(region_, region)){
		renderer().resize(region);
		root_->resize(region.extent());
		overlay_manager_.resize(region);
	}
}

void scene::layout(){
	std::size_t count{};
	while(root_->layout_state.is_children_changed() || !independent_layouts_.empty()){
		for(const auto layout : independent_layouts_){
			layout->try_layout();
		}
		independent_layouts_.clear();

		root_->try_layout();

		count++;
		if(count > 8){
			// break;
			throw std::runtime_error("Bad Layout: Iteration Too Many Times");
		}
	}
}

void scene::update_inbounds(mr::heap_vector<elem*>&& next){
	if(last_inbounds_.size() == next.size() && (last_inbounds_.empty() || last_inbounds_.back() == next.back()))return;

	auto [i1, i2] = std::ranges::mismatch(last_inbounds_, next);

	for(const auto& element : std::ranges::subrange{i1, last_inbounds_.end()} | std::views::reverse){
		element->on_inbound_changed(false, true);
	}

	auto itr = next.begin();
	for(; itr != i2; ++itr){
		(*itr)->on_inbound_changed(true, false);
	}

	for(; itr != next.end(); ++itr){
		(*itr)->on_inbound_changed(true, true);
	}

	last_inbounds_ = std::move(next);

	try_swap_focus(last_inbounds_.empty() ? nullptr : last_inbounds_.back());
}

void scene::update_mouse_state(const input_handle::key_set k){
	using namespace input_handle;
	auto [c, a, m] = k;

	if(a == act::press && focus_key_ && !focus_key_->contains(get_cursor_pos())){
		focus_key_->on_focus_key_changed(false);
		focus_key_ = nullptr;
	}

	if(focus_cursor_){
		const auto pos = util::transform_from_root_to_current(last_inbounds_, get_cursor_pos());
		events::click e{pos, k};

		auto cur = last_inbounds_.rbegin();
		while(cur != last_inbounds_.rend()){
			std::span aboves{cur.base(), last_inbounds_.end()};
			if((*cur)->on_click(e, aboves) != events::op_afterwards::intercepted){
				e.pos = (*cur)->transform_from_children(e.pos);
				++cur;
			}else{
				break;
			}
		}
	}

	if(a == act::press){
		mouse_states_[c].reset(get_cursor_pos());
	}

	if(a == act::release){
		mouse_states_[c].clear(get_cursor_pos());

		if(focus_cursor_ && focus_cursor_->is_focus_extended_by_mouse()){
			update_cursor();
		}
	}

}


void scene::try_swap_focus(elem* newFocus){
	if(newFocus == focus_cursor_) return;

	if(focus_cursor_){
		if(focus_cursor_->is_focus_extended_by_mouse()){
			if(!is_mouse_pressed()){
				swap_focus(newFocus);
			} else return;
		} else{
			swap_focus(newFocus);
		}
	} else{
		swap_focus(newFocus);
	}
}

void scene::swap_focus(elem* newFocus){
	if(focus_cursor_){
		for(auto& state : mouse_states_){
			state.clear(get_cursor_pos());
		}
		focus_cursor_->on_focus_changed(false);
		focus_cursor_->cursor_states_.quit_focus();
	}

	focus_cursor_ = newFocus;

	if(focus_cursor_){
		if(focus_cursor_->is_interactable()){
			focus_cursor_->on_focus_changed(true);
		}
	}
}

void scene::drop_(const elem* target) noexcept{
	drop_elem_nodes(target);
	drop_event_focus(target);
	std::erase(last_inbounds_, target);
	// asyncTaskOwners.erase(const_cast<elem*>(target));
	independent_layouts_.erase(const_cast<elem*>(target));
	// erase_independent_draw(target);
	// erase_direct_access({}, target);
	// tooltipManager.requestDrop(*target);
}
}
