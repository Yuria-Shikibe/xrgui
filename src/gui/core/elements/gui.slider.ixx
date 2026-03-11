//
// Created by Matrix on 2025/11/19.
//

export module mo_yanxi.gui.elem.slider;


export import mo_yanxi.gui.infrastructure;

import mo_yanxi.snap_shot;
import mo_yanxi.math;

export import mo_yanxi.gui.elem.slider_logic;

import std;

namespace mo_yanxi::gui{
export
struct slider2d_slot{
private:
	slider_nd<float, 2> impl_;

public:
	[[nodiscard]] math::upoint2 get_segments() const noexcept{
		return {impl_.slots[0].segments, impl_.slots[1].segments};
	}

	[[nodiscard]] math::vec2 get_segment_unit() const noexcept{
		return {impl_.slots[0].get_segment_unit(), impl_.slots[1].get_segment_unit()};
	}

	[[nodiscard]] bool is_segment_move_activated() const noexcept{
		return impl_.slots[0].is_segment_move_activated() || impl_.slots[1].is_segment_move_activated();
	}

	void move_progress(const math::vec2 movement_in_percent, vec2 base) noexcept{
		impl_.move_progress({movement_in_percent.x, movement_in_percent.y}, {base.x, base.y});
	}

	void move_progress(const math::vec2 movement_in_percent, snap_shot<math::vec2>::selection_ptr base_ptr = &snap_shot<math::vec2>::base) noexcept{
		bool smooth = (base_ptr == &snap_shot<math::vec2>::temp);
		impl_.move_progress_target({movement_in_percent.x, movement_in_percent.y}, smooth);
	}

	void move_minimum_delta(const math::vec2 move, snap_shot<math::vec2>::selection_ptr base_ptr = &snap_shot<math::vec2>::base){
		bool smooth = (base_ptr == &snap_shot<math::vec2>::temp);
		impl_.move_minimum_delta({move.x, move.y}, smooth);
	}

	void clamp(math::vec2 from, math::vec2 to) noexcept{
		impl_.clamp({from.x, from.y}, {to.x, to.y});
	}

	void min(math::vec2 min_val) noexcept{
		impl_.slots[0].min(min_val.x);
		impl_.slots[1].min(min_val.y);
	}

	void max(math::vec2 max_val) noexcept{
		impl_.slots[0].max(max_val.x);
		impl_.slots[1].max(max_val.y);
	}

	bool apply() noexcept{
		return impl_.apply();
	}

	bool update(float delta) noexcept{
		return impl_.update(delta);
	}

	void resume(){
		impl_.resume();
	}

	bool set_progress_from_segments(math::usize2 current, math::usize2 total){
		bool x_changed = impl_.set_progress_from_segments(0, current.x, total.x);
		bool y_changed = impl_.set_progress_from_segments(1, current.y, total.y);
		return x_changed || y_changed;
	}

	bool set_progress_from_segments_x(unsigned current, unsigned total){
		return impl_.set_progress_from_segments(0, current, total);
	}

	bool set_progress_from_segments_y(unsigned current, unsigned total){
		return impl_.set_progress_from_segments(1, current, total);
	}

	bool set_progress(math::vec2 progress) noexcept{
		return impl_.set_progress({progress.x, progress.y});
	}

	[[nodiscard]] math::vec2 get_progress() const noexcept{
		auto p = impl_.get_progress();
		return {p[0], p[1]};
	}


	template <typename T>
	[[nodiscard]] math::vector2<T> get_segments_progress() const noexcept{
		return {impl_.slots[0].template get_segments_progress<T>(), impl_.slots[1].template get_segments_progress<T>()};
	}

	[[nodiscard]] math::vec2 get_temp_progress() const noexcept{
		auto p = impl_.get_temp_progress();
		return {p[0], p[1]};
	}

