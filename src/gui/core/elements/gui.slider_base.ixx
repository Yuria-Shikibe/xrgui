export module mo_yanxi.gui.elem.slider_base;

export import mo_yanxi.gui.infrastructure;
import mo_yanxi.snap_shot;
import mo_yanxi.math;
import std;

namespace mo_yanxi::gui{

export
struct slider1d_slot{
private:
	snap_shot<float> bar_progress_{};

public:
	unsigned segments{};

	[[nodiscard]] float get_segment_unit() const noexcept{
		return segments ? 1.f / static_cast<float>(segments) : 1.f;
	}

	[[nodiscard]] bool is_segment_move_activated() const noexcept{
		return static_cast<bool>(segments);
	}

	void move_progress(const float movement_in_percent, float base) noexcept{
		if(is_segment_move_activated()){
			bar_progress_.temp = std::clamp(std::round((base + movement_in_percent) / get_segment_unit()) * get_segment_unit(), 0.0f, 1.0f);
		} else{
			bar_progress_.temp = std::clamp(base + movement_in_percent, 0.0f, 1.0f);
		}
	}

	void move_progress(const float movement_in_percent, snap_shot<float>::selection_ptr base_ptr = &snap_shot<float>::base) noexcept{
		return move_progress(movement_in_percent, bar_progress_.*base_ptr);
	}

	void move_minimum_delta(const float move, snap_shot<float>::selection_ptr base_ptr = &snap_shot<float>::base){
		if(is_segment_move_activated()){
            float sign = move > 0 ? 1.0f : (move < 0 ? -1.0f : 0.0f);
			move_progress(sign * get_segment_unit(), base_ptr);
		} else{
			move_progress(move, base_ptr);
		}
	}

	void clamp(float from, float to) noexcept{
        from = std::clamp(from, 0.0f, 1.0f);
        to = std::clamp(to, 0.0f, 1.0f);
		bar_progress_.temp = std::clamp(bar_progress_.temp, from, to);
		bar_progress_.base = std::clamp(bar_progress_.base, from, to);
	}

	void min(float min) noexcept{
		min = std::max(min, 0.0f);
		bar_progress_.temp = std::max(bar_progress_.temp, min);
		bar_progress_.base = std::max(bar_progress_.base, min);
	}

	void max(float max) noexcept{
		max = std::min(max, 1.0f);
		bar_progress_.temp = std::min(bar_progress_.temp, max);
		bar_progress_.base = std::min(bar_progress_.base, max);
	}

	bool apply() noexcept{
		return bar_progress_.try_apply();
	}

	bool update(float delta) noexcept{
		auto dst = bar_progress_.temp - bar_progress_.base;
		if(std::abs(dst) < std::numeric_limits<float>::epsilon() * 8){
			return bar_progress_.try_apply();
		}else{
			bar_progress_.base += dst * delta;
			bar_progress_.base = std::clamp(bar_progress_.base, 0.0f, 1.0f);
			return true;
		}
	}

	void resume(){
		bar_progress_.resume();
	}

	bool set_progress_from_segments(unsigned current, unsigned total){
		segments = total;
		float target = total == 0 ? 0.0f : static_cast<float>(current) / static_cast<float>(total);
		if (std::isnan(target)) target = 0.0f;
		return set_progress(target);
	}

	bool set_progress(float progress) noexcept{
		progress = std::clamp(progress, 0.0f, 1.0f);
		if(bar_progress_.base != progress){
			this->bar_progress_ = progress;
			return true;
		}
		return false;
	}

	[[nodiscard]] float get_progress() const noexcept{
		return bar_progress_.base;
	}

	template <typename T>
	[[nodiscard]] T get_segments_progress() const noexcept{
		return static_cast<T>(std::round(bar_progress_.base * segments));
	}

	[[nodiscard]] float get_temp_progress() const noexcept{
		return bar_progress_.temp;
	}

	[[nodiscard]] bool is_sliding() const noexcept{
		return bar_progress_.base != bar_progress_.temp;
	}
};

export
template <typename Derived, typename SlotType, typename ValueType, typename MathExtentsType, typename OptDragSrcType>
struct slider_base : elem{
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

	bool smooth_scroll_{};
	bool smooth_drag_{};
	bool smooth_jump_{};

	OptDragSrcType drag_src_{};

	virtual void on_changed(){
	}
public:
	SlotType bar;

	float approach_speed_scl = 0.05f;

	ValueType scroll_sensitivity;
	ValueType sensitivity;

	MathExtentsType bar_handle_extent;

	[[nodiscard]] slider_base(scene& scene, elem* parent)
	: elem(scene, parent){
		interactivity = interactivity_flag::enabled;
		extend_focus_until_mouse_drop = true;
	}

	[[nodiscard]] ValueType get_progress() const noexcept{
		return bar.get_progress();
	}

	[[nodiscard]] ValueType get_temp_progress() const noexcept{
		return bar.get_temp_progress();
	}

	template <typename T>
	[[nodiscard]] auto get_segments_progress() const noexcept{
		return bar.template get_segments_progress<T>();
	}

	[[nodiscard]] bool is_sliding() const noexcept{
		return bar.is_sliding();
	}

	void set_progress(ValueType progress){
		if(bar.set_progress(progress)){
			on_changed();
		}
	}

	void clamp_progress(ValueType from, ValueType to) noexcept{
		bar.clamp(from, to);
		check_apply();
	}

	void move_progress_target(const ValueType movement_in_percent, bool smooth = true) noexcept{
		bar.move_progress(movement_in_percent, smooth ? &snap_shot<ValueType>::temp : &snap_shot<ValueType>::base);
		if(!smooth) {
			check_apply();
		} else {
			update_approach_state();
		}
	}

protected:
	void on_inbound_changed(bool is_inbounded, bool changed) override{
		elem::on_inbound_changed(is_inbounded, changed);
		set_focused_scroll(is_inbounded);
	}

private:
	void set_smooth(bool Derived::* mptr, bool rst){
		if(util::try_modify(static_cast<Derived*>(this)->*mptr, rst)){
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
		set_smooth(static_cast<bool Derived::*>(&slider_base::smooth_scroll_), smooth);
	}

	[[nodiscard]] bool is_smooth_drag() const noexcept{
		return smooth_drag_;
	}

	void set_smooth_drag(const bool smooth = true){
		set_smooth(static_cast<bool Derived::*>(&slider_base::smooth_drag_), smooth);
	}

	[[nodiscard]] bool is_smooth_jump() const noexcept{
		return smooth_jump_;
	}

	void set_smooth_jump(const bool smooth = true){
		set_smooth(static_cast<bool Derived::*>(&slider_base::smooth_jump_), smooth);
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
};

}
