module;

#include <cmath>

export module mo_yanxi.gui.compound.pie_menu;

import std;

export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.gui.elem.group;

import mo_yanxi.gui.fx;
import mo_yanxi.gui.style.variant;
import mo_yanxi.graphic.color;
import mo_yanxi.graphic.g2d;
import mo_yanxi.graphic.g2d.fringe;
import mo_yanxi.math;
import mo_yanxi.react_flow.common;

namespace mo_yanxi::gui::cpd{

export inline constexpr std::size_t blender_pie_slot_count = 8;

export
[[nodiscard]] constexpr math::vec2 blender_pie_slot_direction(const std::size_t index) noexcept{
	constexpr float diag = .7071067811865476f;
	switch(index % blender_pie_slot_count){
	case 0: return {-1.f, 0.f};
	case 1: return {1.f, 0.f};
	case 2: return {0.f, 1.f};
	case 3: return {0.f, -1.f};
	case 4: return {-diag, -diag};
	case 5: return {diag, -diag};
	case 6: return {-diag, diag};
	case 7: return {diag, diag};
	default: std::unreachable();
	}
}

export
struct pie_menu_item{
	elem_ptr element{};
	std::move_only_function<void()> action{};
	bool disabled{};
};

export
struct pie_menu_item_config{
	std::move_only_function<void()> action{};
	bool disabled{};
};

export
template <std::derived_from<elem> E, typename... Args>
	requires (std::constructible_from<E, scene&, elem*, Args&&...>)
[[nodiscard]] pie_menu_item emplace_pie_menu_item(
	scene& scene,
	elem* parent,
	pie_menu_item_config config = {},
	Args&&... args){
	return pie_menu_item{
		.element = elem_ptr{scene, parent, std::in_place_type<E>, std::forward<Args>(args)...},
		.action = std::move(config.action),
		.disabled = config.disabled
	};
}

export
template <typename Fn, typename... Args>
	requires (!std::same_as<std::remove_cvref_t<Fn>, pie_menu_item_config> && invocable_elem_init_func<Fn>)
[[nodiscard]] pie_menu_item create_pie_menu_item(
	scene& scene,
	elem* parent,
	pie_menu_item_config config,
	Fn&& init,
	Args&&... args){
	return pie_menu_item{
		.element = elem_ptr{scene, parent, std::forward<Fn>(init), std::forward<Args>(args)...},
		.action = std::move(config.action),
		.disabled = config.disabled
	};
}

export
struct pie_menu_item_builder{
private:
	scene& scene_;
	std::vector<pie_menu_item> items_{};

public:
	[[nodiscard]] explicit pie_menu_item_builder(scene& scene)
		: scene_(scene){
	}

	void reserve(const std::size_t count){
		items_.reserve(count);
	}

	elem& push_back(pie_menu_item&& item){
		if(!item.element){
			item.element = elem_ptr{scene_, nullptr, std::in_place_type<elem>};
		}
		auto& result = *item.element;
		items_.push_back(std::move(item));
		return result;
	}

	template <std::derived_from<elem> E, typename... Args>
		requires (std::constructible_from<E, scene&, elem*, Args&&...>)
	E& emplace_back(pie_menu_item_config config, Args&&... args){
		auto item = emplace_pie_menu_item<E>(
			scene_,
			nullptr,
			std::move(config),
			std::forward<Args>(args)...);
		auto& result = static_cast<E&>(*item.element);
		items_.push_back(std::move(item));
		return result;
	}

	template <std::derived_from<elem> E, typename... Args>
		requires (std::constructible_from<E, scene&, elem*, Args&&...>)
	E& emplace_back(Args&&... args){
		return emplace_back<E>(pie_menu_item_config{}, std::forward<Args>(args)...);
	}

	template <invocable_elem_init_func Fn, typename... Args>
	elem_init_func_create_t<Fn>& create_back(pie_menu_item_config config, Fn&& init, Args&&... args){
		auto item = create_pie_menu_item(
			scene_,
			nullptr,
			std::move(config),
			std::forward<Fn>(init),
			std::forward<Args>(args)...);
		auto& result = static_cast<elem_init_func_create_t<Fn>&>(*item.element);
		items_.push_back(std::move(item));
		return result;
	}