	[[nodiscard]] bool is_sliding() const noexcept{
		return impl_.is_sliding();
	}
};

export
struct slider;

namespace style{
export
struct slider_drawer : style_drawer<slider>{
	using style_drawer::style_drawer;
};

export
struct
default_slider_drawer : slider_drawer{
	[[nodiscard]] constexpr default_slider_drawer()
	: slider_drawer(tags::persistent, layer_top_only){
	}

	void draw_layer_impl(
		const slider& element,
		math::frect region,
		float opacityScl,
		fx::layer_param layer_param) const override;

	void draw(const slider& element, math::frect region, float opacityScl) const ;
};

export inline constexpr default_slider_drawer default_slider_drawer_instance;

export inline const slider_drawer* global_default_slider_drawer{};

export inline const slider_drawer* get_global_default_slider_drawer() noexcept{
	return global_default_slider_drawer == nullptr ? &default_slider_drawer_instance : global_default_slider_drawer;
}

export
struct slider_1d;

export
struct slider_1d_drawer : style_drawer<slider_1d>{
	using style_drawer::style_drawer;
};

export
struct
default_slider_1d_drawer : slider_1d_drawer{
	[[nodiscard]] constexpr default_slider_1d_drawer()
	: slider_1d_drawer(tags::persistent, layer_top_only){
	}

	void draw_layer_impl(
		const slider_1d& element,
		math::frect region,
		float opacityScl,
		fx::layer_param layer_param) const override;

	void draw(const slider_1d& element, math::frect region, float opacityScl) const ;
};

export inline constexpr default_slider_1d_drawer default_slider_1d_drawer_instance;

export inline const slider_1d_drawer* global_default_slider_1d_drawer{};

export inline const slider_1d_drawer* get_global_default_slider_1d_drawer() noexcept{
	return global_default_slider_1d_drawer == nullptr ? &default_slider_1d_drawer_instance : global_default_slider_1d_drawer;
}

}

export
template <typename Derived, typename ValueType>
struct slider_base : elem {
protected:
	void update_approach_state() noexcept {
		if (static_cast<Derived*>(this)->bar.is_sliding()) {
			set_update_required(update_channel::value_approach);
		} else {
			set_update_disabled(update_channel::value_approach);
		}
	}

	void check_apply() {
		if (static_cast<Derived*>(this)->bar.apply()) {
			on_changed();
		}
		update_approach_state();
	}

	bool smooth_scroll_{};
	bool smooth_drag_{};
	bool smooth_jump_{};

	std::optional<ValueType> drag_src_{std::nullopt};

	virtual void on_changed() {
		// if(submit_node_)submit_node_->update_value(bar.get_progress());
	}
public:
	float approach_speed_scl = 0.05f;

	[[nodiscard]] slider_base(scene& scene, elem* parent)
		: elem(scene, parent) {
		interactivity = interactivity_flag::enabled;
		extend_focus_until_mouse_drop = true;
	}

protected:
	void on_inbound_changed(bool is_inbounded, bool changed) override {
		elem::on_inbound_changed(is_inbounded, changed);
		set_focused_scroll(is_inbounded);
	}

public:
	events::op_afterwards on_scroll(const events::scroll event, std::span<elem* const> aboves) override {
		static_cast<Derived*>(this)->move_bar(event);

		if (!smooth_scroll_) check_apply();
		else update_approach_state();

		return events::op_afterwards::intercepted;
	}

	events::op_afterwards on_drag(const events::drag event) override {
		if (event.key.mode_bits == input_handle::mode::ctrl) return events::op_afterwards::fall_through;

		if (!drag_src_) return events::op_afterwards::intercepted;

		if (event.key.as_mouse() == input_handle::mouse::LMB) {
			static_cast<Derived*>(this)->drag_bar(event);
		} else if (event.key.as_mouse() == input_handle::mouse::RMB) {
			static_cast<Derived*>(this)->bar.resume();
		}

		update_approach_state();

		return events::op_afterwards::intercepted;
	}

