export module mo_yanxi.gui.elem.slider_logic;

import mo_yanxi.snap_shot;
import mo_yanxi.math;
import std;

namespace mo_yanxi::gui{

export
template <typename ValueType>
struct slider_slot{
private:
	snap_shot<ValueType> bar_progress_{};

public:
	unsigned segments{};

	[[nodiscard]] ValueType get_segment_unit() const noexcept{
		return segments ? static_cast<ValueType>(1) / static_cast<ValueType>(segments) : static_cast<ValueType>(1);
	}

	[[nodiscard]] bool is_segment_move_activated() const noexcept{
		return static_cast<bool>(segments);
	}

	void move_progress(const ValueType movement_in_percent, ValueType base) noexcept{
		if(is_segment_move_activated()){
			bar_progress_.temp = std::clamp(static_cast<ValueType>(std::round((base + movement_in_percent) / get_segment_unit()) * get_segment_unit()), static_cast<ValueType>(0), static_cast<ValueType>(1));
		} else{
			bar_progress_.temp = std::clamp(static_cast<ValueType>(base + movement_in_percent), static_cast<ValueType>(0), static_cast<ValueType>(1));
		}
	}

	void move_progress(const ValueType movement_in_percent, typename snap_shot<ValueType>::selection_ptr base_ptr = &snap_shot<ValueType>::base) noexcept{
		return move_progress(movement_in_percent, bar_progress_.*base_ptr);
	}

	void move_minimum_delta(const ValueType move, typename snap_shot<ValueType>::selection_ptr base_ptr = &snap_shot<ValueType>::base){
		if(is_segment_move_activated()){
            ValueType sign = move > static_cast<ValueType>(0) ? static_cast<ValueType>(1) : (move < static_cast<ValueType>(0) ? static_cast<ValueType>(-1) : static_cast<ValueType>(0));
			move_progress(sign * get_segment_unit(), base_ptr);
		} else{
			move_progress(move, base_ptr);
		}
	}

	void clamp(ValueType from, ValueType to) noexcept{
        from = std::clamp(from, static_cast<ValueType>(0), static_cast<ValueType>(1));
        to = std::clamp(to, static_cast<ValueType>(0), static_cast<ValueType>(1));
		bar_progress_.temp = std::clamp(bar_progress_.temp, from, to);
		bar_progress_.base = std::clamp(bar_progress_.base, from, to);
	}

	void min(ValueType min_val) noexcept{
		min_val = std::max(min_val, static_cast<ValueType>(0));
		bar_progress_.temp = std::max(bar_progress_.temp, min_val);
		bar_progress_.base = std::max(bar_progress_.base, min_val);
	}

	void max(ValueType max_val) noexcept{
		max_val = std::min(max_val, static_cast<ValueType>(1));
		bar_progress_.temp = std::min(bar_progress_.temp, max_val);
		bar_progress_.base = std::min(bar_progress_.base, max_val);
	}

	bool apply() noexcept{
		return bar_progress_.try_apply();
	}

	bool update(float delta) noexcept{
		auto dst = bar_progress_.temp - bar_progress_.base;
		if(std::abs(dst) < std::numeric_limits<ValueType>::epsilon() * 8){
			return bar_progress_.try_apply();
		}else{
			bar_progress_.base += dst * static_cast<ValueType>(delta);
			bar_progress_.base = std::clamp(bar_progress_.base, static_cast<ValueType>(0), static_cast<ValueType>(1));
			return true;
		}
	}

	void resume(){
		bar_progress_.resume();
	}

	bool set_progress_from_segments(unsigned current, unsigned total){
		segments = total;
		ValueType target = total == 0 ? static_cast<ValueType>(0) : static_cast<ValueType>(current) / static_cast<ValueType>(total);
		if (std::isnan(target)) target = static_cast<ValueType>(0);
		return set_progress(target);
	}