	template <typename Fn, typename... Args>
		requires (!std::same_as<std::remove_cvref_t<Fn>, pie_menu_item_config> && invocable_elem_init_func<Fn>)
	auto& create_back(Fn&& init, Args&&... args){
		return create_back(pie_menu_item_config{}, std::forward<Fn>(init), std::forward<Args>(args)...);
	}

	[[nodiscard]] std::vector<pie_menu_item> take_items() &&{
		return std::move(items_);
	}
};

export
struct pie_menu_config{
	float item_radius{132.f};
	float dead_zone{34.f};
	float dead_zone_ring_width{8.f};
	float overlay_margin{18.f};
	float item_spacing{8.f};
	math::vec2 item_extent{104.f, 36.f};
	float start_angle{-0.25f};
	float indicator_sweep{0.075f};
	float unselected_opacity{0.32f};
	float disabled_opacity{0.42f};

	graphic::color dead_zone_color{graphic::colors::black.copy_set_a(0.f)};
	graphic::color ring_color{graphic::colors::black.copy_set_a(.62f)};
	graphic::color selected_ring_color{graphic::colors::aqua.copy_set_a(.90f)};

	std::move_only_function<void()> on_cancel{};

	[[nodiscard]] math::vec2 item_direction(const std::size_t index, const std::size_t item_count) const noexcept{
		if(item_count <= blender_pie_slot_count && index < blender_pie_slot_count){
			return blender_pie_slot_direction(index);
		}

		const float count = std::max(1.f, static_cast<float>(item_count));
		const float angle = (start_angle + static_cast<float>(index) / count) * math::pi_2;
		const auto [cos, sin] = math::cos_sin(angle);
		return {cos, sin};
	}