	events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override {
		elem::on_click(event, aboves);
		const auto [key, action, mode] = event.key;

		switch (static_cast<input_handle::mouse>(key)) {
		case input_handle::mouse::_1: {
			switch (action) {
			case input_handle::act::press: {
				drag_src_ = static_cast<Derived*>(this)->bar.get_temp_progress();

				if (mode == input_handle::mode::ctrl) {
					static_cast<Derived*>(this)->jump_bar(event);
					if (!smooth_jump_) {
						check_apply();
					} else {
						update_approach_state();
					}
				}
				break;
			}
			case input_handle::act::release: {
				drag_src_.reset();
				if (!smooth_drag_) check_apply();
				else update_approach_state();
				break;
			}
			default: break;
			}
			break;
		}
		case input_handle::mouse::_2: {
			drag_src_.reset();
			static_cast<Derived*>(this)->bar.resume();
			break;
		}
		default: break;
		}

		return events::op_afterwards::intercepted;
	}

private:
	void set_smooth(bool slider_base::* mptr, bool rst) {
		if (util::try_modify(this->*mptr, rst)) {
			if (!rst) {
				check_apply();
			}
		}
	}
public:
	[[nodiscard]] bool is_smooth_scroll() const noexcept {
		return smooth_scroll_;
	}

	void set_smooth_scroll(const bool smooth = true) {
		set_smooth(&slider_base::smooth_scroll_, smooth);
	}

	[[nodiscard]] bool is_smooth_drag() const noexcept {
		return smooth_drag_;
	}

	void set_smooth_drag(const bool smooth = true) {
		set_smooth(&slider_base::smooth_drag_, smooth);
	}

	[[nodiscard]] bool is_smooth_jump() const noexcept {
		return smooth_jump_;
	}

	void set_smooth_jump(const bool smooth = true) {
		set_smooth(&slider_base::smooth_jump_, smooth);
	}

protected:
	bool update(float delta_in_ticks) override {
		if (elem::update(delta_in_ticks)) {
			if (!drag_src_ && (smooth_scroll_ || smooth_jump_ || smooth_drag_)) {
				//TODO user provided speed scl?
				if (static_cast<Derived*>(this)->bar.update(delta_in_ticks * approach_speed_scl)) {
					on_changed();
				}

				if (!static_cast<Derived*>(this)->bar.is_sliding()) {
					set_update_disabled(update_channel::value_approach);
				}
			}
			return true;
		}
		return false;
	}
};


struct slider : slider_base<slider, math::vec2> {
protected:
	referenced_ptr<const style::slider_drawer> drawer_{get_scene().style_manager.get_default<style::slider_drawer>()};

public:
	slider2d_slot bar;

	/**
	 * @brief Negative value is accepted to flip the operation
	 */
	math::vec2 scroll_sensitivity{1 / 25.0f, 1 / 25.0f};

	math::vec2 sensitivity{1.0f, 1.0f};

	math::vec2 bar_handle_extent{10.0f, 10.0f};

	[[nodiscard]] slider(scene& scene, elem* parent)
	: slider_base(scene, parent){
	}

	// ==========================================
	// 状态获取与控制 (Getters & Setters)
	// ==========================================
	[[nodiscard]] math::vec2 get_progress() const noexcept{
		return bar.get_progress();
	}

	[[nodiscard]] math::vec2 get_temp_progress() const noexcept{
		return bar.get_temp_progress();
	}

	template <typename T>
	[[nodiscard]] math::vector2<T> get_segments_progress() const noexcept{
		return bar.template get_segments_progress<T>();
	}

	[[nodiscard]] bool is_sliding() const noexcept{
		return bar.is_sliding();
	}

	void set_progress(math::vec2 progress){
		if(bar.set_progress(progress)){
			on_changed();
		}
	}

	void set_progress(float progress){
		auto clampX = is_clamped_to_hori();
		auto clampY = is_clamped_to_vert();
		if(clampX && clampY)return;
		if(clampX){
			set_progress({progress, 0});
		}else if(clampY){
			set_progress({0, progress});
		}else{
			set_progress({progress, progress});
		}
	}

	void set_progress_from_segments(math::usize2 current, math::usize2 total){
		if(bar.set_progress_from_segments(current, total)){
			on_changed();
		}
	}

	void set_progress_from_segments_x(unsigned current, unsigned total){
		if(bar.set_progress_from_segments_x(current, total)){
			on_changed();
		}
	}

	void set_progress_from_segments_y(unsigned current, unsigned total){
		if(bar.set_progress_from_segments_y(current, total)){
			on_changed();
		}
	}

