//
// Created by Matrix on 2025/11/19.
//

export module mo_yanxi.gui.elem.slider;


export import mo_yanxi.gui.infrastructure;

import mo_yanxi.snap_shot;
import mo_yanxi.math;

import std;

namespace mo_yanxi::gui{
export
struct slider2d_slot{
private:
	snap_shot<math::vec2> bar_progress_{};

public:
	math::upoint2 segments{};

	[[nodiscard]] math::vec2 get_segment_unit() const noexcept{
		return math::vec2{
				segments.x ? 1.f / static_cast<float>(segments.x) : 1.f,
				segments.y ? 1.f / static_cast<float>(segments.y) : 1.f
			};
	}

	[[nodiscard]] bool is_segment_move_activated() const noexcept{
		return static_cast<bool>(segments.x) || static_cast<bool>(segments.y);
	}

	void move_progress(const math::vec2 movement_in_percent, vec2 base) noexcept{
		if(is_segment_move_activated()){
			bar_progress_.temp = (base + movement_in_percent).round_to(get_segment_unit()).clamp_xy_normalized();
		} else{
			bar_progress_.temp = (base + movement_in_percent).clamp_xy({}, {1, 1});
		}
	}

	void move_progress(const math::vec2 movement_in_percent, snap_shot<math::vec2>::selection_ptr base_ptr = &snap_shot<math::vec2>::base) noexcept{
		return move_progress(movement_in_percent, bar_progress_.*base_ptr);
	}

	void move_minimum_delta(const math::vec2 move, snap_shot<math::vec2>::selection_ptr base_ptr = &snap_shot<math::vec2>::base){
		if(is_segment_move_activated()){
			move_progress(move.sign_or_zero().mul(get_segment_unit()), base_ptr);
		} else{
			move_progress(move, base_ptr);
		}
	}

	void clamp(math::vec2 from, math::vec2 to) noexcept{
		from.clamp_xy_normalized();
		to.clamp_xy_normalized();
		bar_progress_.temp.clamp_xy(from, to);
		bar_progress_.base.clamp_xy(from, to);
	}

	void min(math::vec2 min) noexcept{
		min.max({});
		bar_progress_.temp.min(min);
		bar_progress_.base.min(min);
	}

	void max(math::vec2 max) noexcept{
		max.min({1, 1});
		bar_progress_.temp.max(max);
		bar_progress_.base.max(max);
	}

	bool apply() noexcept{
		return bar_progress_.try_apply();
	}

	bool update(float delta) noexcept{
		auto dst = bar_progress_.temp - bar_progress_.base;
		if(dst.is_zero(std::numeric_limits<float>::epsilon() * 8)){
			return bar_progress_.try_apply();
		}else{
			bar_progress_.base += dst * delta;
			bar_progress_.base.clamp_xy({0, 0}, {1, 1});
			return true;
		}
	}

	void resume(){
		bar_progress_.resume();
	}

	void set_progress_from_segments(math::usize2 current, math::usize2 total){
		segments = total;
		set_progress((current.as<float>() / total.as<float>()).nan_to0());
	}

	void set_progress_from_segments_x(unsigned current, unsigned total){
		set_progress_from_segments({current, 0}, {total, 0});
	}

	void set_progress_from_segments_y(unsigned current, unsigned total){
		set_progress_from_segments({0, current}, {0, total});
	}

	void set_progress(math::vec2 progress) noexcept{
		progress.clamp_xy({}, math::vectors::constant2<float>::base_vec2);
		this->bar_progress_ = progress;
	}

	[[nodiscard]] math::vec2 get_progress() const noexcept{
		return bar_progress_.base;
	}


	template <typename T>
	[[nodiscard]] math::vector2<T> get_segments_progress() const noexcept{
		return (bar_progress_.base.as<double>() * segments.as<double>()).round<T>();
	}

	[[nodiscard]] math::vec2 get_temp_progress() const noexcept{
		return bar_progress_.temp;
	}

	[[nodiscard]] bool is_sliding() const noexcept{
		return bar_progress_.base != bar_progress_.temp;
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
struct default_slider_drawer : slider_drawer{
	[[nodiscard]] constexpr default_slider_drawer()
	: slider_drawer(tags::persistent, layer_top_only){
	}


	void draw_layer_impl(
		const slider& element,
		math::frect region,
		float opacityScl,
		gfx_config::layer_param layer_param) const override;

	void draw(const slider& element, math::frect region, float opacityScl) const ;
};

export inline constexpr default_slider_drawer default_slider_drawer;
export inline const slider_drawer* global_default_slider_drawer{};

export inline const slider_drawer* get_global_default_slider_drawer() noexcept{
	return global_default_slider_drawer == nullptr ? &default_slider_drawer : global_default_slider_drawer;
}

}




struct slider : elem{
private:

protected:
	void check_apply(){
		if(bar.apply() && submit_node_){
			submit_node_->update_value(bar.get_progress());
		}
	}