	[[nodiscard]] math::vec2 overlay_extent(std::size_t item_count = blender_pie_slot_count) const noexcept{
		item_count = std::max<std::size_t>(item_count, 1);

		const math::vec2 half_item = item_extent * .5f;
		const float ring_radius = dead_zone + dead_zone_ring_width;
		math::vec2 radius{ring_radius, ring_radius};

		for(std::size_t i = 0; i < item_count; ++i){
			const math::vec2 dir = item_direction(i, item_count);
			radius.x = std::max(radius.x, std::abs(dir.x) * item_radius + half_item.x);
			radius.y = std::max(radius.y, std::abs(dir.y) * item_radius + half_item.y);
		}

		const float half_side = std::max(radius.x, radius.y) + overlay_margin;
		return {half_side * 2.f, half_side * 2.f};
	}
};

struct pie_menu_item_layout{
	math::vec2 extent{};
	math::vec2 center_offset{};
};

struct pie_menu_item_state{
	std::move_only_function<void()> action{};
	bool disabled{};
	pie_menu_item_layout layout{};
	math::vec2 observed_scaling{1.f, 1.f};
};

struct pie_menu_layout_metrics{
	math::vec2 extent{};
	math::vec2 center{};
	std::vector<pie_menu_item_layout> item_layouts{};
};

[[nodiscard]] inline math::vec2 sanitize_pie_menu_extent(math::vec2 extent, math::vec2 fallback) noexcept{
	const auto sanitize_axis = [](const float value, const float fallback_value) noexcept{
		if(std::isfinite(value) && value > 0.f){
			return value;
		}
		if(std::isfinite(fallback_value) && fallback_value > 0.f){
			return fallback_value;
		}
		return 0.f;
	};
	return {
		sanitize_axis(extent.x, fallback.x),
		sanitize_axis(extent.y, fallback.y)
	};
}

[[nodiscard]] inline math::vec2 resolve_pie_menu_item_extent(
	elem* item,
	const pie_menu_config& config){
	const math::vec2 fallback = sanitize_pie_menu_extent(config.item_extent, {});
	if(item != nullptr){
		if(auto extent = item->get_prefer_extent()){
			return sanitize_pie_menu_extent(*extent, fallback);
		}
		if(auto extent = item->pre_acquire_size({layout::pending_size, layout::pending_size})){
			return sanitize_pie_menu_extent(*extent, fallback);
		}
	}
	return fallback;
}

[[nodiscard]] inline math::vec2 resolve_pie_menu_item_extent(
	pie_menu_item& item,
	const pie_menu_config& config){
	return resolve_pie_menu_item_extent(item.element.get(), config);
}

[[nodiscard]] inline bool pie_menu_item_rects_overlap(
	const math::vec2 lhs_offset,
	const math::vec2 lhs_extent,
	const math::vec2 rhs_offset,
	const math::vec2 rhs_extent,
	const float spacing) noexcept{
	const math::vec2 half_sum = (lhs_extent + rhs_extent) * .5f + math::vec2{spacing, spacing};
	return std::abs(lhs_offset.x - rhs_offset.x) < half_sum.x
		&& std::abs(lhs_offset.y - rhs_offset.y) < half_sum.y;
}

[[nodiscard]] inline bool pie_menu_layout_overlaps_at_radius(
	const pie_menu_config& config,
	const pie_menu_layout_metrics& metrics,
	const float radius) noexcept{
	if(metrics.item_layouts.size() < 2){
		return false;
	}

	const float spacing = std::max(config.item_spacing, 0.f);
	for(std::size_t i = 0; i < metrics.item_layouts.size(); ++i){
		const math::vec2 lhs_offset = config.item_direction(i, metrics.item_layouts.size()) * radius;
		for(std::size_t j = 0; j < i; ++j){
			const math::vec2 rhs_offset = config.item_direction(j, metrics.item_layouts.size()) * radius;
			if(pie_menu_item_rects_overlap(
				lhs_offset,
				metrics.item_layouts[i].extent,
				rhs_offset,
				metrics.item_layouts[j].extent,
				spacing)){
				return true;
			}
		}
	}
	return false;
}

[[nodiscard]] inline float resolve_pie_menu_layout_radius(
	const pie_menu_config& config,
	const pie_menu_layout_metrics& metrics) noexcept{
	float low = std::max(config.item_radius, 0.f);
	float high = low;

	while(pie_menu_layout_overlaps_at_radius(config, metrics, high) && high < 1'000'000.f){
		low = high;
		high = high * 1.5f + 16.f;
	}

	if(high >= 1'000'000.f){
		return high;
	}

	for(std::size_t i = 0; i < 32; ++i){
		const float mid = (low + high) * .5f;
		if(pie_menu_layout_overlaps_at_radius(config, metrics, mid)){
			low = mid;
		}else{
			high = mid;
		}
	}

	return high;
}

template <typename GetExtent>
[[nodiscard]] inline pie_menu_layout_metrics compute_pie_menu_layout_metrics_from_extents(
	const std::size_t item_count,
	const pie_menu_config& config,
	GetExtent&& get_extent){
	pie_menu_layout_metrics metrics{};
	metrics.item_layouts.reserve(item_count);

	const float ring_radius = std::max({
		0.f,
		config.dead_zone,
		config.dead_zone + config.dead_zone_ring_width
	});
	math::vec2 min_bound{-ring_radius, -ring_radius};
	math::vec2 max_bound{ring_radius, ring_radius};

	for(std::size_t i = 0; i < item_count; ++i){
		const math::vec2 item_extent = std::invoke(get_extent, i);
		metrics.item_layouts.push_back(pie_menu_item_layout{
			.extent = item_extent
		});
	}

	const float layout_radius = resolve_pie_menu_layout_radius(config, metrics);
	for(std::size_t i = 0; i < metrics.item_layouts.size(); ++i){
		auto& item_layout = metrics.item_layouts[i];
		const math::vec2 item_center = config.item_direction(i, metrics.item_layouts.size()) * layout_radius;
		item_layout.center_offset = item_center;

		const math::vec2 half_item = item_layout.extent * .5f;

		min_bound.x = std::min(min_bound.x, item_center.x - half_item.x);
		min_bound.y = std::min(min_bound.y, item_center.y - half_item.y);
		max_bound.x = std::max(max_bound.x, item_center.x + half_item.x);
		max_bound.y = std::max(max_bound.y, item_center.y + half_item.y);
	}

	const float margin = std::max(config.overlay_margin, 0.f);
	min_bound.x -= margin;
	min_bound.y -= margin;
	max_bound.x += margin;
	max_bound.y += margin;

	metrics.extent = sanitize_pie_menu_extent(max_bound - min_bound, {});
	metrics.center = min_bound * -1.f;
	return metrics;
}

[[nodiscard]] inline pie_menu_layout_metrics compute_pie_menu_layout_metrics(
	std::vector<pie_menu_item>& items,
	const pie_menu_config& config){
	return compute_pie_menu_layout_metrics_from_extents(
		items.size(),
		config,
		[&](const std::size_t index){
			return resolve_pie_menu_item_extent(items[index], config);
		});
}

[[nodiscard]] inline pie_menu_layout_metrics compute_pie_menu_layout_metrics(
	elem_span items,
	const pie_menu_config& config){
	return compute_pie_menu_layout_metrics_from_extents(
		items.size(),
		config,
		[&](const std::size_t index){
			return resolve_pie_menu_item_extent(items[index], config);
		});
}

export
struct pie_menu : basic_group{
private:
	pie_menu_config config_{};
	mr::heap_vector<pie_menu_item_state> items_{get_heap_allocator<pie_menu_item_state>()};
	pie_menu_layout_metrics layout_metrics_{};
	std::optional<std::size_t> selected_{};
	input_handle::mouse activation_button_{input_handle::mouse::RMB};
	math::vec2 activation_scene_pos_{};
	bool accepted_{};
	bool dismissed_{};
	bool activation_press_seen_{};
	bool close_queued_{};