	bool set_progress(ValueType progress) noexcept{
		progress = std::clamp(progress, static_cast<ValueType>(0), static_cast<ValueType>(1));
		if(bar_progress_.base != progress){
			this->bar_progress_ = progress;
			return true;
		}
		return false;
	}

	[[nodiscard]] ValueType get_progress() const noexcept{
		return bar_progress_.base;
	}

	template <typename T>
	[[nodiscard]] T get_segments_progress() const noexcept{
		return static_cast<T>(std::round(bar_progress_.base * segments));
	}

	[[nodiscard]] ValueType get_temp_progress() const noexcept{
		return bar_progress_.temp;
	}

	[[nodiscard]] bool is_sliding() const noexcept{
		return bar_progress_.base != bar_progress_.temp;
	}
};

export
template <typename ValueType, size_t Dim>
struct slider_nd {
    std::array<slider_slot<ValueType>, Dim> slots;

    bool smooth_scroll_ = false;
    bool smooth_drag_ = false;
    bool smooth_jump_ = false;
    float approach_speed_scl = 0.05f;

    std::optional<std::array<ValueType, Dim>> drag_src_ = std::nullopt;

    void set_progress(size_t dim_index, ValueType progress) noexcept {
        if (dim_index < Dim) {
            slots[dim_index].set_progress(progress);
        }
    }

    void set_progress(const std::array<ValueType, Dim>& progresses) noexcept {
        for (size_t i = 0; i < Dim; ++i) {
            slots[i].set_progress(progresses[i]);
        }
    }

    bool apply() noexcept {
        bool changed = false;
        for (auto& slot : slots) {
            if (slot.apply()) {
                changed = true;
            }
        }
        return changed;
    }

    bool update(float delta) noexcept {
        bool updated = false;
        for (auto& slot : slots) {
            if (slot.update(delta * approach_speed_scl)) {
                updated = true;
            }
        }
        return updated;
    }

    bool is_sliding() const noexcept {
        for (const auto& slot : slots) {
            if (slot.is_sliding()) return true;
        }
        return false;
    }

    void resume() noexcept {
        for (auto& slot : slots) {
            slot.resume();
        }
    }

    void clamp(const std::array<ValueType, Dim>& from, const std::array<ValueType, Dim>& to) noexcept {
        for (size_t i = 0; i < Dim; ++i) {
            slots[i].clamp(from[i], to[i]);
        }
    }

    std::array<ValueType, Dim> get_progress() const noexcept {
        std::array<ValueType, Dim> result{};
        for (size_t i = 0; i < Dim; ++i) {
            result[i] = slots[i].get_progress();
        }
        return result;
    }

    std::array<ValueType, Dim> get_temp_progress() const noexcept {
        std::array<ValueType, Dim> result{};
        for (size_t i = 0; i < Dim; ++i) {
            result[i] = slots[i].get_temp_progress();
        }
        return result;
    }

    void move_progress(const std::array<ValueType, Dim>& movement, const std::array<ValueType, Dim>& bases) noexcept {
        for (size_t i = 0; i < Dim; ++i) {
            slots[i].move_progress(movement[i], bases[i]);
        }
    }

    void move_progress_target(const std::array<ValueType, Dim>& movement, bool smooth = true) noexcept {
        for (size_t i = 0; i < Dim; ++i) {
            slots[i].move_progress(movement[i], smooth ? &snap_shot<ValueType>::temp : &snap_shot<ValueType>::base);
        }
    }

    void move_minimum_delta(const std::array<ValueType, Dim>& moves) noexcept {
        for (size_t i = 0; i < Dim; ++i) {
            slots[i].move_minimum_delta(moves[i], smooth_scroll_ ? &snap_shot<ValueType>::temp : &snap_shot<ValueType>::base);
        }
    }

    bool set_progress_from_segments(size_t dim_index, unsigned current, unsigned total) {
        if (dim_index < Dim) {
            return slots[dim_index].set_progress_from_segments(current, total);
        }
        return false;
    }
};

}
