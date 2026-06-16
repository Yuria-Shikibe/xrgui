module mo_yanxi.gui.infrastructure;

import :scene;
import mo_yanxi.gui.action.elem;
import mo_yanxi.input_handle;
import mo_yanxi.utility;

namespace mo_yanxi::gui{
overlay_create_result<elem> overlay_manager::push_back(const overlay_layout& layout, elem_ptr&& elem_ptr, bool fade_in){
	elem_ptr->element_channel_ = elem_tree_channel::overlay;

	if(fade_in){
		elem_ptr->set_propagate_opacity(0.f);
		elem_ptr->push_action<action::alpha_action>(10.f, nullptr, 1.f);
	}
	overlay& dlg = *overlays_.emplace(std::move(elem_ptr), layout);
	active_stack_.push_back(std::addressof(dlg));
	draw_sequence_.push_back(dlg.get());
	return {dlg};
}

void overlay_manager::truncate(active_container::iterator where){
	std::ranges::subrange rng{where, active_stack_.end()};
	for (overlay* dialog : rng){
		dialog->notify_operation(overlay_operation::dismiss);
		dialog->element->clear_scene_references_recursively();
		fading_overlays_.push_back({
			.dialog = dialog
		});
	}

	active_stack_.erase(where, active_stack_.end());
}

overlay_external_press_result overlay_manager::handle_external_press(
	const math::vec2 scene_position,
	const events::key_set key){
	if(active_stack_.empty() || key.action != input_handle::act::press){
		return overlay_external_press_result::ignored;
	}

	auto where = std::prev(active_stack_.end());
	const overlay& target = **where;
	if(target.layout_config.external_press_policy == overlay_external_press_policy::ignore
		|| target.get() == nullptr
		|| target.get()->bound_abs().contains_loose(scene_position)){
		return overlay_external_press_result::ignored;
	}

	const overlay_external_press_policy policy = target.layout_config.external_press_policy;
	this->truncate(where);
	if(policy == overlay_external_press_policy::dismiss_and_retarget_right_press
		&& key.as_mouse() == input_handle::mouse::RMB){
		return overlay_external_press_result::retarget;
	}
	return overlay_external_press_result::intercepted;
}

void overlay_manager::update(float delta_in_tick){
	modifiable_erase_if(fading_overlays_, [&, this](overlay_fading& fading){
		overlay& dialog = *fading.dialog;
		fading.duration -= delta_in_tick;
		if(fading.duration <= 0){
			std::erase(draw_sequence_, dialog.get());
			overlays_.erase(overlays_.get_iterator(fading.dialog));

			return true;
		}else{
			dialog.element->set_propagate_opacity(fading.duration / overlay_fading::fading_time);
			dialog.element->update(delta_in_tick);
			return false;
		}
	});

	for (overlay* value : active_stack_){
		if(value->layout_changed){
			value->layout_changed = false;
			value->update_bound(last_vp_);
			value->get()->try_layout();
		}
		value->get()->update(delta_in_tick);
	}

}


math::vec2 overlay::get_overlay_extent(const math::vec2 scene_viewport_size) const{
	using namespace layout;

	//TODO fill parent?

	auto ext = layout_config.extent;
	if(ext.width.type == size_category::passive){
		ext.width.value = scene_viewport_size.x * std::clamp(ext.width.value, 0.f, 1.f);
		ext.width.type = size_category::mastering;
	}

	if(ext.height.type == size_category::passive){
		ext.height.value = scene_viewport_size.y * std::clamp(ext.height.value, 0.f, 1.f);
		ext.height.type = size_category::mastering;
	}

	ext = ext.promote();
	const optional_mastering_extent validSz = ext.potential_max_size();
	element->restriction_extent = validSz;
	math::vec2 rst;

	if(ext.width.pending() || ext.height.pending()){
		auto sz = element->pre_acquire_size(validSz);
		rst = sz.value_or(scene_viewport_size);
	}else{
		rst = validSz.potential_extent();
	}

	element->set_prefer_extent(rst.inf_to0());
	return rst;
}

void overlay::update_bound(const rect scene_viewport) const{
	const auto sz = get_overlay_extent(scene_viewport.extent()) * layout_config.scaling;
	const auto off = layout_config.absolute_offset + layout_config.scaling_offset * scene_viewport.extent();

	rect allocated_region{tags::from_extent, off, sz};
	allocated_region = math::rect::fit_rect_within_bound(scene_viewport, allocated_region);
	const auto embed = align::transform_offset(layout_config.align, scene_viewport.extent(), allocated_region);

	element->set_rel_pos(embed);
	element->resize(allocated_region.extent());
	element->update_abs_src(scene_viewport.src);
	element->try_layout();
}

events::dispatch_result overlay_manager::on_esc() noexcept{
	for (overlay* elem : active_stack_ | std::views::reverse){
		if(util::thoroughly_esc(elem->element.get()) != events::dispatch_result::unhandled){
			return events::dispatch_result::handled;
		}
	}

	if(!active_stack_.empty()){
		truncate(std::prev(active_stack_.end()));
		return events::dispatch_result::handled;
	}

	return events::dispatch_result::unhandled;
}
}