	[[nodiscard]] math::vec2 content_center_() const noexcept{
		return layout_metrics_.center;
	}

	[[nodiscard]] math::vec2 item_direction_(const std::size_t index) const noexcept{
		return config_.item_direction(index, items_.size());
	}

	[[nodiscard]] float item_turn_(const std::size_t index) const noexcept{
		const math::vec2 dir = item_direction_(index);
		float turn = std::atan2(dir.y, dir.x) / math::pi_2;
		if(turn < 0.f){
			turn += 1.f;
		}
		return turn;
	}

	[[nodiscard]] float indicator_sweep_() const noexcept{
		if(items_.size() <= blender_pie_slot_count){
			return config_.indicator_sweep;
		}
		return std::min(config_.indicator_sweep, .82f / static_cast<float>(items_.size()));
	}

	[[nodiscard]] std::optional<std::size_t> hit_test_local(math::vec2 pos) const noexcept{
		if(items_.empty()) return std::nullopt;

		const math::vec2 local = pos - content_src_offset();
		const math::vec2 delta = local - content_center_();
		const float distance = delta.length();
		if(distance <= config_.dead_zone){
			return std::nullopt;
		}

		const math::vec2 direction = delta / distance;
		float best_score = std::numeric_limits<float>::lowest();
		std::size_t best_index{};

		for(std::size_t i = 0; i < items_.size(); ++i){
			const float score = direction.dot(item_direction_(i));
			if(score > best_score){
				best_score = score;
				best_index = i;
			}
		}

		return best_index;
	}

	[[nodiscard]] std::optional<std::size_t> hit_test_scene(math::vec2 scene_pos) const noexcept{
		return hit_test_local(util::transform_scene2local(*this, scene_pos));
	}

	void sync_item_states(){
		for(std::size_t i = 0; i < exposed_children().size(); ++i){
			auto& child = *exposed_children()[i];
			const bool selected = selected_ && *selected_ == i;
			const bool disabled = i < items_.size() && items_[i].disabled;

			float opacity = 1.f;
			if(selected_ && !selected){
				opacity *= config_.unselected_opacity;
			}
			if(disabled){
				opacity *= config_.disabled_opacity;
			}

			child.set_toggled(selected);
			child.set_disabled(disabled);
			child.set_propagate_opacity(opacity);
		}
	}

	void set_selected(std::optional<std::size_t> next){
		if(selected_ == next) return;
		selected_ = next;
		sync_item_states();
		get_scene().notify_display_state_changed(get_channel());
	}

	void update_selection_from_scene_cursor(){
		set_selected(hit_test_scene(get_scene().get_cursor_pos()));
	}

	void close_overlay(){
		get_scene().close_overlay(this);
	}

	void apply_layout_metrics(pie_menu_layout_metrics&& metrics){
		layout_metrics_ = std::move(metrics);
		for(std::size_t i = 0; i < items_.size() && i < layout_metrics_.item_layouts.size(); ++i){
			items_[i].layout = layout_metrics_.item_layouts[i];
		}
	}

	void sync_observed_child_transforms(){
		for(std::size_t i = 0; i < items_.size() && i < exposed_children().size(); ++i){
			items_[i].observed_scaling = exposed_children()[i]->get_scaling();
		}
	}

	void recompute_layout_metrics_from_children(){
		apply_layout_metrics(compute_pie_menu_layout_metrics(exposed_children(), config_));
		sync_observed_child_transforms();
	}

