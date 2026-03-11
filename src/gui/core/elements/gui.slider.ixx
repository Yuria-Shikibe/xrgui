//
// Created by Matrix on 2025/11/19.
//

export module mo_yanxi.gui.elem.slider;


export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.gui.elem.slider_logic;

import mo_yanxi.snap_shot;
import mo_yanxi.math;

import std;

namespace mo_yanxi::gui{

export
struct slider;

export
struct slider_1d;

namespace style{
export
struct slider_drawer : style_drawer<slider>{
	using style_drawer::style_drawer;
};

export
struct default_slider_drawer : slider_drawer{
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
struct slider_1d_drawer : style_drawer<slider_1d>{
	using style_drawer::style_drawer;
};

export
struct default_slider_1d_drawer : slider_1d_drawer{
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
struct slider : elem{
protected:
	void update_approach_state() noexcept {
		if(bar.is_sliding()){
			set_update_required(update_channel::value_approach);
		} else {
			set_update_disabled(update_channel::value_approach);
		}
	}

	void check_apply(){
		if(bar.apply()){
			on_changed();
		}
		update_approach_state();
	}

	referenced_ptr<const style::slider_drawer> drawer_{get_scene().style_manager.get_default<style::slider_drawer>()};

	bool smooth_scroll_{};
	bool smooth_drag_{};
	bool smooth_jump_{};

	std::optional<std::array<float, 2>> drag_src_{std::nullopt};

	virtual void on_changed(){
		// if(submit_node_)submit_node_->update_value(bar.get_progress());
	}
public:
	slider_nd<float, 2> bar;

	float approach_speed_scl = 0.05f;

	/**
	 * @brief Negative value is accepted to flip the operation
	 */
	math::vec2 scroll_sensitivity{1 / 25.0f, 1 / 25.0f};

	math::vec2 sensitivity{1.0f, 1.0f};

	math::vec2 bar_handle_extent{10.0f, 10.0f};

	[[nodiscard]] slider(scene& scene, elem* parent)
	: elem(scene, parent){
		interactivity = interactivity_flag::enabled;
		extend_focus_until_mouse_drop = true;
	}

	// ==========================================
	// 状态获取与控制 (Getters & Setters)
	// ==========================================
	[[nodiscard]] math::vec2 get_progress() const noexcept{
		auto arr = bar.get_progress();
		return {arr[0], arr[1]};
	}

	[[nodiscard]] math::vec2 get_temp_progress() const noexcept{
		auto arr = bar.get_temp_progress();
		return {arr[0], arr[1]};
	}

	template <typename T>
	[[nodiscard]] math::vector2<T> get_segments_progress() const noexcept{
		return {bar.slots[0].template get_segments_progress<T>(), bar.slots[1].template get_segments_progress<T>()};
	}

	[[nodiscard]] bool is_sliding() const noexcept{
		return bar.is_sliding();
	}

