module;

#include <cassert>

module mo_yanxi.gui.infrastructure;

import mo_yanxi.graphic.g2d;
import mo_yanxi.gui.fx.compound;
import mo_yanxi.graphic.g2d.fringe;

namespace mo_yanxi::gui{
bool elem_ref_access::retain_live(elem* element) noexcept{
	assert(element != nullptr);
	return element->try_retain_external_ref_live_();
}

void elem_ref_access::retain_existing(elem* element) noexcept{
	assert(element != nullptr);
	element->retain_external_ref_existing_();
}

void elem_ref_access::release(elem* element) noexcept{
	assert(element != nullptr);
	element->release_external_ref_();
}

bool elem_ref_access::is_live(const elem* element) noexcept{
	return element != nullptr && element->is_live();
}

std::stop_token elem_ref_access::stop_token(const elem* element) noexcept{
	assert(element != nullptr);
	return element->lifetime_stop_token();
}

/*void style::debug_elem_drawer::draw_layer_impl(const elem& element, math::frect region, float opacityScl,
                                               fx::layer_param layer_param) const{
	switch(layer_param.layer_index){
	case 0 : draw(element, region, opacityScl);
		break;
	case 1 : draw_background(element, region, opacityScl);
		break;
	default : std::unreachable();
	}
}

void style::debug_elem_drawer::draw(const elem& element, rect region, float opacityScl) const{
	auto cregion = element.clip_to_content_bound(region);

	element.get_scene().renderer().push(graphic::g2d::rect_aabb_outline{
			.v00 = cregion.vert_00(),
			.v11 = cregion.vert_11(),
			.stroke = 1,
			.vert_color = graphic::colors::YELLOW.copy().set_a(.1f)
		});

	using namespace graphic;
	using namespace graphic::g2d;
	color c = colors::gray;
	/*if(element.cursor_state().pressed){
		c = colors::aqua;
	}else #1#
	if(element.cursor_state().focused){
		c = colors::white;
	} else if(element.cursor_state().inbound){
		c = colors::light_gray;
	}
	c.set_a(.75f);
	float f1 = element.cursor_state().get_factor_of(&cursor_states::time_inbound);
	float f2 = element.cursor_state().get_factor_of(&cursor_states::time_pressed);
	float f3 = element.cursor_state().get_factor_of(&cursor_states::time_focus);

	float light = (element.is_toggled() ? 1.6f : 1.f) * (element.is_disabled() ? .5f : 1.f);

	graphic::g2d::quad_group vc{
			c.mul_a(opacityScl).set_light(light),
			c.create_lerp(colors::ACID.to_light(2), f1).mul_a(opacityScl).set_light(light),
			c.create_lerp(colors::ORANGE.to_light(2), f2).mul_a(opacityScl).set_light(light),
			c.create_lerp(colors::CRIMSON.to_light(2), f3).mul_a(opacityScl).set_light(light),
		};

	auto vcb = vc;
	vcb *= color{.1, .1, .1, 1};


	region.shrink(1.f);
	element.get_scene().renderer().push(rect_aabb_outline{
			.v00 = region.vert_00(),
			.v11 = region.vert_11(),
			.stroke = {2},
			.vert_color = {vc}
		});

	if(element.cursor_state().inbound){
		auto pos = util::transform_scene2local(element, element.get_scene().get_cursor_pos());
		auto pos_abs = pos + element.pos_abs();

		auto hit_region = rect{pos_abs, 5 + util::get_nest_depth(&element) * 9.f};

		element.get_scene().renderer().push(rect_aabb_outline{
				.v00 = hit_region.vert_00(),
				.v11 = hit_region.vert_11(),
				.stroke = {2},
				.vert_color = colors::LIME.copy().set_a(.8f)
			});

		auto seg = math::rect::get_closest_vertex_pair(region, hit_region);

		graphic::g2d::fringe::inplace_line_context<12> ctx{};

		fx::compound::dash_line(seg, {8.0, 6.0, 24.0, 6.0}, [&](math::section<math::vec2> s){
			ctx.push(s.from, 1, colors::LIME.copy().set_a(.8f));
			ctx.push(s.to, 1, colors::LIME.copy().set_a(.8f));
			static constexpr float stroke = .5f;
			ctx.add_fringe_cap(stroke, stroke);
			element.renderer() << ctx.fringe_inner(line_segments{}, stroke);
			element.renderer() << ctx.fringe_outer(line_segments{}, stroke);
			element.renderer() << ctx.mid(line_segments{});
			ctx.clear();
		});
	}


	region.scl_size(.25f, .25f);


	//
}

void style::debug_elem_drawer::draw_background(const elem& element, math::frect region, float opacityScl) const{
	using namespace graphic;

	element.get_scene().renderer().push(graphic::g2d::rect_aabb{
			.v00 = region.vert_00(),
			.v11 = region.vert_11(),
			.vert_color = {colors::dark_gray.create_lerp({0, 0, 0, 1}, .85f).copy().mul_a(opacityScl)}
		});
}*/


style::target_known_node_ptr<elem> elem::get_elem_default_style() const{
	return get_style_tree_manager().get_default<elem>();
}

elem::elem(scene& scene, elem* parent) noexcept :
	scene_(std::addressof(scene)),
	parent_(parent),
	is_at_display_stage_(parent ? parent->decide_is_children_displayable_on_add(*this) : true){
	if(is_at_display_stage()){
		scene.notify_display_state_changed(get_channel());
	}

	scene.incr_ref_count_();

	init_altitude_(parent_ ? parent_->layer_altitude_ + 1 : 0);
}

void elem::on_pointer_button(events::event_context& ctx, const events::pointer_button_event& event){
	if(!ctx.is_target_or_bubble_phase()){
		return;
	}
	if(!is_interactable()){
		return;
	}
	if(is_focused()){
		cursor_states_.update_press(event.key);
	}
}

void elem::on_pointer_drag(events::event_context& ctx, const events::pointer_drag_event& event){
	(void)ctx;
	(void)event;
}

void elem::on_pointer_move(events::event_context& ctx, const events::pointer_move_event& event){
	if(ctx.is_target_or_bubble_phase()){
		on_cursor_moved(event);
	}
}

void elem::on_wheel(events::event_context& ctx, const events::wheel_event& event){
	(void)ctx;
	(void)event;
}

void elem::on_key(events::event_context& ctx, const events::key_event& event){
	(void)ctx;
	(void)event;
}

void elem::on_text(events::event_context& ctx, const events::text_event& event){
	(void)ctx;
	(void)event;
}

void elem::on_ime(events::event_context& ctx, const events::ime_event& event){
	(void)ctx;
	(void)event;
}

void elem::load_default_resources(){
	sync_run([](elem& elem){
		elem.sound_group = elem.get_sound_manager().lookup(sound::default_asset_group_name);
		elem.set_style(elem.get_elem_default_style());
	});
}

void elem::push_to_action_queue(){
	get_scene().async_push_elem_to_action_pending(this);
}

tooltip::align_config elem::tooltip_get_align_config() const{
	tooltip::align_config cfg{
			tooltip_create_config.layout_info.follow,
			tooltip_create_config.layout_info.attach_point_tooltip
		};
	switch(tooltip_create_config.layout_info.follow){
	case tooltip::anchor_type::owner :{
		auto pos = align::get_vert(tooltip_create_config.layout_info.attach_point_spawner, extent());
		pos = util::transform_local2scene(*this, pos);
		cfg.pos = pos;
		break;
	}

	default : break;
	}
	return cfg;
}

void elem::create_tooltip(bool fade_in, bool below_scene){
	get_scene().tooltip_manager_.append_tooltip(*this, below_scene, fade_in);
}

bool elem::tooltip_spawner_contains(math::vec2 cursorPos) const noexcept{
	return rect{extent()}.contains_loose(util::transform_scene2local(*this, cursorPos));
}

void elem::drop_tooltip() const{
	if(has_tooltip()) get_scene().tooltip_manager_.request_drop(this);
}

void elem::set_style(style::family_variant v){
	util::sync_set_elem_style(*this, v);
}

void elem::set_style() noexcept{
	sync_run([](elem& elem){
		elem.style_ = {};
		elem.get_scene().notify_display_state_changed(elem.get_channel());
		if(util::try_modify(elem.style_border_cache_, {})){
			elem.notify_isolated_layout_changed();
		}
	});
}

bool elem::update(float delta_in_ticks){
	return true;
}

void elem::mark_destroying_no_external_refs_() noexcept{
	const auto state = lifecycle_ref_.load(std::memory_order_acquire);
	if(elem::lifecycle_ref_count_(state) != 0u){
		assert(false && "cannot destroy an externally referenced elem");
		std::terminate();
	}
	lifecycle_ref_.store(lifecycle_destroying_bit_, std::memory_order_release);
}

bool elem::try_retain_external_ref_live_() noexcept{
	auto state = lifecycle_ref_.load(std::memory_order_acquire);
	while(elem::is_lifecycle_live_(state)){
		const auto count = elem::lifecycle_ref_count_(state);
		if(count == lifecycle_ref_mask_){
			assert(false && "elem external reference count overflow");
			std::terminate();
		}
		if(lifecycle_ref_.compare_exchange_weak(
			state,
			state + lifecycle_ref_word{1u},
			std::memory_order_acq_rel,
			std::memory_order_acquire)){
			if(is_live()){
				return true;
			}
			release_external_ref_();
			return false;
		}
	}
	return false;
}

void elem::retain_external_ref_existing_() noexcept{
	auto state = lifecycle_ref_.load(std::memory_order_acquire);
	while(!elem::is_lifecycle_destroying_(state)){
		const auto count = elem::lifecycle_ref_count_(state);
		if(count == 0u || count == lifecycle_ref_mask_){
			assert(false && "invalid elem external reference count");
			std::terminate();
		}
		if(lifecycle_ref_.compare_exchange_weak(
			state,
			state + lifecycle_ref_word{1u},
			std::memory_order_acq_rel,
			std::memory_order_acquire)){
			return;
		}
	}

	assert(false && "cannot retain a destroying elem");
	std::terminate();
}

void elem::release_external_ref_() noexcept{
	auto state = lifecycle_ref_.load(std::memory_order_acquire);
	while(!elem::is_lifecycle_destroying_(state)){
		const auto count = elem::lifecycle_ref_count_(state);
		if(count == 0u){
			assert(false && "elem external reference count underflow");
			std::terminate();
		}
		if(lifecycle_ref_.compare_exchange_weak(
			state,
			state - lifecycle_ref_word{1u},
			std::memory_order_acq_rel,
			std::memory_order_acquire)){
			return;
		}
	}

	assert(false && "cannot release a destroying elem");
	std::terminate();
}

void elem::detach_from_scene() noexcept{
	assert(scene_ != nullptr);
	assert(is_on_scene_thread(get_scene()));
	if(!scene_refs_attached_){
		return;
	}

	scene_refs_attached_ = false;
	const auto previous_state = lifecycle_ref_.fetch_and(
		static_cast<lifecycle_ref_word>(~lifecycle_live_bit_),
		std::memory_order_acq_rel);
	if(elem::is_lifecycle_destroying_(previous_state)){
		assert(false && "cannot detach a destroying elem");
		std::terminate();
	}
	lifetime_stop_source_.request_stop();
	scene_->layer_altitude_record_.erase(layer_altitude_, 1);
	scene_->drop_(this);
	if(is_at_display_stage()){
		scene_->notify_display_state_changed(get_channel());
	}
	is_at_display_stage_ = false;
	parent_ = nullptr;
}

void elem::notify_layout_changed(propagate_mask propagation){
	if(check_propagate_satisfy(propagation, propagate_mask::local)) layout_state.notify_self_changed();

	if(parent_){
		const bool force_upper = check_propagate_satisfy(propagation, propagate_mask::force_upper);
		if(force_upper || (check_propagate_satisfy(propagation, propagate_mask::super) && layout_state.is_broadcastable(
			propagate_mask::super))){
			if(parent_->layout_state.notify_children_changed(force_upper)){
				if(parent_->layout_state.intercept_lower_to_isolated){
					parent_->notify_isolated_layout_changed();
				} else{
					parent_->notify_layout_changed(propagation - propagate_mask::child);
				}
			}
		}
	}

	if(check_propagate_satisfy(propagation, propagate_mask::child) && layout_state.is_broadcastable(
		propagate_mask::child)){
		for(auto&& element : exposed_children()){
			if(element->layout_state.notify_parent_changed()){
				element->notify_layout_changed(propagation - propagate_mask::super);
			}
		}
	}
}

void elem::notify_isolated_layout_changed(){
	layout_state.notify_self_changed();
	get_scene().add_isolated_layout_update(this);
}

bool elem::update_abs_src(math::vec2 parent_content_src) noexcept{
	return util::try_modify(absolute_pos_, parent_content_src + relative_pos_);
}

bool elem::contains(const math::vec2 pos_relative) const noexcept{
	return bound_rel().contains_loose(pos_relative) &&
		(!parent() || parent()->parent_contain_constrain(parent()->transform_from_content_space(pos_relative)));
}

bool elem::contains_self(const math::vec2 pos_relative, const float margin) const noexcept{
	return bound_rel().expand(margin, margin).contains_loose(pos_relative);
}

bool elem::parent_contain_constrain(const math::vec2 pos_relative) const noexcept{
	return (!parent() || parent()->parent_contain_constrain(parent()->transform_from_content_space(pos_relative)));
}

bool elem::is_focused_scroll() const noexcept{
	assert(scene_ != nullptr);
	return scene_->input_handler_.focus_scroll == this;
}

bool elem::is_focused_key() const noexcept{
	assert(scene_ != nullptr);
	return scene_->input_handler_.focus_key == this;
}

bool elem::is_focused() const noexcept{
	assert(scene_ != nullptr);
	return scene_->input_handler_.focus_cursor == this;
}

bool elem::is_inbounded() const noexcept{
	assert(scene_ != nullptr);
	return std::ranges::contains(scene_->input_handler_.get_inbounds(), this);
}

void elem::set_focused_scroll(const bool focus) noexcept{
	if(!focus && !is_focused_scroll()) return;
	this->scene_->input_handler_.focus_scroll = focus ? this : nullptr;
}

void elem::set_focused_key(const bool focus) noexcept{
	if(focus){
		get_scene().input_handler_.switch_key_focus(this);
	} else if(is_focused_key()){
		get_scene().input_handler_.switch_key_focus(nullptr);
	}
}

void elem::update_altitude_(altitude_t height){
	if(layer_altitude_ == height){
		return;
	}
	scene_->layer_altitude_record_.erase(layer_altitude_, 1);
	layer_altitude_ = height;
	scene_->layer_altitude_record_.insert(height, 1);
	for (auto&& wrapper : collect_children()){
		wrapper.for_each([this](elem& child){
			child.update_altitude_(layer_altitude_ + 1);
		});
	}
}

void elem::init_altitude_(altitude_t height){
	layer_altitude_ = height;
	scene_->layer_altitude_record_.insert(height, 1);
}

void elem::relocate_scene(scene& target_scene) noexcept{
	for(auto&& elem_wrapper : collect_children()){
		elem_wrapper.for_each([&](elem& e){
			e.relocate_self_scene(target_scene);
		});
	}

	relocate_self_scene(target_scene);
}

void elem::relocate_self_scene(scene& target_scene) noexcept{
	assert(&target_scene != scene_);

	scene_->decr_ref_count_();
	scene_ = &target_scene;
	scene_->incr_ref_count_();
}

events::dispatch_result util::thoroughly_esc(elem* where) noexcept{
	while(where){
		if(where->on_esc() == events::dispatch_result::handled){
			return events::dispatch_result::handled;
		}
		where = where->parent();
	}
	return events::dispatch_result::unhandled;
}
}
