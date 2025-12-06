module mo_yanxi.gui.infrastructure;

import :scene;
import mo_yanxi.gui.action.generic;
import mo_yanxi.utility;

namespace mo_yanxi::gui{
overlay_create_result<elem> overlay_manager::push_back(const overlay_layout& layout, elem_ptr&& elem_ptr, bool fade_in){
	if(fade_in){
		elem_ptr->update_context_opacity(0.f);
		elem_ptr->push_action<action::alpha_action>(10, nullptr, 1.);
	}
	overlay& dlg = overlays_.emplace_back(overlay{std::move(elem_ptr), layout});
	draw_sequence_.push_back(dlg.get());
	return {dlg};
}

void overlay_manager::truncate(container::iterator where){
	std::ranges::subrange rng{where, overlays_.end()};
	for (const auto & dialog : rng){
		dialog.element->clear_scene_references_recursively();
	}

	std::ranges::move(
		rng | std::views::transform([](auto&& v){
			return overlay_fading{std::move(v)};
		}), std::back_inserter(overlay_fadings_));

	overlays_.erase(where, overlays_.end());
}

void overlay_manager::update(float delta_in_tick){
	modifiable_erase_if(overlay_fadings_, [&, this](overlay_fading& dialog){
		dialog.duration -= delta_in_tick;
		if(dialog.duration <= 0){
			std::erase(draw_sequence_, dialog.get());

			return true;
		}else{
			dialog.element->update_context_opacity(dialog.duration / overlay_fading::fading_time);
			return false;
		}
	});

	for (auto& value : overlays_){
		if(value.layout_changed){
			value.layout_changed = false;
			value.update_bound(last_vp_);
			value.get()->try_layout();
		}
		value.get()->update(delta_in_tick);
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

	if(ext.width.pending() || ext.height.pending()){
		auto sz = element->pre_acquire_size(validSz);
		return sz.value_or(scene_viewport_size);
	}else{
		return validSz.potential_extent();
	}
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

events::op_afterwards overlay_manager::on_esc() noexcept{
	for (auto&& elem : overlays_ | std::views::reverse){
		if(util::thoroughly_esc(elem.element.get()) != events::op_afterwards::fall_through){
			return events::op_afterwards::intercepted;
		}
	}

	if(!overlays_.empty()){
		truncate(std::prev(overlays_.end()));
		return events::op_afterwards::intercepted;
	}

	return events::op_afterwards::fall_through;
}
}