	void clamp_progress(math::vec2 from, math::vec2 to) noexcept{
		bar.clamp(from, to);
		check_apply();
	}

	void move_progress_target(const math::vec2 movement_in_percent, bool smooth = true) noexcept{
		bar.move_progress(movement_in_percent, smooth ? &snap_shot<math::vec2>::temp : &snap_shot<math::vec2>::base);
		if(!smooth) {
			check_apply();
		} else {
			update_approach_state();
		}
	}

	// react_flow::provider_cached<math::vec2>& request_react_node(){
	// 	if(submit_node_){
	// 		return *submit_node_;
	// 	}
	// 	auto& node = get_scene().request_react_node<react_flow::provider_cached<math::vec2>>(*this);
	// 	this->submit_node_ = &node;
	// 	return node;
	// }

	[[nodiscard]] bool is_clamped() const noexcept{
		return is_clamped_to_hori() || is_clamped_to_vert();
	}

	[[nodiscard]] const style::slider_drawer* get_drawer() const noexcept{
		return drawer_.get();
	}

	void set_drawer(const referenced_ptr<const style::slider_drawer>& drawer){
		drawer_ = drawer;
		drawer->apply_to(*this);
	}

	void set_drawer(const style::slider_drawer& drawer){
		set_drawer(&drawer);
	}

	void set_hori_only() noexcept{
		sensitivity.y = 0.0f;
	}

	void set_vert_only() noexcept{
		sensitivity.x = 0.0f;
	}

	void set_clamp_from_layout_policy(layout::layout_policy policy, math::vec2 target_sensitivity = {1, 1}){
		sensitivity = target_sensitivity;
		switch(policy){
		case layout::layout_policy::none: break;
		case layout::layout_policy::vert_major: set_vert_only(); break;
		case layout::layout_policy::hori_major: set_hori_only(); break;
		default: std::unreachable();
		}
	}

	[[nodiscard]] bool is_clamped_to_hori() const noexcept{
		return sensitivity.y == 0.0f;
	}

	[[nodiscard]] bool is_clamped_to_vert() const noexcept{
		return sensitivity.x == 0.0f;
	}

	[[nodiscard]] math::vec2 get_bar_handle_movable_extent() const noexcept{
		return content_extent().fdim(get_bar_handle_extent());
	}

protected:

	void draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const override;

public:
	void move_bar(const events::scroll& event) {
		math::vec2 move = event.delta;

		if(input_handle::matched(event.mode, input_handle::mode::shift)){
			move.swap_xy();
		}

		const auto mov = -scroll_sensitivity * sensitivity * (is_clamped() ? vec2{move.y, move.y} : move);
		bar.move_minimum_delta(mov, smooth_scroll_ ? &snap_shot<vec2>::temp : &snap_shot<vec2>::base);
	}

	void drag_bar(const events::drag& event) {
		bar.move_progress(event.delta() * sensitivity / content_extent(), drag_src_.value());
	}

	void jump_bar(const events::click& event) {
		const auto move = (event.get_content_pos(*this) - get_progress() * content_extent()) /
			content_extent();
		bar.move_progress(move * sensitivity.sign_or_zero());
	}

	[[nodiscard]] constexpr math::vec2 get_bar_handle_extent() const noexcept{
		return get_bar_handle_extent(content_extent());
	}

	[[nodiscard]] constexpr math::vec2 get_bar_handle_extent(math::vec2 content_extent) const noexcept{
		return {
				is_clamped_to_vert() ? content_extent.x : bar_handle_extent.x,
				is_clamped_to_hori() ? content_extent.y : bar_handle_extent.y,
			};
	}
};

export
struct slider_1d : slider_base<slider_1d, float> {
protected:
	referenced_ptr<const style::slider_1d_drawer> drawer_{get_scene().style_manager.get_default<style::slider_1d_drawer>()};

public:
	slider_slot<float> bar;
	bool is_vertical = false;

	float scroll_sensitivity = 1 / 25.0f;
	float sensitivity = 1.0f;
	float bar_handle_extent = 10.0f;

	[[nodiscard]] slider_1d(scene& scene, elem* parent)
		: slider_base(scene, parent) {}

