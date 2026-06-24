module;

#include <cassert>
#include <gch/small_vector.hpp>

module mo_yanxi.gui.infrastructure;

import :scene_input;
import std;

namespace mo_yanxi::gui{

namespace scene_submodule{

namespace{

[[nodiscard]] std::optional<sound::play_event> mouse_play_event(input_handle::act action) noexcept{
	switch(action){
	case input_handle::act::press:
		return sound::play_event::on_press;
	case input_handle::act::release:
		return sound::play_event::on_release;
	case input_handle::act::double_press:
		return sound::play_event::on_double_press;
	default:
		return std::nullopt;
	}
}

[[nodiscard]] sound::play_event state_play_event(
	const sound::state_family family,
	const bool after) noexcept{
	switch(family){
	case sound::state_family::toggle:
		return after ? sound::play_event::on_toggle_on : sound::play_event::on_toggle_off;
	case sound::state_family::disabled:
		return after ? sound::play_event::on_disable : sound::play_event::on_enable;
	}
	std::unreachable();
}

struct scene_event_router{
	using route_buffer = gch::small_vector<elem*, 16, mr::heap_allocator<elem*>>;

	[[nodiscard]] static route_buffer collect_ancestor_path(elem& target){
		auto path = route_buffer{target.get_scene().get_heap_allocator<elem*>()};
		for(elem* current = std::addressof(target); current != nullptr; current = current->parent()){
			path.push_back(current);
		}
		std::ranges::reverse(path);
		return path;
	}

