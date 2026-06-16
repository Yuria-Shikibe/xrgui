module;

#include <cassert>

module mo_yanxi.gui.infrastructure;

import std;
import mo_yanxi.platform.thread;
import mo_yanxi.gui.style.tree;

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

struct scene_event_router{
	[[nodiscard]] static mr::heap_vector<elem*> collect_ancestor_path(elem& target){
		auto path = mr::heap_vector<elem*>{target.get_scene().get_heap_allocator<elem*>()};
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

input::audio_request_transaction::audio_request_transaction(const input& target) noexcept
	: owner(std::addressof(target)){
	owner->begin_audio_request_transaction();
}

input::audio_request_transaction::audio_request_transaction(audio_request_transaction&& other) noexcept
	: owner(std::exchange(other.owner, nullptr)){
}

input::audio_request_transaction& input::audio_request_transaction::operator=(
	audio_request_transaction&& other){
	if(this != std::addressof(other)){
		if(owner != nullptr){
			owner->end_audio_request_transaction();
		}
		owner = std::exchange(other.owner, nullptr);
	}
	return *this;
}

input::audio_request_transaction::~audio_request_transaction() noexcept(false){
	if(owner != nullptr){
		owner->end_audio_request_transaction();
	}
}

void input::play_audio_for_handled(const elem* element, sound::play_event event) const{
	if(element != nullptr && !element->is_disabled()){
		request_audio(element, event, sound::play_priority::input);
	}
}

void input::begin_audio_request_transaction() const noexcept{
	++audio_request_transaction_depth_;
}

void input::end_audio_request_transaction() const{
	assert(audio_request_transaction_depth_ > 0);
	if(audio_request_transaction_depth_ == 0){
		return;
	}
	--audio_request_transaction_depth_;
	if(audio_request_transaction_depth_ == 0){
		flush_audio_request_();
	}
}

void input::request_audio(
	const elem* element,
	const sound::play_event event,
	const sound::play_priority priority) const{
	if(element == nullptr || !element->scene_audio_auto_proxy){
		return;
	}
	if(!element->get_audio_asset_(event)){
		if(pending_audio_request_
			&& std::to_underlying(priority) >= std::to_underlying(pending_audio_request_->priority)){
			pending_audio_request_.reset();
		}
		return;
	}

	if(pending_audio_request_
		&& std::to_underlying(priority) < std::to_underlying(pending_audio_request_->priority)){
		return;
	}

	pending_audio_request_ = audio_request{
		.element = element,
		.event = event,
		.priority = priority
	};
	if(audio_request_transaction_depth_ == 0){
		flush_audio_request_();
	}
}

void input::flush_audio_request_() const{
	auto request = std::exchange(pending_audio_request_, std::nullopt);
	if(!request || request->element == nullptr){
		return;
	}
	static_cast<void>(request->element->play_audio_detached(request->event));
}

void action_queue::update(float delta_in_tick) noexcept{
	if(auto cont = pendings.fetch()){
		for (auto value : *cont){
			active.insert(value);
		}
	}

	active.modify_and_erase([&](elem* p){
		return p->update_action(delta_in_tick);
	});
}

void action_queue::erase(const elem* target){
	if(target->has_pending_action()){
		pendings.modify([&](decltype(pendings)::container_type& cont){
			using std::erase;
			erase(cont, const_cast<elem*>(target));
		});
		active.erase(const_cast<elem*>(target));
	}else if(target->has_consuming_action()){
		active.erase(const_cast<elem*>(target));
	}
}


void input::update_elem_cursor_state(float delta_in_tick, tooltip::tooltip_manager& tooltip) noexcept{
	cursor_event_active_elems_.modify_and_erase([&](elem* e){
		e->cursor_states_.update(delta_in_tick);

		if(e->cursor_states_.focused){
			tooltip.try_append_tooltip(*e, false);
		}

		return e->cursor_states_.check_update_exitable();
	});
}


void input::switch_key_focus(elem* element){
	if(focus_key == element)return;
	if(focus_key)focus_key->on_focus_key_changed(false);
	focus_key = element;
	if(focus_key)focus_key->on_focus_key_changed(true);
}

void input::switch_last_inbound_click(elem* element){
	if(last_inbound_click == element)return;
	if(last_inbound_click)last_inbound_click->on_last_clicked_changed(false);
	last_inbound_click = element;
	if(last_inbound_click)last_inbound_click->on_last_clicked_changed(true);
}

void input::try_swap_focus(){
	elem* newFocus = nullptr;
	auto rst = std::ranges::find_last_if(inbounds_.get_cur(), [](const elem* e){
		return e->is_interactable() && e->interactivity != interactivity_flag::children_only;
	});
	if(!rst.empty()){
		newFocus = *rst.begin();
	}

	if(newFocus == focus_cursor) return;


	if(focus_cursor){
		if(focus_cursor->is_focus_extended_by_mouse()){
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

void input::swap_focus(elem* newFocus){
	if(focus_cursor){
		for(auto& state : mouse_states_){
			state.clear();
		}
		focus_cursor->on_focus_changed(false);
		focus_cursor->cursor_states_.quit_focus();
	}

	focus_cursor = newFocus;

	if(focus_cursor){
		if(focus_cursor->is_interactable()){
			focus_cursor->on_focus_changed(true);
		}
	}
}

scene_input_dispatcher::scene_input_dispatcher(input& state) noexcept
	: state_(state){
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

input_key_result scene_input_dispatcher::dispatch_key_input(input_handle::key_set key) const{
	state_.inputs_.inform(key);

	if(key.action == input_handle::act::press && key.key_code == std::to_underlying(input_handle::key::esc)){
		return input_key_result::esc_required;
	}

	if(!state_.focus_key){
		return input_key_result::unhandled;
	}

	auto path = scene_event_router::collect_ancestor_path(*state_.focus_key);
	const auto control = scene_event_router::dispatch_path(
		std::span<elem* const>{path},
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

input_key_result input::handle_key_input(input_handle::key_set key){
	return scene_input_dispatcher{*this}.dispatch_key_input(key);
}

events::dispatch_result scene_input_dispatcher::dispatch_text_input(char32_t val) const{
	if(!state_.focus_key){
		return events::dispatch_result::unhandled;
	}
	auto path = scene_event_router::collect_ancestor_path(*state_.focus_key);
	const auto control = scene_event_router::dispatch_path(
		std::span<elem* const>{path},
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

events::dispatch_result input::handle_text_input(char32_t val){
	return scene_input_dispatcher{*this}.dispatch_text_input(val);
}

events::dispatch_result scene_input_dispatcher::dispatch_ime_composition(
	const input_handle::ime_composition_event& ime_event) const{
	if(!state_.focus_key){
		return events::dispatch_result::unhandled;
	}
	auto path = scene_event_router::collect_ancestor_path(*state_.focus_key);
	const auto control = scene_event_router::dispatch_path(
		std::span<elem* const>{path},
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

events::dispatch_result input::handle_ime_composition(const input_handle::ime_composition_event& event){
	return scene_input_dispatcher{*this}.dispatch_ime_composition(event);
}

events::dispatch_result scene_input_dispatcher::dispatch_scroll(math::vec2 scroll) const{
	state_.inputs_.set_scroll_offset(scroll.x, scroll.y);
	const auto mode = state_.inputs_.main_binds.get_mode();
	const auto cursor_scene_pos = state_.inputs_.cursor_pos();

	auto dispatch_wheel = [&](std::span<elem* const> path){
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

	if(state_.focus_scroll){
		auto path = scene_event_router::collect_ancestor_path(*state_.focus_scroll);
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

events::dispatch_result input::handle_scroll(math::vec2 scroll){
	return scene_input_dispatcher{*this}.dispatch_scroll(scroll);
}

void input::on_focus_lost(){
	for(elem* element : inbounds_.get_cur()){
		element->on_inbound_changed(false, true);
		cursor_event_active_elems_.insert(element);
	}
	inbounds_.clear();

	focus_scroll = nullptr;
	swap_focus(nullptr);
	switch_key_focus(nullptr);
	switch_last_inbound_click(nullptr);

	for(auto& state : mouse_states_){
		state.clear();
	}
	inputs_.set_inbound(false);
	inputs_.reset_active_inputs();
	request_cursor_update();
}

events::dispatch_result scene_input_dispatcher::dispatch_pointer_button(input_handle::key_set k) const{
	using namespace input_handle;
	auto [c, a, m] = k;

	if(state_.has_passthrough_mouse_capture()){
		if(a == act::press){
			state_.mouse_states_[c].reset(state_.inputs_.cursor_pos(), mouse_capture_owner::passthrough);
		}

		if(a == act::release){
			state_.mouse_states_[c].clear();
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
		auto control = scene_event_router::dispatch_path(
			std::span<elem* const>{path},
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
		state_.mouse_states_[c].clear();
		if(state_.focus_cursor && state_.focus_cursor->is_focus_extended_by_mouse()){
			state_.request_cursor_update();
		}
		return control.result();
	}else if(state_.focus_cursor){
		auto path = state_.inbounds_.get_cur();
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
			state_.mouse_states_[c].reset(cursor_scene_pos, owner, control.handled_by());
		}

		if(a == act::release){
			state_.mouse_states_[c].clear();
			if(state_.focus_cursor && state_.focus_cursor->is_focus_extended_by_mouse()){
				state_.request_cursor_update();
			}
		}

		return control.result();
	}

	return events::dispatch_result::unhandled;
}

events::dispatch_result input::handle_mouse_input(input_handle::key_set k){
	return scene_input_dispatcher{*this}.dispatch_pointer_button(k);
}

void input::update_inbounds(){
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
	if(state_.focus_cursor == nullptr){
		return events::dispatch_result::unhandled;
	}

	mr::heap_vector<elem*> fallback_path{};
	if(path.empty()){
		fallback_path = scene_event_router::collect_ancestor_path(*state_.focus_cursor);
		path = std::span<elem* const>{fallback_path};
	}

	const auto scene_src = state_.inputs_.last_cursor_pos();
	const auto scene_dst = state_.get_cursor_pos();
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

input::cursor_update_result input::update_cursor(overlay_manager& overlays, tooltip::tooltip_manager& tooltips,
                                                 elem& scene_root){
	request_cursor_update_ = false;

	if(has_passthrough_mouse_capture()){
		return {events::dispatch_result::unhandled, style::cursor_style{style::cursor_type::regular}};
	}

	if(!(focus_cursor && is_mouse_pressed() && focus_cursor->is_focus_extended_by_mouse())){
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

	if(!focus_cursor) return {events::dispatch_result::unhandled, style::cursor_style{style::cursor_type::regular}};

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

style::cursor_style input::get_cursor_style(math::vec2 cursor_local_pos) const{
	if(has_passthrough_mouse_capture() || !focus_cursor){
		return style::cursor_style{style::cursor_type::regular};
	}
	return focus_cursor->get_cursor_type(cursor_local_pos);
}

style::cursor_style input::get_cursor_style() const{
	if(has_passthrough_mouse_capture() || !focus_cursor){
		return style::cursor_style{style::cursor_type::regular};
	}
	auto cursor_transformed = inputs_.cursor_pos();
	util::transform_scene2local(get_inbounds(), std::span{&cursor_transformed, 1});

	return focus_cursor->get_cursor_type(cursor_transformed);
}

void scene_deleter::operator()(scene* ptr) noexcept{
	delete ptr;
}
}

std::thread::id exchange_scene_thread(scene& s, std::thread::id id){
	return std::exchange(s.ui_main_thread_id, id);
}

style::style_tree_manager scene_resources::init_style_tree_manager_() const{
	style::style_tree_manager manager{};
	manager.reserve(64);
	return manager;
}


scene_base::scene_base(scene_resources& resources, renderer_frontend&& renderer): scene_shared_resources(resources), renderer_(std::move(renderer)){
	platform::set_thread_attributes(
		react_flow_.get_async_working_thread().native_handle(), {
			.name = "xrgui react flow",
			.priority = platform::thread_priority::normal
		});
}

void scene_base::drop_(const elem* target) noexcept{
	drop_elem_nodes(target);
	input_handler_.drop_elem(target);


	target->drop_tooltip();

	instant_task_queue_.erase(target);
	active_update_elems_.erase({const_cast<elem*>(target)});
	std::erase(active_update_elems_state_changes, const_cast<elem*>(target));

	if(async_task_queue_)async_task_queue_->cancel_owner(target);
	action_queue_.erase(target);

	independent_layouts_.get_bak().erase(const_cast<elem*>(target));
}

void scene_base::retire_elem(elem* target) noexcept{
	assert(target != nullptr);
	assert(is_on_scene_thread(*this));
	assert(std::addressof(target->get_scene()) == this);

	target->detach_from_scene_recursively();
	if(target->has_external_refs_()){
		retired_elements_.push_back({target});
	}else{
		target->mark_destroying_no_external_refs_();
		target->deleter_(target);
	}
}

void scene_base::collect_retired_elements() noexcept{
	assert(is_on_scene_thread(*this));

	for(std::size_t index = 0; index < retired_elements_.size();){
		elem* target = retired_elements_[index].element;
		assert(target != nullptr);
		if(target->has_external_refs_()){
			++index;
			continue;
		}

		retired_elements_[index] = retired_elements_.back();
		retired_elements_.pop_back();
		target->mark_destroying_no_external_refs_();
		target->deleter_(target);
	}
}

void scene_base::resize(const math::frect region){
	assert(is_on_scene_thread(*this));
	if(util::try_modify(region_, region)){
		renderer().resize(region);
		root().resize(region.extent());
		overlay_manager_.resize(region);
	}
}

void scene_base::update_cursor_type(){
	current_cursor_drawers_ = resources_->cursor_collection_manager.get_drawers(input_handler_.get_cursor_style());
}

void scene_base::capture_mouse(elem& target, input_handle::mouse mouse_button_code, math::vec2 press_scene_pos){
	assert(is_on_scene_thread(*this));
	assert(std::addressof(target.get_scene()) == this);

	auto& current_inbounds = input_handler_.inbounds_.get_cur();
	for(elem* previous : current_inbounds){
		if(previous != std::addressof(target)){
			previous->on_inbound_changed(false, true);
		}
	}

	current_inbounds.clear();
	current_inbounds.push_back(std::addressof(target));

	auto& next_inbounds = input_handler_.inbounds_.get_bak();
	next_inbounds.clear();
	next_inbounds.push_back(std::addressof(target));

	target.on_inbound_changed(true, true);
	input_handler_.cursor_event_active_elems_.insert(std::addressof(target));

	input_handler_.swap_focus(std::addressof(target));
	input_handler_.mouse_states_[std::to_underlying(mouse_button_code)].reset(
		press_scene_pos,
		mouse_capture_owner::ui,
		std::addressof(target));

	if(input_handler_.last_inbound_click != std::addressof(target)){
		if(input_handler_.last_inbound_click){
			input_handler_.last_inbound_click->on_last_clicked_changed(false);
		}
		input_handler_.last_inbound_click = std::addressof(target);
		input_handler_.last_inbound_click->on_last_clicked_changed(true);
	}

	target.cursor_states_.update_press(input_handle::key_set{mouse_button_code, input_handle::act::press});
	request_cursor_update();
}


void scene_base::update(double delta_in_tick){
	assert(is_on_scene_thread(*this));
	const auto delta_in_tick_f = static_cast<float>(delta_in_tick);
	native_gui_callbacks_->consume();
	input_communicate_async_task_queue_.consume(static_cast<scene&>(*this));

	react_flow_.update();
	if(async_task_queue_)async_task_queue_->process_done();
	instant_task_queue_.consume();
	input_handler_.inputs_.update(delta_in_tick_f);

	tooltip_manager_.update(delta_in_tick_f, get_cursor_pos(), input_handler_.is_mouse_pressed());
	overlay_manager_.update(delta_in_tick_f);

	if(input_handler_.request_cursor_update_){
		auto [op, style] = input_handler_.update_cursor(overlay_manager_, tooltip_manager_, root());
		current_cursor_drawers_ = resources_->cursor_collection_manager.get_drawers(style);

		apply_update_state_changes();
	}

	root().update(delta_in_tick_f);

	for (auto active_update_elem : active_update_elems_){
		active_update_elem.elem->update(delta_in_tick_f);
	}

	input_handler_.update_elem_cursor_state(delta_in_tick_f, tooltip_manager_);
	action_queue_.update(delta_in_tick_f);

	apply_update_state_changes();

	current_time_ += delta_in_tick;
	current_frame_++;
	collect_retired_elements();
}



events::dispatch_result scene_base::on_esc(){
	if(tooltip_manager_.on_esc() != events::dispatch_result::unhandled){
		request_cursor_update();
		return events::dispatch_result::handled;
	}
	if(overlay_manager_.on_esc() != events::dispatch_result::unhandled){
		request_cursor_update();
		return events::dispatch_result::handled;
	}

	elem* focus = input_handler_.focus_key;
	if(!focus) focus = input_handler_.focus_cursor;

	if(util::thoroughly_esc(focus) != events::dispatch_result::unhandled){
		request_cursor_update();
		return events::dispatch_result::handled;
	}
	return events::dispatch_result::unhandled;
}


void scene_base::layout(){
	assert(is_on_scene_thread(*this));
	std::size_t count{};

	independent_layouts_.swap();
	while(root().layout_state.is_children_changed() || !independent_layouts_.get_cur().empty()){
		for(const auto layout : independent_layouts_.get_cur()){
			layout->try_layout();
		}
		independent_layouts_.get_cur().clear();

		root().try_layout();

		count++;
		if(count > 8){

			throw std::runtime_error("Bad Layout: Iteration Too Many Times");
		}
	}

	if(count){
		request_cursor_update();
	}
}


void scene::init_root() const{
	scene_root_->element_channel_ = elem_tree_channel::regular;
}

void scene::enable_elem_async_task_post(bool enable){
	if(enable != (async_task_queue_ != nullptr)){
		if(enable){
			async_task_queue_ = std::make_unique<decltype(async_task_queue_)::element_type>(get_heap_allocator(), fork());
			platform::set_thread_attributes(async_task_queue_->get_element_async_task_thread().native_handle(), {
				                                .name = "xrgui ui async task",
				                                .priority = platform::thread_priority::normal
			                                });
		}else{
			async_task_queue_ = nullptr;
		}
	}
}
}