	[[nodiscard]] float get_progress() const noexcept { return bar.get_progress(); }
	[[nodiscard]] float get_temp_progress() const noexcept { return bar.get_temp_progress(); }

	template <typename T>
	[[nodiscard]] T get_segments_progress() const noexcept { return bar.template get_segments_progress<T>(); }

	[[nodiscard]] bool is_sliding() const noexcept { return bar.is_sliding(); }

	void set_progress(float progress) {
		if (bar.set_progress(progress)) on_changed();
	}

	void set_progress_from_segments(unsigned current, unsigned total) {
		if (bar.set_progress_from_segments(current, total)) on_changed();
	}

	void clamp_progress(float from, float to) noexcept {
		bar.clamp(from, to);
		check_apply();
	}

	void move_progress_target(float movement_in_percent, bool smooth = true) noexcept {
		bar.move_progress(movement_in_percent, smooth ? &snap_shot<float>::temp : &snap_shot<float>::base);
		if (!smooth) check_apply();
		else update_approach_state();
	}

	[[nodiscard]] const style::slider_1d_drawer* get_drawer() const noexcept {
		return drawer_.get();
	}

	void set_drawer(const referenced_ptr<const style::slider_1d_drawer>& drawer) {
		drawer_ = drawer;
		drawer->apply_to(*this);
	}

	void set_drawer(const style::slider_1d_drawer& drawer) {
		set_drawer(&drawer);
	}

	[[nodiscard]] float get_bar_handle_movable_extent() const noexcept {
		return is_vertical ? (content_extent().y - get_bar_handle_extent().y) : (content_extent().x - get_bar_handle_extent().x);
	}

	[[nodiscard]] constexpr math::vec2 get_bar_handle_extent() const noexcept {
		return get_bar_handle_extent(content_extent());
	}

	[[nodiscard]] constexpr math::vec2 get_bar_handle_extent(math::vec2 content_extent) const noexcept {
		return {
			is_vertical ? content_extent.x : bar_handle_extent,
			is_vertical ? bar_handle_extent : content_extent.y
		};
	}

protected:
	void draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const override {
		elem::draw_layer(clipSpace, param);
		if (drawer_) {
			drawer_->draw_layer(*this, content_bound_abs(), get_draw_opacity(), param);
		}
	}

public:
	void move_bar(const events::scroll& event) {
		float move = is_vertical ? event.delta.y : event.delta.x;
		const auto mov = -scroll_sensitivity * sensitivity * move;
		bar.move_minimum_delta(mov, smooth_scroll_ ? &snap_shot<float>::temp : &snap_shot<float>::base);
	}

	void drag_bar(const events::drag& event) {
		float delta = is_vertical ? event.delta().y : event.delta().x;
		float extent = is_vertical ? content_extent().y : content_extent().x;
		bar.move_progress(delta * sensitivity / extent, drag_src_.value());
	}

	void jump_bar(const events::click& event) {
		float pos = is_vertical ? event.get_content_pos(*this).y : event.get_content_pos(*this).x;
		float extent = is_vertical ? content_extent().y : content_extent().x;
		float move = (pos - get_progress() * extent) / extent;
		float sign = sensitivity > 0 ? 1.0f : (sensitivity < 0 ? -1.0f : 0.0f);
		bar.move_progress(move * sign);
	}
};

export
struct slider_with_2d_output : slider{
private:
	react_flow::node_holder<react_flow::provider_cached<math::vec2>> output_node;

public:

	slider_with_2d_output(scene& scene, elem* parent)
		: slider(scene, parent){
	}

	react_flow::provider_cached<math::vec2>& get_provider() noexcept{
		return output_node.node;
	}

	void on_changed() override{
		output_node->update_value(bar.get_progress());
	}
};

export
struct slider_with_output : slider{
private:
	react_flow::node_holder<react_flow::provider_cached<float>> output_node;

public:

	slider_with_output(scene& scene, elem* parent)
		: slider(scene, parent){
	}

	react_flow::provider_cached<float>& get_provider() noexcept{
		return output_node.node;
	}

	void on_changed() override{
		output_node->update_value(bar.get_progress().get_max());
	}
};

}