	template <typename MakeEvent, typename Handler>
	[[nodiscard]] static events::event_control dispatch_path(
		std::span<elem* const> path,
		const events::gui_event_type type,
		MakeEvent&& make_event,
		Handler handler){
		events::event_control control{};
		if(path.empty()){
			return control;
		}

		elem* const target = path.back();

		auto invoke_one = [&](elem& current, const events::event_phase phase){
			events::event_context ctx{
				events::event_route{
					.type = type,
					.phase = phase,
					.target = target,
					.current = std::addressof(current),
					.path = path
				},
				control
			};
			auto event = std::invoke(make_event, current);
			std::invoke(handler, current, ctx, event);
		};

		for(elem* current : path){
			invoke_one(*current, events::event_phase::preview);
			if(control.propagation_stopped()){
				break;
			}
		}

		if(!control.propagation_stopped()){
			invoke_one(*target, events::event_phase::target);
		}

		if(!control.propagation_stopped() && path.size() > 1){
			for(auto current = std::next(path.rbegin()); current != path.rend(); ++current){
				invoke_one(**current, events::event_phase::bubble);
				if(control.propagation_stopped()){
					break;
				}
			}
		}

		if(!control.default_prevented()){
			invoke_one(*target, events::event_phase::default_action);
		}

		return control;
	}
};

}

events::gui_event_type scene_input_dispatcher::key_event_type(const input_handle::act action) noexcept{
	switch(action){
	case input_handle::act::release:
		return events::gui_event_type::key_up;
	case input_handle::act::repeat:
	case input_handle::act::continuous:
		return events::gui_event_type::key_repeat;
	default:
		return events::gui_event_type::key_down;
	}
}

events::gui_event_type scene_input_dispatcher::pointer_event_type(const input_handle::act action) noexcept{
	switch(action){
	case input_handle::act::release:
		return events::gui_event_type::pointer_release;
	case input_handle::act::double_press:
		return events::gui_event_type::pointer_click;
	default:
		return events::gui_event_type::pointer_press;
	}
}

scene_input_dispatcher::scene_input_dispatcher(input_state& state) noexcept
	: state_(state){
}

input_key_result scene_input_dispatcher::dispatch_key_input(input_handle::key_set key) const{
	state_.inputs_.inform(key);

	if(key.action == input_handle::act::press && key.key_code == std::to_underlying(input_handle::key::esc)){
		return input_key_result::esc_required;
	}

	if(!state_.focus_key_){
		return input_key_result::unhandled;
	}

	auto path = scene_event_router::collect_ancestor_path(*state_.focus_key_);
	auto route = std::span<elem* const>{path};
	auto audio_route = state_.make_audio_route_scope(route);
	const auto control = scene_event_router::dispatch_path(
		route,
		key_event_type(key.action),
		[&](elem&){
			return events::key_event{
					.key = key
				};
		},
		&elem::on_key);
	if(control.handled()){
		state_.play_audio_for_handled(control.handled_by(), sound::play_event::on_key);
		return input_key_result::handled;
	}
	return input_key_result::unhandled;
}

events::dispatch_result scene_input_dispatcher::dispatch_text_input(char32_t val) const{
	if(!state_.focus_key_){
		return events::dispatch_result::unhandled;
	}
	auto path = scene_event_router::collect_ancestor_path(*state_.focus_key_);
	auto route = std::span<elem* const>{path};
	auto audio_route = state_.make_audio_route_scope(route);
	const auto control = scene_event_router::dispatch_path(
		route,
		events::gui_event_type::text_input,
		[&](elem&){
			return events::text_event{
					.value = val
				};
		},
		&elem::on_text);
	if(control.handled()){
		state_.play_audio_for_handled(control.handled_by(), sound::play_event::on_text_input);
	}
	return control.result();
}

events::dispatch_result scene_input_dispatcher::dispatch_ime_composition(
	const input_handle::ime_composition_event& ime_event) const{
	if(!state_.focus_key_){
		return events::dispatch_result::unhandled;
	}
	auto path = scene_event_router::collect_ancestor_path(*state_.focus_key_);
	auto route = std::span<elem* const>{path};
	auto audio_route = state_.make_audio_route_scope(route);
	const auto control = scene_event_router::dispatch_path(
		route,
		events::gui_event_type::ime_composition,
		[&](elem&){
			return events::ime_event{
					.composition = std::addressof(ime_event)
				};
		},
		&elem::on_ime);
	if(control.handled()){
		state_.play_audio_for_handled(control.handled_by(), sound::play_event::on_ime_composition);
	}
	return control.result();
}

events::dispatch_result scene_input_dispatcher::dispatch_scroll(math::vec2 scroll) const{
	state_.inputs_.set_scroll_offset(scroll.x, scroll.y);
	const auto mode = state_.inputs_.main_binds.get_mode();
	const auto cursor_scene_pos = state_.inputs_.cursor_pos();

	auto dispatch_wheel = [&](std::span<elem* const> path){
		auto audio_route = state_.make_audio_route_scope(path);
		return scene_event_router::dispatch_path(
			path,
			events::gui_event_type::wheel,
			[&](elem& current){
				return events::wheel_event{
						.scene_pos = cursor_scene_pos,
						.local_pos = util::transform_scene2local(current, cursor_scene_pos),
						.delta = scroll,
						.mode = mode
					};
			},
			&elem::on_wheel);
	};

	if(state_.focus_scroll_){
		auto path = scene_event_router::collect_ancestor_path(*state_.focus_scroll_);
		const auto control = dispatch_wheel(std::span<elem* const>{path});
		if(control.handled()){
			state_.play_audio_for_handled(control.handled_by(), sound::play_event::on_scroll);
			return control.result();
		}
	}

	auto path = state_.get_inbounds();
	const auto control = dispatch_wheel(path);
	if(control.handled()){
		state_.play_audio_for_handled(control.handled_by(), sound::play_event::on_scroll);
	}
	return control.result();
}

events::dispatch_result scene_input_dispatcher::dispatch_pointer_button(input_handle::key_set k) const{
	using namespace input_handle;
	auto [c, a, m] = k;

	if(state_.has_passthrough_mouse_capture()){
		if(a == act::press){
			state_.set_mouse_capture(c, state_.inputs_.cursor_pos(), mouse_capture_owner::passthrough);
		}

		if(a == act::release){
			state_.clear_mouse_capture(c);
			if(!state_.has_passthrough_mouse_capture()){
				state_.request_cursor_update();
			}
		}

		return events::dispatch_result::unhandled;
	}

	const auto cursor_scene_pos = state_.inputs_.cursor_pos();
	if(auto* captured_target = state_.mouse_states_[c].capture_target();
		captured_target != nullptr && a == act::release){
		auto path = scene_event_router::collect_ancestor_path(*captured_target);
		auto route = std::span<elem* const>{path};
		auto audio_route = state_.make_audio_route_scope(route);
		auto control = scene_event_router::dispatch_path(
			route,
			pointer_event_type(a),
			[&](elem& current){
				return events::pointer_button_event{
						.scene_pos = cursor_scene_pos,
						.local_pos = util::transform_scene2local(current, cursor_scene_pos),
						.key = k
					};
			},
			&elem::on_pointer_button);
		if(const auto play_event = mouse_play_event(a); control.handled() && play_event){
			state_.play_audio_for_handled(control.handled_by(), *play_event);
		}
		if(!control.handled()){
			control.mark_handled(*captured_target);
		}
		state_.switch_last_inbound_click(control.handled() ? control.handled_by() : captured_target);
		state_.clear_mouse_capture(c);
		if(state_.focus_cursor_ && state_.focus_cursor_->is_focus_extended_by_mouse()){
			state_.request_cursor_update();
		}
		return control.result();
	}else if(state_.focus_cursor_){
		auto path = state_.inbounds_.get_cur();
		auto audio_route = state_.make_audio_route_scope(path);
		const auto control = scene_event_router::dispatch_path(
			path,
			pointer_event_type(a),
			[&](elem& current){
				return events::pointer_button_event{
						.scene_pos = cursor_scene_pos,
						.local_pos = util::transform_scene2local(current, cursor_scene_pos),
						.key = k
					};
			},
			&elem::on_pointer_button);
		if(control.handled()){
			if(const auto play_event = mouse_play_event(a)){
				state_.play_audio_for_handled(control.handled_by(), *play_event);
			}
			if(a == act::press || a == act::release){
				state_.switch_last_inbound_click(control.handled_by());
			}
		}else if(a == act::press){
			state_.switch_last_inbound_click(nullptr);
		}

		if(a == act::press){
			const auto owner = control.handled() ? mouse_capture_owner::ui : mouse_capture_owner::passthrough;
			state_.set_mouse_capture(c, cursor_scene_pos, owner, control.handled_by());
		}

		if(a == act::release){
			state_.clear_mouse_capture(c);
			if(state_.focus_cursor_ && state_.focus_cursor_->is_focus_extended_by_mouse()){
				state_.request_cursor_update();
			}
		}

		return control.result();
	}

	return events::dispatch_result::unhandled;
}

events::dispatch_result scene_input_dispatcher::dispatch_pointer_drag(
	const mouse_state& drag_state,
	const std::uint16_t button_code,
	std::span<elem* const> path) const{
	if(!drag_state.is_ui_owned()){
		return events::dispatch_result::unhandled;
	}

	std::array<math::vec2, 2> drag_points{drag_state.src, state_.get_cursor_pos()};
	const auto scene_src = drag_points[0];
	const auto scene_dst = drag_points[1];
	const auto mode = state_.inputs_.main_binds.get_mode();
	events::key_set key{button_code, input_handle::act::ignore, mode};

	auto dispatch_drag = [&](std::span<elem* const> route){
		auto audio_route = state_.make_audio_route_scope(route);
		return scene_event_router::dispatch_path(
			route,
			events::gui_event_type::pointer_drag,
			[&](elem& current){
				std::array points{scene_src, scene_dst};
				util::transform_scene2local(current, std::span<math::vec2>{points});
				return events::pointer_drag_event{
						.scene_src = scene_src,
						.scene_dst = scene_dst,
						.local_src = points[0],
						.local_dst = points[1],
						.key = key
					};
			},
			&elem::on_pointer_drag);
	};

	if(elem* captured_target = drag_state.capture_target()){
		auto route = scene_event_router::collect_ancestor_path(*captured_target);
		const auto control = dispatch_drag(std::span<elem* const>{route});
		if(control.handled()){
			state_.play_audio_for_handled(control.handled_by(), sound::play_event::on_drag);
		}
		return control.result();
	}

	if(path.empty()){
		return events::dispatch_result::unhandled;
	}

	const auto control = dispatch_drag(path);
	if(control.handled()){
		state_.play_audio_for_handled(control.handled_by(), sound::play_event::on_drag);
	}

	return control.result();
}


events::dispatch_result scene_input_dispatcher::dispatch_cursor_move(
	std::span<elem* const> path,
	std::span<const math::vec2, 2> local_points) const{
	(void)local_points;
	if(state_.focus_cursor_ == nullptr){
		return events::dispatch_result::unhandled;
	}

	scene_event_router::route_buffer fallback_path{state_.focus_cursor_->get_scene().get_heap_allocator<elem*>()};
	if(path.empty()){
		fallback_path = scene_event_router::collect_ancestor_path(*state_.focus_cursor_);
		path = std::span<elem* const>{fallback_path};
	}

	const auto scene_src = state_.inputs_.last_cursor_pos();
	const auto scene_dst = state_.get_cursor_pos();
	auto audio_route = state_.make_audio_route_scope(path);
	const auto control = scene_event_router::dispatch_path(
		path,
		events::gui_event_type::pointer_move,
		[&](elem& current){
			std::array points{scene_src, scene_dst};
			util::transform_scene2local(current, std::span<math::vec2>{points});
			return events::pointer_move_event{
					.scene_src = scene_src,
					.scene_dst = scene_dst,
					.local_src = points[0],
					.local_dst = points[1]
				};
		},
		&elem::on_pointer_move);
	return control.result();
}


void input_state::set_mouse_capture(
	const std::uint16_t button_code,
	const math::vec2 press_scene_pos,
	const mouse_capture_owner owner,
	elem* target) noexcept{
	const auto bit = mouse_bit(button_code);
	mouse_states_[button_code].reset(press_scene_pos, owner, target);
	mouse_pressed_mask_ |= bit;
	if(owner == mouse_capture_owner::passthrough){
		passthrough_mouse_mask_ |= bit;
	}else{
		passthrough_mouse_mask_ &= static_cast<mouse_mask_type>(~bit);
	}
}

void input_state::clear_mouse_capture(const std::uint16_t button_code) noexcept{
	const auto bit = mouse_bit(button_code);
	mouse_states_[button_code].clear();
	mouse_pressed_mask_ &= static_cast<mouse_mask_type>(~bit);
	passthrough_mouse_mask_ &= static_cast<mouse_mask_type>(~bit);
}

void input_state::clear_all_mouse_captures() noexcept{
	for(auto& state : mouse_states_){
		state.clear();
	}
	mouse_pressed_mask_ = {};
	passthrough_mouse_mask_ = {};
}

void input_state::drop_event_focus(const elem* target) noexcept{
	if(focus_scroll_ == target)focus_scroll_ = nullptr;
	if(focus_cursor_ == target)focus_cursor_ = nullptr;
	if(focus_key_ == target)focus_key_ = nullptr;
	if(last_inbound_click_ == target)last_inbound_click_ = nullptr;
}

void input_state::drop_elem(const elem* target) noexcept{
	drop_event_focus(target);
	std::erase(inbounds_.get_bak(), target);
	std::erase(inbounds_.get_cur(), target);
	cursor_event_active_elems_.erase(const_cast<elem*>(target));
	for(std::size_t index = 0; index < mouse_states_.size(); ++index){
		if(mouse_states_[index].target == target){
			clear_mouse_capture(static_cast<std::uint16_t>(index));
		}
	}
}

void input_state::set_scroll_focus(elem* element, const bool focus) noexcept{
	if(!focus && !is_scroll_focus(element)){
		return;
	}
	focus_scroll_ = focus ? element : nullptr;
}

void input_state::set_key_focus(elem* element, const bool focus){
	if(focus){
		switch_key_focus(element);
	}else if(is_key_focus(element)){
		switch_key_focus(nullptr);
	}
}

void input_state::capture_mouse(elem& target, input_handle::mouse mouse_button_code, math::vec2 press_scene_pos){
	auto& current_inbounds = inbounds_.get_cur();
	for(elem* previous : current_inbounds){
		if(previous != std::addressof(target)){
			previous->on_inbound_changed(false, true);
		}
	}

	current_inbounds.clear();
	current_inbounds.push_back(std::addressof(target));

	auto& next_inbounds = inbounds_.get_bak();
	next_inbounds.clear();
	next_inbounds.push_back(std::addressof(target));

	target.on_inbound_changed(true, true);
	cursor_event_active_elems_.insert(std::addressof(target));

	swap_focus(std::addressof(target));
	set_mouse_capture(
		std::to_underlying(mouse_button_code),
		press_scene_pos,
		mouse_capture_owner::ui,
		std::addressof(target));

	switch_last_inbound_click(std::addressof(target));
	target.cursor_states_.update_press(input_handle::key_set{mouse_button_code, input_handle::act::press});
}

input_state::audio_request_transaction::audio_request_transaction(const input_state& target) noexcept
	: owner(std::addressof(target)){
	owner->begin_audio_request_transaction();
}

input_state::audio_request_transaction::audio_request_transaction(audio_request_transaction&& other) noexcept
	: owner(std::exchange(other.owner, nullptr)){
}

input_state::audio_request_transaction& input_state::audio_request_transaction::operator=(
	audio_request_transaction&& other){
	if(this != std::addressof(other)){
		if(owner != nullptr){
			owner->end_audio_request_transaction();
		}
		owner = std::exchange(other.owner, nullptr);
	}
	return *this;
}

input_state::audio_request_transaction::~audio_request_transaction() noexcept(false){
	if(owner != nullptr){
		owner->end_audio_request_transaction();
	}
}

input_state::audio_route_scope::audio_route_scope(const input_state& target, std::span<elem* const> route) noexcept
	: owner(std::addressof(target)),
	  previous_route(target.current_audio_route_){
	owner->current_audio_route_ = route;
	if(!route.empty()){
		owner->audio_transaction_had_route_ = true;
	}
}

input_state::audio_route_scope::audio_route_scope(audio_route_scope&& other) noexcept
	: owner(std::exchange(other.owner, nullptr)),
	  previous_route(std::exchange(other.previous_route, {})){
}

input_state::audio_route_scope& input_state::audio_route_scope::operator=(audio_route_scope&& other) noexcept{
	if(this != std::addressof(other)){
		if(owner != nullptr){
			owner->current_audio_route_ = previous_route;
		}
		owner = std::exchange(other.owner, nullptr);
		previous_route = std::exchange(other.previous_route, {});
	}
	return *this;
}

input_state::audio_route_scope::~audio_route_scope(){
	if(owner != nullptr){
		owner->current_audio_route_ = previous_route;
	}
}

void input_state::begin_audio_request_transaction() const noexcept{
	if(audio_request_transaction_depth_ == 0){
		input_audio_fallback_.reset();
		semantic_audio_request_.reset();
		state_audio_deltas_.clear();
		audio_transaction_had_route_ = false;
		current_audio_route_ = {};
	}
	++audio_request_transaction_depth_;
}

void input_state::end_audio_request_transaction() const{
	assert(audio_request_transaction_depth_ > 0);
	if(audio_request_transaction_depth_ == 0){
		return;
	}
	--audio_request_transaction_depth_;
	if(audio_request_transaction_depth_ == 0){
		flush_audio_request_();
	}
}

void input_state::request_audio(
	const elem* element,
	const sound::play_event event,
	const sound::request_origin origin) const{
	if(element == nullptr || !element->scene_audio_auto_proxy){
		return;
	}
	if(!element->get_audio_asset_(event)){
		return;
	}

	auto request = audio_request{
		.element = element,
		.event = event,
		.origin = origin,
		.sequence = ++audio_request_sequence_
	};
	switch(origin){
	case sound::request_origin::input_fallback:
		input_audio_fallback_ = request;
		break;
	case sound::request_origin::semantic:
		semantic_audio_request_ = request;
		break;
	case sound::request_origin::state_delta:
		break;
	}

	if(audio_request_transaction_depth_ == 0){
		flush_audio_request_();
	}
}

void input_state::request_semantic_audio(const elem* element, const sound::play_event event) const{
	request_audio(element, event, sound::request_origin::semantic);
}

void input_state::record_state_audio_delta(
	const elem* element,
	const sound::state_family family,
	const bool before,
	const bool after) const{
	if(element == nullptr || before == after){
		return;
	}

	const bool on_event_route = std::ranges::find(current_audio_route_, element) != current_audio_route_.end();
	state_audio_deltas_.push_back(state_audio_delta{
		.element = element,
		.family = family,
		.before = before,
		.after = after,
		.on_event_route = on_event_route,
		.sequence = ++audio_request_sequence_
	});
	if(audio_request_transaction_depth_ == 0){
		flush_audio_request_();
	}
}

void input_state::update_elem_cursor_state(float delta_in_tick, tooltip::tooltip_manager& tooltip) noexcept{
	cursor_event_active_elems_.modify_and_erase([&](elem* e){
		e->cursor_states_.update(delta_in_tick);

		if(e->cursor_states_.focused){
			tooltip.try_append_tooltip(*e, false);
		}

		return e->cursor_states_.check_update_exitable();
	});
}

void input_state::play_audio_for_handled(const elem* element, sound::play_event event) const{
	if(element != nullptr && !element->is_disabled()){
		request_audio(element, event, sound::request_origin::input_fallback);
	}
}

void input_state::switch_last_inbound_click(elem* element){
	if(last_inbound_click_ == element)return;
	if(last_inbound_click_)last_inbound_click_->on_last_clicked_changed(false);
	last_inbound_click_ = element;
	if(last_inbound_click_)last_inbound_click_->on_last_clicked_changed(true);
}

void input_state::switch_key_focus(elem* element){
	if(focus_key_ == element)return;
	if(focus_key_)focus_key_->on_focus_key_changed(false);
	focus_key_ = element;
	if(focus_key_)focus_key_->on_focus_key_changed(true);
}

void input_state::try_swap_focus(){
	elem* newFocus = nullptr;
	auto rst = std::ranges::find_last_if(inbounds_.get_cur(), [](const elem* e){
		return e->is_interactable() && e->interactivity != interactivity_flag::children_only;
	});
	if(!rst.empty()){
		newFocus = *rst.begin();
	}

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

void input_state::swap_focus(elem* newFocus){
	if(focus_cursor_){
		clear_all_mouse_captures();
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

input_key_result input_state::handle_key_input(input_handle::key_set key){
	return scene_input_dispatcher{*this}.dispatch_key_input(key);
}

events::dispatch_result input_state::handle_text_input(char32_t val){
	return scene_input_dispatcher{*this}.dispatch_text_input(val);
}

events::dispatch_result input_state::handle_ime_composition(const input_handle::ime_composition_event& event){
	return scene_input_dispatcher{*this}.dispatch_ime_composition(event);
}

events::dispatch_result input_state::handle_scroll(math::vec2 scroll){
	return scene_input_dispatcher{*this}.dispatch_scroll(scroll);
}

events::dispatch_result input_state::handle_mouse_input(input_handle::key_set k){
	return scene_input_dispatcher{*this}.dispatch_pointer_button(k);
}

void input_state::on_focus_lost(){
	for(elem* element : inbounds_.get_cur()){
		element->on_inbound_changed(false, true);
		cursor_event_active_elems_.insert(element);
	}
	inbounds_.clear();

	focus_scroll_ = nullptr;
	swap_focus(nullptr);
	switch_key_focus(nullptr);
	switch_last_inbound_click(nullptr);

	clear_all_mouse_captures();
	inputs_.set_inbound(false);
	inputs_.reset_active_inputs();
	request_cursor_update();
}

void input_state::update_inbounds(){
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

	try_swap_focus();
}

input_state::cursor_update_result input_state::update_cursor(overlay_manager& overlays, tooltip::tooltip_manager& tooltips,
                                                 elem& scene_root){
	request_cursor_update_ = false;

	if(has_passthrough_mouse_capture()){
		return {events::dispatch_result::unhandled, style::cursor_style{style::cursor_type::regular}};
	}

	if(!(focus_cursor_ && is_mouse_pressed() && focus_cursor_->is_focus_extended_by_mouse())){
		auto& container = inbounds_.get_bak();
		container.clear();

		for (auto && activeTooltip : tooltips.get_active_tooltips() | std::views::reverse){
			if(tooltips.is_below_scene(activeTooltip.element.get()))continue;
			util::dfs_record_inbound_element(get_cursor_pos(), container, activeTooltip.element.get());
			if(!container.empty())goto upt;
		}

		if(const overlay* top = overlays.top_active_overlay()){
			util::dfs_record_inbound_element(get_cursor_pos(), container, top->get());
		}else{
			if(container.empty()){
				util::dfs_record_inbound_element(get_cursor_pos(), container, &scene_root);
			}

			if(container.empty()){
				for (auto && activeTooltip : tooltips.get_active_tooltips() | std::views::reverse){
					if(!tooltips.is_below_scene(activeTooltip.element.get()))continue;
					util::dfs_record_inbound_element(get_cursor_pos(), container, activeTooltip.element.get());
					if(!container.empty())goto upt;
				}
			}
		}


	upt:

		update_inbounds();
	}

	if(!focus_cursor_) return {events::dispatch_result::unhandled, style::cursor_style{style::cursor_type::regular}};

	const auto rng = get_inbounds();
	std::array cursor_points{inputs_.last_cursor_pos(), get_cursor_pos()};
	util::transform_scene2local(rng, std::span<math::vec2>{cursor_points});
	scene_input_dispatcher dispatcher{*this};

	for(const auto& [i, state] : mouse_states_ | std::views::enumerate){
		if(!state.is_ui_owned()) continue;
		static_cast<void>(dispatcher.dispatch_pointer_drag(state, static_cast<std::uint16_t>(i), rng));
	}

	const auto rst = dispatcher.dispatch_cursor_move(
		rng,
		std::span<const math::vec2, 2>{cursor_points});

	return {rst, get_cursor_style(cursor_points[1])};
}

style::cursor_style input_state::get_cursor_style(math::vec2 cursor_local_pos) const{
	if(has_passthrough_mouse_capture() || !focus_cursor_){
		return style::cursor_style{style::cursor_type::regular};
	}
	return focus_cursor_->get_cursor_type(cursor_local_pos);
}

style::cursor_style input_state::get_cursor_style() const{
	if(has_passthrough_mouse_capture() || !focus_cursor_){
		return style::cursor_style{style::cursor_type::regular};
	}
	auto cursor_transformed = inputs_.cursor_pos();
	util::transform_scene2local(get_inbounds(), std::span{&cursor_transformed, 1});

	return focus_cursor_->get_cursor_type(cursor_transformed);
}

void input_state::flush_audio_request_() const{
	auto& net_deltas = net_state_deltas_;
	net_deltas.clear();
	net_deltas.reserve(state_audio_deltas_.size());
	for(const auto& delta : state_audio_deltas_){
		auto existing = std::ranges::find_if(net_deltas, [&](const net_state_delta& value){
			return value.element == delta.element && value.family == delta.family;
		});
		if(existing == net_deltas.end()){
			net_deltas.push_back(net_state_delta{
				.element = delta.element,
				.family = delta.family,
				.before = delta.before,
				.after = delta.after,
				.on_event_route = delta.on_event_route,
				.sequence = delta.sequence
			});
		}else{
			existing->after = delta.after;
			existing->on_event_route = existing->on_event_route || delta.on_event_route;
			existing->sequence = delta.sequence;
		}
	}

	struct family_summary{
		bool enter{};
		bool exit{};
	};
	std::array<family_summary, 2> family_summaries{};

	auto is_delta_eligible = [&](const net_state_delta& delta) noexcept{
		if(delta.before == delta.after){
			return false;
		}
		if(audio_transaction_had_route_ && !delta.on_event_route){
			return false;
		}
		return true;
	};

	for(const auto& delta : net_deltas){
		if(delta.before == delta.after){
			continue;
		}
		auto& summary = family_summaries[std::to_underlying(delta.family)];
		if(delta.after){
			summary.enter = true;
		}else{
			summary.exit = true;
		}
	}

	auto state_request = std::optional<audio_request>{};
	for(const auto& delta : net_deltas){
		if(!is_delta_eligible(delta) || delta.element == nullptr || !delta.element->scene_audio_auto_proxy){
			continue;
		}
		const auto& summary = family_summaries[std::to_underlying(delta.family)];
		if(summary.enter && summary.exit){
			continue;
		}

		const auto event = state_play_event(delta.family, delta.after);
		if(!delta.element->get_audio_asset_(event)){
			continue;
		}
		if(!state_request || delta.sequence >= state_request->sequence){
			state_request = audio_request{
				.element = delta.element,
				.event = event,
				.origin = sound::request_origin::state_delta,
				.sequence = delta.sequence
			};
		}
	}

	auto request = semantic_audio_request_;
	if(!request){
		request = state_request;
	}
	if(!request){
		request = input_audio_fallback_;
	}

	input_audio_fallback_.reset();
	semantic_audio_request_.reset();
	state_audio_deltas_.clear();
	net_deltas.clear();
	audio_transaction_had_route_ = false;
	current_audio_route_ = {};

	if(!request || request->element == nullptr){
		return;
	}
	static_cast<void>(request->element->play_audio_detached(request->event));
}

}

}
