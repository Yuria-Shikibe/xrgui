module;

#include <cassert>

module mo_yanxi.gui.infrastructure;

import std;
import mo_yanxi.platform.thread;
import mo_yanxi.gui.style.tree;

namespace mo_yanxi::gui{

namespace scene_submodule{

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

input_key_result input::on_key_input(input_handle::key_set key){
	inputs_.inform(key);

	if(key.action == input_handle::act::press && key.key_code == std::to_underlying(input_handle::key::esc)){
		return input_key_result::esc_required;
	}else{
		if(focus_key){
			if(focus_key->on_key_input(key) == events::op_afterwards::intercepted){
				return input_key_result::intercepted;
			}
		}

		for (auto value : inbounds_.get_cur() | std::views::reverse){
			if(value->on_key_input(key) == events::op_afterwards::intercepted){
				return input_key_result::intercepted;
			}
		}
	}

	return input_key_result::fall_through;
}

events::op_afterwards input::on_unicode_input(char32_t val) const{
	if(focus_key){
		return focus_key->on_unicode_input(val);
	}
	return events::op_afterwards::fall_through;
}

events::op_afterwards input::on_scroll(math::vec2 scroll) const{
	events::scroll e{scroll, inputs_.main_binds.get_mode()};
	//TODO provide cursor position?

	if(focus_scroll){
		auto rst = focus_scroll->on_scroll(e, {});
		if(rst == events::op_afterwards::intercepted)return events::op_afterwards::intercepted;
	}

	auto rng = get_inbounds();
	auto cur = rng.rbegin();
	while(cur != rng.rend()){
		std::span aboves{cur.base(), rng.end()};
		if((*cur)->on_scroll(e, aboves) != events::op_afterwards::intercepted){
			++cur;
		}else{
			return events::op_afterwards::intercepted;
		}
	}

	return events::op_afterwards::fall_through;
}

events::op_afterwards input::on_mouse_input(input_handle::key_set k){
	using namespace input_handle;
	auto [c, a, m] = k;

	auto result = events::op_afterwards::fall_through;
	if(focus_cursor){
		const std::span rng = inbounds_.get_cur();
		const auto pos = util::transform_scene2local(rng, inputs_.cursor_pos());
		events::click e{pos, k};

		auto cur = rng.rbegin();
		while(cur != rng.rend()){
			const std::span aboves{cur.base(), rng.end()};
			if((*cur)->on_click(e, aboves) != events::op_afterwards::intercepted){
				e.pos = util::transform_current2parent(**cur, e.pos);
				++cur;
			}else{
				result = events::op_afterwards::intercepted;
				if(a == act::press){
					if(last_inbound_click)last_inbound_click->on_last_clicked_changed(false);
					last_inbound_click = *cur;
					last_inbound_click->on_last_clicked_changed(true);
				}

				goto END;
			}
		}

		if(a == act::press){
			if(last_inbound_click)last_inbound_click->on_last_clicked_changed(false);
			last_inbound_click = nullptr;
		}

	END:
		(void)0;
	}

	if(a == act::press){
		mouse_states_[c].reset(inputs_.cursor_pos());
	}

	if(a == act::release){
		mouse_states_[c].clear();

		if(focus_cursor && focus_cursor->is_focus_extended_by_mouse()){
			request_cursor_update();
		}
	}

	return result;
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

input::cursor_update_result input::update_cursor(overlay_manager& overlays, tooltip::tooltip_manager& tooltips,
                                                 elem& scene_root){
	request_cursor_update_ = false;

	if(!(focus_cursor && is_mouse_pressed() && focus_cursor->is_focus_extended_by_mouse())){
		auto& container = inbounds_.get_bak();
		container.clear();

		for (auto && activeTooltip : tooltips.get_active_tooltips() | std::views::reverse){
			if(tooltips.is_below_scene(activeTooltip.element.get()))continue;
			util::dfs_record_inbound_element(get_cursor_pos(), container, activeTooltip.element.get());
			if(!container.empty())goto upt;
		}

		if(!overlays.empty()){
			const auto& top = *std::ranges::rbegin(overlays);
			util::dfs_record_inbound_element(get_cursor_pos(), container, top.get());
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

	if(!focus_cursor) return {events::op_afterwards::fall_through, style::cursor_style{style::cursor_type::regular}};


	const auto rng = get_inbounds();
	const auto cursor_transform_delta = util::transform_scene2local(rng, {});
	const auto cursor_transformed = get_cursor_pos() + cursor_transform_delta;

	for(const auto& [i, state] : mouse_states_ | std::views::enumerate){
		if(!state) continue;

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

	const auto rst = focus_cursor->on_cursor_moved(
		events::cursor_move{.src = inputs_.last_cursor_pos() + cursor_transform_delta, .dst = cursor_transformed});

	return {rst, get_cursor_style(cursor_transformed)};
}

style::cursor_style input::get_cursor_style(math::vec2 cursor_local_pos) const{
	if(!focus_cursor){
		return style::cursor_style{style::cursor_type::regular};
	}
	return focus_cursor->get_cursor_type(cursor_local_pos);
}

style::cursor_style input::get_cursor_style() const{
	if(!focus_cursor){
		return style::cursor_style{style::cursor_type::regular};
	}
	const auto cursor_transform_delta = util::transform_scene2local(get_inbounds(), {});
	const auto cursor_transformed = inputs_.cursor_pos() + cursor_transform_delta;

	return focus_cursor->get_cursor_type(cursor_transformed);
}

void scene_deleter::operator()(scene* ptr) noexcept{
	delete ptr;
}
}

std::thread::id exchange_scene_thread(scene& s, std::thread::id id){
	return std::exchange(s.ui_main_thread_id, id);
}

style::style_manager scene_resources::init_style_manager_() const{
	style::style_manager manager{};
	manager.reserve(64);
	manager.register_style<style::elem_style_drawer>(referenced_ptr<style::debug_elem_drawer>{std::in_place, style_config{}});
	return manager;
}

style::style_tree_manager scene_resources::init_style_tree_manager_() const{
	style::style_tree_manager manager{};
	manager.reserve(64);
	manager.register_style<elem>(style::make_tree_node_ptr(style::tree_leaf{
		[](const style::typed_draw_param<elem>&){}
	}));
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

	if(async_task_queue_)async_task_queue_->erase(target);
	action_queue_.erase(target);

	independent_layouts_.get_bak().erase(const_cast<elem*>(target));
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


void scene_base::update(double delta_in_tick){
	assert(is_on_scene_thread(*this));
	input_communicate_async_task_queue_.consume(static_cast<scene&>(*this));

	react_flow_.update();
	if(async_task_queue_)async_task_queue_->process_done();
	instant_task_queue_.consume();

	tooltip_manager_.update(delta_in_tick, get_cursor_pos(), input_handler_.is_mouse_pressed());
	overlay_manager_.update(delta_in_tick);

	if(input_handler_.request_cursor_update_){
		auto [op, style] = input_handler_.update_cursor(overlay_manager_, tooltip_manager_, root());
		current_cursor_drawers_ = resources_->cursor_collection_manager.get_drawers(style);

		apply_update_state_changes();
	}

	root().update(delta_in_tick);

	for (auto active_update_elem : active_update_elems_){
		active_update_elem.elem->update(delta_in_tick);
	}

	input_handler_.update_elem_cursor_state(delta_in_tick, tooltip_manager_);
	action_queue_.update(delta_in_tick);

	apply_update_state_changes();

	current_time_ += delta_in_tick;
	current_frame_++;
}



events::op_afterwards scene_base::on_esc(){
	if(tooltip_manager_.on_esc() != events::op_afterwards::fall_through){
		request_cursor_update();
		return events::op_afterwards::intercepted;
	}
	if(overlay_manager_.on_esc() != events::op_afterwards::fall_through){
		request_cursor_update();
		return events::op_afterwards::intercepted;
	}

	elem* focus = input_handler_.focus_key;
	if(!focus) focus = input_handler_.focus_cursor;

	if(util::thoroughly_esc(focus) != events::op_afterwards::fall_through){
		request_cursor_update();
		return events::op_afterwards::intercepted;
	}
	return events::op_afterwards::fall_through;
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
