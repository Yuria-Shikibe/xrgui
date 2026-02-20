module mo_yanxi.gui.infrastructure;

import std;

namespace mo_yanxi::gui{
scene::scene(scene&& other) noexcept: scene_base{std::move(other)}{
	root().relocate_scene_(this);
	inputs_.main_binds.set_context(std::ref(*this));
}

scene& scene::operator=(scene&& other) noexcept{
	if(this == &other) return *this;
	scene_base::operator =(std::move(other));
	root().relocate_scene_(this);
	inputs_.main_binds.set_context(std::ref(*this));
	return *this;
}

void scene::update(double delta_in_tick){
	react_flow_->update();
	update_elem_cursor_state_(delta_in_tick);

	tooltip_manager_.update(delta_in_tick, get_cursor_pos(), is_mouse_pressed());
	overlay_manager_.update(delta_in_tick);

	if(request_cursor_update_){
		update_cursor();
	}

	root().update(delta_in_tick);
	current_time_ += delta_in_tick;
	current_frame_++;
}

void scene::draw_at(const elem& elem){
	auto c = get_region().intersection_with(elem.bound_abs());
	const auto bound = c.round<int>();

	for(unsigned i = 0; i < pass_config_.size(); ++i){
		renderer().update_state(pass_config_[i].begin_config);

		renderer().update_state({},
			fx::batch_draw_mode::def, graphic::draw::instruction::make_state_tag(fx::state_type::push_constant, 0x00000010)
		);


		elem.draw_layer(c, {i});

		if(pass_config_[i].end_config)renderer().update_state(fx::blit_config{
				.blit_region = {bound.src, bound.extent()},
				.pipe_info = pass_config_[i].end_config.value()
			});
	}


	if(auto tail = pass_config_.get_tail_blit())renderer().update_state(fx::blit_config{
			.blit_region = {bound.src, bound.extent()},
			.pipe_info = tail.value()
		});
}

void scene::draw(rect clip){
	renderer().init_projection();


	{
		gui::viewport_guard _{renderer(), region_};

		auto draw_elem = [&](const elem& e, rect region){
			draw_at(e);
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
	// renderer().update_state(pass_config_[i].begin_config);

	if(inputs_.is_cursor_inbound()){
		renderer().update_state(fx::pipeline_config{
				.draw_targets = {0b1},
				.pipeline_index = 0
			});

		renderer().update_state(
			{}, fx::batch_draw_mode::def,
			graphic::draw::instruction::make_state_tag(fx::state_type::push_constant, 0x00000010)
		);

		cursor_collection_.draw(*this);

	}


	//renderer().consume();

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
	//TODO provide cursor position?

	auto rng = get_inbounds();
	if(focus_scroll_){
		auto rst = focus_scroll_->on_scroll(e, {});
		if(rst == events::op_afterwards::intercepted)return;

		if(const auto itr = std::ranges::find(rng, focus_cursor_); itr == rng.end()){
			auto cur = std::reverse_iterator{itr};
			while(cur != rng.rend()){
				std::span aboves{cur.base(), rng.end()};
				if((*cur)->on_scroll(e, aboves) != events::op_afterwards::intercepted){
					++cur;
				}else{
					break;
				}
			}
		}
	}else{
		auto cur = rng.rbegin();
		while(cur != rng.rend()){
			std::span aboves{cur.base(), rng.end()};
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
		auto& container = inbounds_.get_bak();
		container.clear();

		for (auto && activeTooltip : tooltip_manager_.get_active_tooltips() | std::views::reverse){
			if(tooltip_manager_.is_below_scene(activeTooltip.element.get()))continue;
			util::dfs_record_inbound_element(get_cursor_pos(), container, activeTooltip.element.get());
			if(!container.empty())goto upt;
		}

		if(!overlay_manager_.empty()){
			auto& top = *std::ranges::rbegin(overlay_manager_);
			if(container.empty()){
				util::dfs_record_inbound_element(get_cursor_pos(), container, top.get());
			}
		}else{
			if(container.empty()){
				util::dfs_record_inbound_element(get_cursor_pos(), container, &root());
			}

			if(container.empty()){
				for (auto && activeTooltip : tooltip_manager_.get_active_tooltips() | std::views::reverse){
					if(!tooltip_manager_.is_below_scene(activeTooltip.element.get()))continue;
					util::dfs_record_inbound_element(get_cursor_pos(), container, activeTooltip.element.get());
					if(!container.empty())goto upt;
				}
			}
		}


		upt:

		update_inbounds();
	}

	if(!focus_cursor_) return;

	const auto rng = get_inbounds();
	const auto cursor_transform_delta = util::transform_scene2local(rng, {});
	const auto cursor_transformed = get_cursor_pos() + cursor_transform_delta;

	for(const auto& [i, state] : mouse_states_ | std::views::enumerate){
		if(!state.pressed) continue;

		auto src = state.src + cursor_transform_delta;
		auto dst = cursor_transformed;
		const auto mode = inputs_.main_binds.get_mode();
		events::key_set key{static_cast<std::uint16_t>(i), input_handle::act::ignore, mode};

		auto cur = rng.rbegin();
		while(cur != rng.rend()){
			if((*cur)->on_drag({src, dst, key}) != events::op_afterwards::intercepted){
				const auto delta = util::transform_current2parent(**cur, {});
				src += delta;
				dst += delta;
				++cur;
			}else{
				break;
			}
		}
	}

	focus_cursor_->on_cursor_moved(
		events::cursor_move{.src = inputs_.last_cursor_pos() + cursor_transform_delta, .dst = cursor_transformed});
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

	independent_layouts_.swap();
	while(root_->layout_state.is_children_changed() || !independent_layouts_.get_cur().empty()){
		for(const auto layout : independent_layouts_.get_cur()){
			layout->try_layout();
		}
		independent_layouts_.get_cur().clear();

		root_->try_layout();

		count++;
		if(count > 8){
			// break;
			throw std::runtime_error("Bad Layout: Iteration Too Many Times");
		}
	}
}

void scene::update_inbounds(){
	auto& frontend = inbounds_.get_cur();
	auto& backend = inbounds_.get_bak();

	if(frontend.size() == backend.size() && (frontend.empty() || frontend.back() == backend.back()))return;

	auto [i1, i2] = std::ranges::mismatch(frontend, backend);

	for(const auto& element : std::ranges::subrange{i1, frontend.end()} | std::views::reverse){
		element->on_inbound_changed(false, true);
	}

	auto itr = backend.begin();
	for(; itr != i2; ++itr){
		(*itr)->on_inbound_changed(true, false);
	}

	for(; itr != backend.end(); ++itr){
		(*itr)->on_inbound_changed(true, true);
		cursor_event_active_elems_.insert(*itr);
	}

	inbounds_.swap();

	try_swap_focus(inbounds_.get_cur().empty() ? nullptr : inbounds_.get_cur().back());
}

void scene::update_mouse_state(const input_handle::key_set k){
	using namespace input_handle;
	auto [c, a, m] = k;

	if(a == act::press && focus_key_ && !focus_key_->contains(get_cursor_pos())){
		focus_key_->on_focus_key_changed(false);
		focus_key_ = nullptr;
	}

	if(focus_cursor_){
		const auto rng = get_inbounds();
		const auto pos = util::transform_scene2local(rng, get_cursor_pos());
		events::click e{pos, k};

		auto cur = rng.rbegin();
		while(cur != rng.rend()){
			const std::span aboves{cur.base(), rng.end()};
			if((*cur)->on_click(e, aboves) != events::op_afterwards::intercepted){
				e.pos = util::transform_current2parent(**cur, e.pos);
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
	std::erase(inbounds_.get_bak(), target);
	std::erase(inbounds_.get_cur(), target);
	// asyncTaskOwners.erase(const_cast<elem*>(target));
	cursor_event_active_elems_.erase(const_cast<elem*>(target));
	independent_layouts_.get_bak().erase(const_cast<elem*>(target));
	layer_altitude_record_.erase(target->get_altitude());
	// erase_independent_draw(target);
	// erase_direct_access({}, target);
	// tooltipManager.requestDrop(*target);
}

void scene::update_elem_cursor_state_(float delta_in_tick) noexcept{
	cursor_event_active_elems_.modify_and_erase([=](elem* e){
		e->cursor_states_.update(delta_in_tick);
		e->draw_flag.set_self_draw_required(1);

		if(e->cursor_states_.focused){
			tooltip_manager_.try_append_tooltip(*e, false);
		}

		return e->cursor_states_.check_update_exitable();
	});
}

}
