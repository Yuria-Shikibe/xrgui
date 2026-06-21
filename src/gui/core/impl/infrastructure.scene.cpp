module;

#include <cassert>

module mo_yanxi.gui.infrastructure;

import :scene_input;
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

void scene_base::capture_mouse(elem& target, input_handle::mouse mouse_button_code, math::vec2 press_scene_pos){
	assert(is_on_scene_thread(*this));
	assert(std::addressof(target.get_scene()) == this);

	input_handler_.capture_mouse(target, mouse_button_code, press_scene_pos);
	request_cursor_update();
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
	const auto delta_in_tick_f = static_cast<float>(delta_in_tick);
	input_communicate_async_task_queue_.consume(static_cast<scene&>(*this));

	react_flow_.update();
	if(async_task_queue_)async_task_queue_->process_done();
	instant_task_queue_.consume();
	input_handler_.update_bindings(delta_in_tick_f);

	tooltip_manager_.update(delta_in_tick_f, get_cursor_pos(), input_handler_.is_mouse_pressed());
	overlay_manager_.update(delta_in_tick_f);

	if(input_handler_.cursor_update_requested()){
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


events::dispatch_result scene_base::handle_input_event(const input_handle::input_event_variant& event){
	assert(is_on_scene_thread(*this));
	using input_handle::input_event_type;

	switch(event.type){
	case input_event_type::input_key:{
		auto audio_request_transaction = input_handler_.make_audio_request_transaction();
		switch(input_handler_.handle_key_input(event.input_key)){
		case scene_submodule::input_key_result::handled:
			return events::dispatch_result::handled;
		case scene_submodule::input_key_result::esc_required:
			return on_esc();
		default:
			return events::dispatch_result::unhandled;
		}
	}
	case input_event_type::input_mouse:{
		auto audio_request_transaction = input_handler_.make_audio_request_transaction();
		input_handler_.inform_input(event.input_key);
		switch(overlay_manager_.handle_external_press(input_handler_.get_cursor_pos(), event.input_key)){
		case overlay_external_press_result::ignored:
			break;
		case overlay_external_press_result::intercepted:{
			auto [op, style] = input_handler_.update_cursor(overlay_manager_, tooltip_manager_, root());
			static_cast<void>(op);
			current_cursor_drawers_ = resources_->cursor_collection_manager.get_drawers(style);
			return events::dispatch_result::handled;
		}
		case overlay_external_press_result::retarget:{
			auto [op, style] = input_handler_.update_cursor(overlay_manager_, tooltip_manager_, root());
			static_cast<void>(op);
			current_cursor_drawers_ = resources_->cursor_collection_manager.get_drawers(style);
			break;
		}
		default:
			std::unreachable();
		}
		const auto rst = input_handler_.handle_mouse_input(event.input_key);
		update_cursor_type();
		return rst;
	}
	case input_event_type::input_scroll:{
		auto audio_request_transaction = input_handler_.make_audio_request_transaction();
		return input_handler_.handle_scroll(event.cursor);
	}
	case input_event_type::input_u32:{
		auto audio_request_transaction = input_handler_.make_audio_request_transaction();
		return input_handler_.handle_text_input(event.input_char);
	}
	case input_event_type::input_ime_composition:{
		auto audio_request_transaction = input_handler_.make_audio_request_transaction();
		return input_handler_.handle_ime_composition(event.ime_composition);
	}
	case input_event_type::cursor_move:{
		auto audio_request_transaction = input_handler_.make_audio_request_transaction();
		input_handler_.inform_cursor_move(event.cursor);
		auto [op, style] = input_handler_.update_cursor(overlay_manager_, tooltip_manager_, root());
		current_cursor_drawers_ = resources_->cursor_collection_manager.get_drawers(style);
		return op;
	}
	case input_event_type::cursor_inbound:
		input_handler_.input_inbound(event.is_inbound);
		return events::dispatch_result::handled;
	case input_event_type::focus_lost:
		input_handler_.on_focus_lost();
		request_cursor_update();
		return events::dispatch_result::handled;
	case input_event_type::frame_split:
		update(std::chrono::duration_cast<std::chrono::duration<double, std::ratio<1, 60>>>(
			event.frame_delta_time).count());
		return events::dispatch_result::handled;
	}
	std::unreachable();
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

	if(util::thoroughly_esc(input_handler_.esc_focus_candidate()) != events::dispatch_result::unhandled){
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

void scene::enable_forked_scene_tasks(bool enable){
	if(enable != (async_task_queue_ != nullptr)){
		if(enable){
			async_task_queue_ = std::make_unique<decltype(async_task_queue_)::element_type>(get_heap_allocator(), fork());
			platform::set_thread_attributes(async_task_queue_->get_async_task_thread().native_handle(), {
				                                .name = "xrgui ui async task",
				                                .priority = platform::thread_priority::normal
			                                });
		}else{
			async_task_queue_ = nullptr;
		}
	}
}
}
