import re

with open('src/gui/core/elements/gui.slider_logic.ixx', 'r') as f:
    text = f.read()

# Replace slider_slot
slider_slot_new = """
export
template <typename ValueType>
struct slider_slot{
private:
	snap_shot<ValueType> bar_progress_{};

public:
	unsigned segments{};

	[[nodiscard]] ValueType get_segment_unit() const noexcept{
		if constexpr(std::floating_point<ValueType>){
			return segments ? static_cast<ValueType>(1) / static_cast<ValueType>(segments) : static_cast<ValueType>(1);
		} else if constexpr(std::integral<ValueType>){
			return static_cast<ValueType>(1);
		}
	}

	[[nodiscard]] bool is_segment_move_activated() const noexcept{
		if constexpr(std::floating_point<ValueType>){
			return static_cast<bool>(segments);
		} else if constexpr(std::integral<ValueType>){
			return true;
		}
	}

	[[nodiscard]] ValueType get_max_value() const noexcept{
		if constexpr(std::floating_point<ValueType>){
			return static_cast<ValueType>(1);
		} else if constexpr(std::integral<ValueType>){
			return static_cast<ValueType>(segments);
		}
	}

	void move_progress(const ValueType movement, ValueType base) noexcept{
		if constexpr(std::floating_point<ValueType>){
			if(is_segment_move_activated()){
				bar_progress_.temp = std::clamp(static_cast<ValueType>(std::round((base + movement) / get_segment_unit()) * get_segment_unit()), static_cast<ValueType>(0), get_max_value());
			} else{
				bar_progress_.temp = std::clamp(static_cast<ValueType>(base + movement), static_cast<ValueType>(0), get_max_value());
			}
		} else if constexpr(std::integral<ValueType>){
			bar_progress_.temp = std::clamp(static_cast<ValueType>(base + movement), static_cast<ValueType>(0), get_max_value());
		}
	}

	void move_progress(const ValueType movement, typename snap_shot<ValueType>::selection_ptr base_ptr = &snap_shot<ValueType>::base) noexcept{
		return move_progress(movement, bar_progress_.*base_ptr);
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
        from = std::clamp(from, static_cast<ValueType>(0), get_max_value());
        to = std::clamp(to, static_cast<ValueType>(0), get_max_value());
		bar_progress_.temp = std::clamp(bar_progress_.temp, from, to);
		bar_progress_.base = std::clamp(bar_progress_.base, from, to);
	}

	void min(ValueType min_val) noexcept{
		min_val = std::max(min_val, static_cast<ValueType>(0));
		bar_progress_.temp = std::max(bar_progress_.temp, min_val);
		bar_progress_.base = std::max(bar_progress_.base, min_val);
	}

	void max(ValueType max_val) noexcept{
		max_val = std::min(max_val, get_max_value());
		bar_progress_.temp = std::min(bar_progress_.temp, max_val);
		bar_progress_.base = std::min(bar_progress_.base, max_val);
	}

	bool apply() noexcept{
		return bar_progress_.try_apply();
	}

	bool update(float delta) noexcept{
		if constexpr(std::floating_point<ValueType>){
			auto dst = bar_progress_.temp - bar_progress_.base;
			if(std::abs(dst) < std::numeric_limits<ValueType>::epsilon() * 8){
				return bar_progress_.try_apply();
			}else{
				bar_progress_.base += dst * static_cast<ValueType>(delta);
				bar_progress_.base = std::clamp(bar_progress_.base, static_cast<ValueType>(0), get_max_value());
				return true;
			}
		} else if constexpr(std::integral<ValueType>){
            // Integral types cannot easily "smoothly approach" using fractional delta addition unless we maintain a shadow float,
            // or just cast `diff * delta`. But if `diff * delta` is truncated to 0, it gets stuck.
            // So we apply instantly, or do a simple proportional step.
            float diff = static_cast<float>(bar_progress_.temp) - static_cast<float>(bar_progress_.base);
            if (std::abs(diff) < 0.5f) {
                return bar_progress_.try_apply();
            } else {
                float step = diff * delta;
                if (std::abs(step) < 1.0f) step = (step > 0.0f) ? 1.0f : -1.0f; // guarantee at least 1 unit move to prevent stuck
                bar_progress_.base += static_cast<ValueType>(step);
                bar_progress_.base = std::clamp(bar_progress_.base, static_cast<ValueType>(0), get_max_value());
                return true;
            }
		}
	}

	void resume(){
		bar_progress_.resume();
	}

	bool set_progress_from_segments(unsigned current, unsigned total){
		segments = total;
		if constexpr(std::floating_point<ValueType>){
			ValueType target = total == 0 ? static_cast<ValueType>(0) : static_cast<ValueType>(current) / static_cast<ValueType>(total);
			if (std::isnan(target)) target = static_cast<ValueType>(0);
			return set_progress(target);
		} else if constexpr(std::integral<ValueType>){
			return set_progress(static_cast<ValueType>(current));
		}
	}

	bool set_progress(ValueType progress) noexcept{
		progress = std::clamp(progress, static_cast<ValueType>(0), get_max_value());
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
		if constexpr(std::floating_point<ValueType>){
			return static_cast<T>(std::round(bar_progress_.base * segments));
		} else if constexpr(std::integral<ValueType>){
			return static_cast<T>(bar_progress_.base);
		}
	}

	[[nodiscard]] ValueType get_temp_progress() const noexcept{
		return bar_progress_.temp;
	}

	[[nodiscard]] bool is_sliding() const noexcept{
		return bar_progress_.base != bar_progress_.temp;
	}
};"""

start = text.find('export\ntemplate <typename ValueType>\nstruct slider_slot{')
end = text.find('export\ntemplate <typename ValueType, size_t Dim>\nstruct slider_nd {')

if start != -1 and end != -1:
    new_text = text[:start] + slider_slot_new + "\n\n" + text[end:]
    with open('src/gui/core/elements/gui.slider_logic.ixx', 'w') as f:
        f.write(new_text)
    print("Replaced slider_slot")
else:
    print("Could not find blocks")