	[[nodiscard]] bool item_extents_match_layout(){
		if(items_.size() != exposed_children().size()){
			return false;
		}

		constexpr float epsilon = .01f;
		for(std::size_t i = 0; i < items_.size(); ++i){
			auto* child = exposed_children()[i];
			const math::vec2 extent = resolve_pie_menu_item_extent(child, config_);
			if(!(extent - items_[i].layout.extent).is_zero(epsilon)){
				return false;
			}
			if(!(child->get_scaling() - items_[i].observed_scaling).is_zero(epsilon)){
				return false;
			}
		}
		return true;
	}

	bool refresh_layout_metrics_if_needed(const bool notify_layout = true){
		if(layout_state.is_children_changed() || !item_extents_match_layout()){
			recompute_layout_metrics_from_children();
			if(notify_layout){
				notify_isolated_layout_changed();
			}
			return true;
		}
		return false;
	}

	[[nodiscard]] bool selected_item_is_usable() const noexcept{
		return selected_
			&& *selected_ < items_.size()
			&& !items_[*selected_].disabled
			&& static_cast<bool>(items_[*selected_].action);
	}

	void finish_from_current_selection(bool from_queue = false){
		if(close_queued_ && !from_queue) return;
		close_queued_ = true;

		if(selected_item_is_usable()){
			accepted_ = true;
			std::invoke(items_[*selected_].action);
		}
		close_overlay();
	}

	void queue_finish_from_current_selection(){
		if(close_queued_) return;
		close_queued_ = true;
		post_task([](pie_menu& menu){
			if(menu.is_live()){
				menu.finish_from_current_selection(true);
			}
		});
	}

	elem& append_item(pie_menu_item&& item){
		if(!item.element){
			item.element = elem_ptr{get_scene(), this, std::in_place_type<elem>};
		}

		auto& child = *item.element;
		child.set_disabled(item.disabled);
		const std::size_t index = items_.size();
		const pie_menu_item_layout item_layout = index < layout_metrics_.item_layouts.size()
			? layout_metrics_.item_layouts[index]
			: pie_menu_item_layout{
				.extent = sanitize_pie_menu_extent(config_.item_extent, {}),
				.center_offset = item_direction_(index) * std::max(config_.item_radius, 0.f)
			};
		items_.push_back(pie_menu_item_state{
			.action = std::move(item.action),
			.disabled = item.disabled,
			.layout = item_layout,
			.observed_scaling = child.get_scaling()
		});
		return basic_group::push_back(std::move(item.element));
	}

	void position_items(){
		if(items_.empty()) return;

		const math::vec2 center = content_center_();
		for(std::size_t i = 0; i < exposed_children().size(); ++i){
			auto& child = *exposed_children()[i];
			const pie_menu_item_layout item_layout = i < items_.size()
				? items_[i].layout
				: pie_menu_item_layout{
					.extent = sanitize_pie_menu_extent(config_.item_extent, {}),
					.center_offset = item_direction_(i) * std::max(config_.item_radius, 0.f)
				};
			const math::vec2 item_center = center + item_layout.center_offset;
			child.resize(item_layout.extent, propagate_mask::none);
			child.set_rel_pos(item_center - item_layout.extent * .5f);
			child.update_abs_src(content_src_pos_abs());
			child.try_layout();
		}
	}