	void set_progress(math::vec2 progress){
		if(bar.set_progress({progress.x, progress.y})){
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
		bool changedX = bar.set_progress_from_segments(0, current.x, total.x);
		bool changedY = bar.set_progress_from_segments(1, current.y, total.y);
		if(changedX || changedY){
			on_changed();
		}
	}

	void set_progress_from_segments_x(unsigned current, unsigned total){
		if(bar.set_progress_from_segments(0, current, total)){
			on_changed();
		}
	}

	void set_progress_from_segments_y(unsigned current, unsigned total){
		if(bar.set_progress_from_segments(1, current, total)){
			on_changed();
		}
	}

	void clamp_progress(math::vec2 from, math::vec2 to) noexcept{
		bar.clamp({from.x, from.y}, {to.x, to.y});
		check_apply();
	}

	void move_progress_target(const math::vec2 movement_in_percent, bool smooth = true) noexcept{
		bar.move_progress_target({movement_in_percent.x, movement_in_percent.y}, smooth);
		if(!smooth) {
			check_apply();
		} else {
			update_approach_state();
		}
	}

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
	void on_inbound_changed(bool is_inbounded, bool changed) override{
		elem::on_inbound_changed(is_inbounded, changed);
		set_focused_scroll(is_inbounded);
	}

	void draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const override;

public:
	events::op_afterwards on_scroll(const events::scroll event, std::span<elem* const> aboves) override{
		move_bar(bar, event);

		if(!smooth_scroll_) check_apply();
		else update_approach_state();

		return events::op_afterwards::intercepted;
	}

	events::op_afterwards on_drag(const events::drag event) override{
		if(event.key.mode_bits == input_handle::mode::ctrl)return events::op_afterwards::fall_through;

		if(!drag_src_)return events::op_afterwards::intercepted;

		if(event.key.as_mouse() == input_handle::mouse::LMB){
			auto move = event.delta() * sensitivity / content_extent();
			bar.move_progress({move.x, move.y}, drag_src_.value());
		}else if(event.key.as_mouse() == input_handle::mouse::RMB){
			bar.resume();
		}


		update_approach_state();

		return events::op_afterwards::intercepted;
	}

	events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override{
		elem::on_click(event, aboves);
		const auto [key, action, mode] = event.key;

		switch(static_cast<input_handle::mouse>(key)){
		case input_handle::mouse::_1 :{
			switch(action){
			case input_handle::act::press :{
				drag_src_ = bar.get_temp_progress();

				if(mode == input_handle::mode::ctrl){
					const auto move = (event.get_content_pos(*this) - get_progress() * content_extent()) /
						content_extent();
					auto sign_move = move * sensitivity.sign_or_zero();
					bar.move_progress({sign_move.x, sign_move.y}, bar.get_progress());

					if(!smooth_jump_){
						check_apply();
					} else{
						update_approach_state();
					}
				}
				break;
			}

			case input_handle::act::release :{
				drag_src_.reset();
				if(!smooth_drag_) check_apply();
				else update_approach_state();
				break;
			}

			default : break;
			}
			break;
		}
		case input_handle::mouse::_2 :{
			drag_src_.reset();
			bar.resume();
			break;
		}
		default: break;
		}


		return events::op_afterwards::intercepted;
	}

protected:
	void move_bar(slider_nd<float, 2>& slot, const events::scroll& event) const{
		math::vec2 move = event.delta;

		if(input_handle::matched(event.mode, input_handle::mode::shift)){
			move.swap_xy();
		}

		const auto mov = -scroll_sensitivity * sensitivity * (is_clamped() ? vec2{move.y, move.y} : move);
		slot.move_minimum_delta({mov.x, mov.y}, smooth_scroll_);
	}

private:
	void set_smooth(bool slider::* mptr, bool rst){
		if(util::try_modify(this->*mptr, rst)){
			if(!rst){
				check_apply();
			}
		}
	}
public:
	[[nodiscard]] bool is_smooth_scroll() const noexcept{
		return smooth_scroll_;
	}

	void set_smooth_scroll(const bool smooth = true) {
		set_smooth(&slider::smooth_scroll_, smooth);
	}

	[[nodiscard]] bool is_smooth_drag() const noexcept{
		return smooth_drag_;
	}

	void set_smooth_drag(const bool smooth = true){
		set_smooth(&slider::smooth_drag_, smooth);
	}

	[[nodiscard]] bool is_smooth_jump() const noexcept{
		return smooth_jump_;
	}

	void set_smooth_jump(const bool smooth = true){
		set_smooth(&slider::smooth_jump_, smooth);
	}

protected:
	bool update(float delta_in_ticks) override{
		if(elem::update(delta_in_ticks)){
			if(!drag_src_ && (smooth_scroll_ || smooth_jump_ || smooth_drag_)){
				//TODO user provided speed scl?
				if(bar.update(delta_in_ticks * approach_speed_scl)){
					on_changed();
				}

				if(!bar.is_sliding()){
					set_update_disabled(update_channel::value_approach);
				}
			}
			return true;
		}
		return false;
	}

public:
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
struct slider_1d : elem{
protected:
	void update_approach_state() noexcept {
		if(bar.is_sliding()){
			set_update_required(update_channel::value_approach);
		} else {
			set_update_disabled(update_channel::value_approach);
		}
	}

	void check_apply(){
		if(bar.apply()){
			on_changed();
		}
		update_approach_state();
	}

	referenced_ptr<const style::slider_1d_drawer> drawer_{get_scene().style_manager.get_default<style::slider_1d_drawer>()};

	bool smooth_scroll_{};
	bool smooth_drag_{};
	bool smooth_jump_{};

	std::optional<float> drag_src_{std::nullopt};

	virtual void on_changed(){
	}
public:
	slider_slot<float> bar;
	bool is_vertical{false};

	float approach_speed_scl = 0.05f;

	/**
	 * @brief Negative value is accepted to flip the operation
	 */
	float scroll_sensitivity{1 / 25.0f};

	float sensitivity{1.0f};

	math::vec2 bar_handle_extent{10.0f, 10.0f};

	[[nodiscard]] slider_1d(scene& scene, elem* parent)
	: elem(scene, parent){
		interactivity = interactivity_flag::enabled;
		extend_focus_until_mouse_drop = true;
	}

	// ==========================================
	// 状态获取与控制 (Getters & Setters)
	// ==========================================
	[[nodiscard]] float get_progress() const noexcept{
		return bar.get_progress();
	}

	[[nodiscard]] float get_temp_progress() const noexcept{
		return bar.get_temp_progress();
	}

	template <typename T>
	[[nodiscard]] T get_segments_progress() const noexcept{
		return bar.template get_segments_progress<T>();
	}

	[[nodiscard]] bool is_sliding() const noexcept{
		return bar.is_sliding();
	}

	void set_progress(float progress){
		if(bar.set_progress(progress)){
			on_changed();
		}
	}

	void set_progress_from_segments(unsigned current, unsigned total){
		if(bar.set_progress_from_segments(current, total)){
			on_changed();
		}
	}

	void clamp_progress(float from, float to) noexcept{
		bar.clamp(from, to);
		check_apply();
	}

	void move_progress_target(float movement_in_percent, bool smooth = true) noexcept{
		bar.move_progress(movement_in_percent, smooth ? &snap_shot<float>::temp : &snap_shot<float>::base);
		if(!smooth) {
			check_apply();
		} else {
			update_approach_state();
		}
	}

	[[nodiscard]] const style::slider_1d_drawer* get_drawer() const noexcept{
		return drawer_.get();
	}

	void set_drawer(const referenced_ptr<const style::slider_1d_drawer>& drawer){
		drawer_ = drawer;
		drawer->apply_to(*this);
	}

	void set_drawer(const style::slider_1d_drawer& drawer){
		set_drawer(&drawer);
	}

	void set_clamp_from_layout_policy(layout::layout_policy policy, float target_sensitivity = 1){
		sensitivity = target_sensitivity;
		switch(policy){
		case layout::layout_policy::none: break;
		case layout::layout_policy::vert_major: is_vertical = true; break;
		case layout::layout_policy::hori_major: is_vertical = false; break;
		default: std::unreachable();
		}
	}

	[[nodiscard]] math::vec2 get_bar_handle_movable_extent() const noexcept{
		return content_extent().fdim(get_bar_handle_extent());
	}

protected:
	void on_inbound_changed(bool is_inbounded, bool changed) override{
		elem::on_inbound_changed(is_inbounded, changed);
		set_focused_scroll(is_inbounded);
	}

	void draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const override;

public:
	events::op_afterwards on_scroll(const events::scroll event, std::span<elem* const> aboves) override{
		move_bar(bar, event);

		if(!smooth_scroll_) check_apply();
		else update_approach_state();

		return events::op_afterwards::intercepted;
	}

	events::op_afterwards on_drag(const events::drag event) override{
		if(event.key.mode_bits == input_handle::mode::ctrl)return events::op_afterwards::fall_through;

		if(!drag_src_)return events::op_afterwards::intercepted;

		if(event.key.as_mouse() == input_handle::mouse::LMB){
			auto move_vec = event.delta() * sensitivity / content_extent();
			float move = is_vertical ? move_vec.y : move_vec.x;
			bar.move_progress(move, drag_src_.value());
		}else if(event.key.as_mouse() == input_handle::mouse::RMB){
			bar.resume();
		}

		update_approach_state();

		return events::op_afterwards::intercepted;
	}

	events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override{
		elem::on_click(event, aboves);
		const auto [key, action, mode] = event.key;

		switch(static_cast<input_handle::mouse>(key)){
		case input_handle::mouse::_1 :{
			switch(action){
			case input_handle::act::press :{
				drag_src_ = bar.get_temp_progress();

				if(mode == input_handle::mode::ctrl){
					const auto move_vec = (event.get_content_pos(*this) - get_progress_vec() * content_extent()) /
						content_extent();
					float move = is_vertical ? move_vec.y : move_vec.x;
					float sign = move > 0 ? 1 : (move < 0 ? -1 : 0);
					float sign_move = sign * sensitivity;

					bar.move_progress(sign_move, bar.get_progress());

					if(!smooth_jump_){
						check_apply();
					} else{
						update_approach_state();
					}
				}
				break;
			}

			case input_handle::act::release :{
				drag_src_.reset();
				if(!smooth_drag_) check_apply();
				else update_approach_state();
				break;
			}

			default : break;
			}
			break;
		}
		case input_handle::mouse::_2 :{
			drag_src_.reset();
			bar.resume();
			break;
		}
		default: break;
		}


		return events::op_afterwards::intercepted;
	}

protected:
	void move_bar(slider_slot<float>& slot, const events::scroll& event) const{
		math::vec2 move = event.delta;

		if(input_handle::matched(event.mode, input_handle::mode::shift)){
			move.swap_xy();
		}

		float move_val = is_vertical ? move.y : move.x;
		// 如果水平，通常滚轮垂直滚动应该转换为水平移动（可选）
		if (!is_vertical && move.y != 0 && move.x == 0) {
			move_val = move.y;
		}

		const auto mov = -scroll_sensitivity * sensitivity * move_val;
		slot.move_minimum_delta(mov, smooth_scroll_ ? &snap_shot<float>::temp : &snap_shot<float>::base);
	}

private:
	void set_smooth(bool slider_1d::* mptr, bool rst){
		if(util::try_modify(this->*mptr, rst)){
			if(!rst){
				check_apply();
			}
		}
	}
public:
	[[nodiscard]] bool is_smooth_scroll() const noexcept{
		return smooth_scroll_;
	}

	void set_smooth_scroll(const bool smooth = true) {
		set_smooth(&slider_1d::smooth_scroll_, smooth);
	}

	[[nodiscard]] bool is_smooth_drag() const noexcept{
		return smooth_drag_;
	}

	void set_smooth_drag(const bool smooth = true){
		set_smooth(&slider_1d::smooth_drag_, smooth);
	}

	[[nodiscard]] bool is_smooth_jump() const noexcept{
		return smooth_jump_;
	}

	void set_smooth_jump(const bool smooth = true){
		set_smooth(&slider_1d::smooth_jump_, smooth);
	}

protected:
	bool update(float delta_in_ticks) override{
		if(elem::update(delta_in_ticks)){
			if(!drag_src_ && (smooth_scroll_ || smooth_jump_ || smooth_drag_)){
				if(bar.update(delta_in_ticks * approach_speed_scl)){
					on_changed();
				}

				if(!bar.is_sliding()){
					set_update_disabled(update_channel::value_approach);
				}
			}
			return true;
		}
		return false;
	}

public:
	[[nodiscard]] math::vec2 get_progress_vec() const noexcept{
		return {is_vertical ? 0 : bar.get_progress(), is_vertical ? bar.get_progress() : 0};
	}

	[[nodiscard]] math::vec2 get_temp_progress_vec() const noexcept{
		return {is_vertical ? 0 : bar.get_temp_progress(), is_vertical ? bar.get_temp_progress() : 0};
	}

	[[nodiscard]] constexpr math::vec2 get_bar_handle_extent() const noexcept{
		return get_bar_handle_extent(content_extent());
	}

	[[nodiscard]] constexpr math::vec2 get_bar_handle_extent(math::vec2 content_extent) const noexcept{
		return {
				is_vertical ? content_extent.x : bar_handle_extent.x,
				!is_vertical ? content_extent.y : bar_handle_extent.y,
			};
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
		output_node->update_value(get_progress());
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
		output_node->update_value(get_progress().get_max());
	}
};

export
struct slider_1d_with_output : slider_1d{
private:
	react_flow::node_holder<react_flow::provider_cached<float>> output_node;

public:

	slider_1d_with_output(scene& scene, elem* parent)
		: slider_1d(scene, parent){
	}

	react_flow::provider_cached<float>& get_provider() noexcept{
		return output_node.node;
	}

	void on_changed() override{
		output_node->update_value(get_progress());
	}
};

}