	react_flow::provider_cached<math::vec2>* submit_node_;
	referenced_ptr<const style::slider_drawer> drawer_{style::get_global_default_slider_drawer()};

	bool smooth_scroll_{};
	bool smooth_drag_{};
	bool smooth_jump_{};

	math::optional_vec2<float> drag_src_{math::nullopt_vec2<float>};

public:
	float approach_speed_scl = 0.05f;

	slider2d_slot bar;

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

	react_flow::provider_cached<math::vec2>& request_react_node(){
		if(submit_node_){
			return *submit_node_;
		}
		auto& node = get_scene().request_react_node<react_flow::provider_cached<math::vec2>>(*this);
		this->submit_node_ = &node;
		return node;
	}

	[[nodiscard]] bool is_clamped() const noexcept{
		return is_clamped_to_hori() || is_clamped_to_vert();
	}

	[[nodiscard]] math::vec2 get_progress() const noexcept{
		return bar.get_progress();
	}

	[[nodiscard]] const style::slider_drawer* get_drawer() const noexcept{
		return drawer_.get();
	}

	void set_drawer(const referenced_ptr<const style::slider_drawer>& drawer){
		drawer_ = drawer;
		drawer->apply_to(*this);
	}

	void set_hori_only() noexcept{
		sensitivity.y = 0.0f;
	}

	[[nodiscard]] bool is_clamped_to_hori() const noexcept{
		return sensitivity.y == 0.0f;
	}

	void set_vert_only() noexcept{
		sensitivity.x = 0.0f;
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

	void draw_layer(const rect clipSpace, gfx_config::layer_param_pass_t param) const override;

public:
	events::op_afterwards on_scroll(const events::scroll event, std::span<elem* const> aboves) override{
		move_bar(bar, event);

		if(!smooth_scroll_)check_apply();
		return events::op_afterwards::intercepted;
	}

	events::op_afterwards on_drag(const events::drag event) override{
		bar.move_progress(event.delta() * sensitivity / content_extent(), drag_src_);
		return events::op_afterwards::intercepted;
	}

	events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override{
		elem::on_click(event, aboves);
		const auto [key, action, mode] = event.key;

		switch(action){
		case input_handle::act::press :{
			drag_src_ = bar.get_temp_progress();

			if(mode == input_handle::mode::ctrl){
				const auto move = (event.pos - content_src_pos_rel() - get_progress() * content_extent()) /
					content_extent();
				bar.move_progress(move * sensitivity.sign_or_zero());
				if(!smooth_jump_)check_apply();
			}
			break;
		}

		case input_handle::act::release :{
			drag_src_.reset();
			if(!smooth_drag_)check_apply();
		}

		default : break;
		}

		return events::op_afterwards::intercepted;
	}

protected:
	void move_bar(slider2d_slot& slot, const events::scroll& event) const{
		math::vec2 move = event.delta;

		if(input_handle::matched(event.mode, input_handle::mode::shift)){
			move.swap_xy();
		}

		const auto mov = -scroll_sensitivity * sensitivity * (is_clamped() ? vec2{move.y, move.y} : move);
		slot.move_minimum_delta(mov, smooth_scroll_ ? &snap_shot<vec2>::temp : &snap_shot<vec2>::base);

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

	void set_smooth_scroll(const bool smooth) {
		set_smooth(&slider::smooth_scroll_, smooth);
	}

	[[nodiscard]] bool is_smooth_drag() const noexcept{
		return smooth_drag_;
	}

	void set_smooth_drag(const bool smooth){
		set_smooth(&slider::smooth_drag_, smooth);
	}

	[[nodiscard]] bool is_smooth_jump() const noexcept{
		return smooth_jump_;
	}

	void set_smooth_jump(const bool smooth){
		set_smooth(&slider::smooth_jump_, smooth);

	}

	bool update(float delta_in_ticks) override{
		if(elem::update(delta_in_ticks)){
			if(!drag_src_ && (smooth_scroll_ || smooth_jump_ || smooth_drag_)){
				//TODO user provided speed scl?
				if(bar.update(delta_in_ticks * approach_speed_scl) && submit_node_){
					submit_node_->update_value(bar.get_progress());
				}

			}
			return true;
		}
		return false;
	}
	[[nodiscard]] constexpr math::vec2 get_bar_handle_extent() const noexcept{
		return {
				is_clamped_to_vert() ? content_width() : bar_handle_extent.x,
				is_clamped_to_hori() ? content_height() : bar_handle_extent.y,
			};
	}
};


}