	void draw_dead_zone(float opacity_scl) const{
		const auto center = content_src_pos_abs() + content_center_();
		constexpr float ring_fringe = 1.5f;
		constexpr float ring_vertex_max_error = .65f;

		if(config_.dead_zone > .5f && config_.dead_zone_color.a > .001f){
			auto color = config_.dead_zone_color.copy().mul_a(opacity_scl);
			renderer() << graphic::g2d::fringe::poly(graphic::g2d::poly{
				.pos = center,
				.segments = graphic::g2d::get_circle_vertices(config_.dead_zone),
				.radius = {0.f, config_.dead_zone},
				.color = {color, color}
			}, ring_fringe);
		}

		if(config_.dead_zone_ring_width > .5f){
			const float outer = config_.dead_zone + config_.dead_zone_ring_width;
			const auto ring_color = config_.ring_color.copy().mul_a(opacity_scl);
			const auto ring_segments = [outer, ring_vertex_max_error](const float sweep) noexcept{
				return fx::get_smooth_circle_vertex_count(outer, std::abs(sweep), ring_vertex_max_error);
			};

			if(selected_){
				const float sweep = indicator_sweep_();
				const float start = item_turn_(*selected_) - sweep * .5f;
				const float unselected_start = start + sweep;
				const float unselected_sweep = 1.f - sweep;
				renderer() << graphic::g2d::fringe::poly_partial(graphic::g2d::poly_partial{
					.pos = center,
					.segments = ring_segments(unselected_sweep),
					.range = {unselected_start, unselected_sweep},
					.radius = {config_.dead_zone, outer},
					.color = {ring_color, ring_color, ring_color, ring_color}
				}, ring_fringe);

				const auto selected_color = config_.selected_ring_color.copy().mul_a(opacity_scl);
				renderer() << graphic::g2d::fringe::poly_partial(graphic::g2d::poly_partial{
					.pos = center,
					.segments = ring_segments(sweep),
					.range = {start, sweep},
					.radius = {config_.dead_zone, outer},
					.color = {selected_color, selected_color, selected_color, selected_color}
				}, ring_fringe);
			} else{
				renderer() << graphic::g2d::fringe::poly(graphic::g2d::poly{
					.pos = center,
					.segments = ring_segments(1.f),
					.radius = {config_.dead_zone, outer},
					.color = {ring_color, ring_color}
				}, ring_fringe);
			}
		}
	}

public:
	[[nodiscard]] pie_menu(
		scene& scene,
		elem* parent,
		pie_menu_config config,
		std::vector<pie_menu_item> items,
		input_handle::mouse activation_button = input_handle::mouse::RMB,
		math::vec2 activation_scene_pos = {})
		: basic_group(scene, parent),
		  config_(std::move(config)),
		  activation_button_(activation_button),
		  activation_scene_pos_(activation_scene_pos){
		interactivity = interactivity_flag::enabled;
		set_focus_extended_by_mouse(true);
		set_overflow_ignored(true);
		set_style();

		apply_layout_metrics(compute_pie_menu_layout_metrics(items, config_));

		for(auto& item : items){
			append_item(std::move(item));
		}
		sync_item_states();

		post_task([](pie_menu& menu){
			if(menu.get_scene().is_mouse_pressed(menu.activation_button_)){
				menu.get_scene().capture_mouse(menu, menu.activation_button_, menu.activation_scene_pos_);
				menu.activation_press_seen_ = true;
			}else{
				menu.queue_finish_from_current_selection();
			}
		});
	}

	bool update(float delta_in_ticks) override{
		if(!basic_group::update(delta_in_ticks)) return false;

		if(refresh_layout_metrics_if_needed()){
			position_items();
		}
		update_selection_from_scene_cursor();
		if(get_scene().is_mouse_pressed(activation_button_)){
			activation_press_seen_ = true;
		}else if(activation_press_seen_){
			queue_finish_from_current_selection();
		}
		return true;
	}

	events::op_afterwards on_drag(const events::drag event) override{
		if(event.key.as_mouse() == activation_button_){
			set_selected(hit_test_local(event.dst));
		}
		return events::op_afterwards::intercepted;
	}

	events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override{
		elem::on_click(event, aboves);
		if(event.key.as_mouse() == activation_button_){
			set_selected(hit_test_local(event.pos));
			if(event.key.on_release()){
				finish_from_current_selection();
			}
		}
		return events::op_afterwards::intercepted;
	}

	void layout_elem() override{
		refresh_layout_metrics_if_needed(false);
		elem::layout_elem();
		position_items();
		refresh_overflowed_state_from_children();
	}

protected:
	bool resize_impl(const math::vec2 size) override{
		if(elem::resize_impl(size)){
			position_items();
			return true;
		}
		return false;
	}

	std::optional<math::vec2> pre_acquire_size_impl(layout::optional_mastering_extent extent) override{
		return layout_metrics_.extent.min(extent.potential_extent());
	}

public:
	elem& push_item(pie_menu_item&& item){
		auto& result = append_item(std::move(item));
		recompute_layout_metrics_from_children();
		notify_isolated_layout_changed();
		return result;
	}

	template <std::derived_from<elem> E, typename... Args>
		requires (std::constructible_from<E, scene&, elem*, Args&&...>)
	E& emplace_back(pie_menu_item_config config, Args&&... args){
		auto item = emplace_pie_menu_item<E>(
			get_scene(),
			this,
			std::move(config),
			std::forward<Args>(args)...);
		auto& result = static_cast<E&>(*item.element);
		push_item(std::move(item));
		return result;
	}

	template <std::derived_from<elem> E, typename... Args>
		requires (std::constructible_from<E, scene&, elem*, Args&&...>)
	E& emplace_back(Args&&... args){
		return emplace_back<E>(pie_menu_item_config{}, std::forward<Args>(args)...);
	}

	template <invocable_elem_init_func Fn, typename... Args>
	elem_init_func_create_t<Fn>& create_back(pie_menu_item_config config, Fn&& init, Args&&... args){
		auto item = create_pie_menu_item(
			get_scene(),
			this,
			std::move(config),
			std::forward<Fn>(init),
			std::forward<Args>(args)...);
		auto& result = static_cast<elem_init_func_create_t<Fn>&>(*item.element);
		push_item(std::move(item));
		return result;
	}

	template <typename Fn, typename... Args>
		requires (!std::same_as<std::remove_cvref_t<Fn>, pie_menu_item_config> && invocable_elem_init_func<Fn>)
	auto& create_back(Fn&& init, Args&&... args){
		return create_back(pie_menu_item_config{}, std::forward<Fn>(init), std::forward<Args>(args)...);
	}

	void handle_overlay_dismissed(){
		if(!accepted_ && !std::exchange(dismissed_, true) && config_.on_cancel){
			std::invoke(config_.on_cancel);
		}
	}

	void record_draw_layer(draw_recorder& call_stack_builder) const override{
		elem::record_draw_layer(call_stack_builder);

		call_stack_builder.push_call_enter(*this, [](const pie_menu& s, const draw_call_param& p) static -> draw_call_param{
			const auto content_bound = s.content_bound_abs();
			const auto draw_bound = content_bound.intersection_with(p.draw_bound);
			const float opacity_scl = util::get_final_draw_opacity(s, p);

			if(opacity_scl < 0.f || draw_bound.is_roughly_zero_area(0.01f)){
				return {
					.current_subject = nullptr,
					.draw_bound = draw_bound,
					.opacity_scl = opacity_scl
				};
			}

			return {
				.current_subject = &s,
				.draw_bound = draw_bound,
				.opacity_scl = opacity_scl
			};
		});

		call_stack_builder.push_call_noop(*this, [](const pie_menu& s, const draw_call_param& p,
		                                            const draw_immut_args& args) static{
			if(!args.layer.is_top() || p.current_subject == nullptr) return;
			s.draw_dead_zone(p.opacity_scl);
		});

		for(const auto& element : exposed_children()){
			element->record_draw_layer(call_stack_builder);
		}

		call_stack_builder.push_call_leave();
	}
};

export
inline auto show_pie_menu(
	scene& scene,
	math::vec2 center,
	std::vector<pie_menu_item> items,
	pie_menu_config config = {},
	input_handle::mouse activation_button = input_handle::mouse::RMB){
	const pie_menu_layout_metrics metrics = compute_pie_menu_layout_metrics(items, config);
	const math::vec2 extent = metrics.extent;
	overlay_layout layout{
		.extent = {
			{layout::size_category::mastering, extent.x},
			{layout::size_category::mastering, extent.y}
		},
		.align = align::pos::top_left,
		.external_press_policy = overlay_external_press_policy::dismiss_and_intercept,
		.absolute_offset = center - metrics.center
	};

	auto result = scene.emplace_overlay<pie_menu>(
		layout,
		std::move(config),
		std::move(items),
		activation_button,
		center);

	auto on_dismiss = react_flow::node_pointer{react_flow::make_listener([](const overlay_operation_context& context){
		if(context.operation == overlay_operation::dismiss && context.element != nullptr){
			if(auto* menu = dynamic_cast<pie_menu*>(context.element)){
				menu->handle_overlay_dismissed();
			}
		}
	})};
	result.dialog.get_operation_provider().connect_successor(*on_dismiss);
	return result;
}

export
template <std::invocable<pie_menu_item_builder&> Init>
inline auto show_pie_menu(
	scene& scene,
	math::vec2 center,
	Init&& init,
	pie_menu_config config = {},
	input_handle::mouse activation_button = input_handle::mouse::RMB){
	pie_menu_item_builder builder{scene};
	std::invoke(std::forward<Init>(init), builder);
	return show_pie_menu(
		scene,
		center,
		std::move(builder).take_items(),
		std::move(config),
		activation_button);
}

}
