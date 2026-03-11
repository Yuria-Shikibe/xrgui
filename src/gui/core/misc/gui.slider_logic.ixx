export module mo_yanxi.gui.elem.slider_logic;

import mo_yanxi.snap_shot;
import mo_yanxi.math;
import mo_yanxi.meta_programming;
import std;

namespace mo_yanxi::gui{

export
template <typename ValueType>
struct slider_slot{
	using value_type = ValueType;
	using snap_shot_type = snap_shot<value_type>;
    // 引入带符号的增量类型以支持安全的减法判断
    using delta_type = mo_yanxi::to_signed_t<value_type>;

private:
	snap_shot_type bar_progress_{};

public:
	unsigned segments{};

	[[nodiscard]] constexpr value_type get_segment_unit() const noexcept{
		if constexpr(std::floating_point<value_type>){
			return segments ?
				static_cast<value_type>(1) / static_cast<value_type>(segments) : static_cast<value_type>(1);
		} else if constexpr(std::integral<value_type>){
			return static_cast<value_type>(1);
		}else{
			static_assert(false, "unsupported type");
		}
	}

	[[nodiscard]] constexpr bool is_segment_move_activated() const noexcept{
		if constexpr(std::floating_point<value_type>){
			return static_cast<bool>(segments);
		} else if constexpr(std::integral<value_type>){
			return true;
		}else{
			static_assert(false, "unsupported type");
		}
	}

	[[nodiscard]] constexpr value_type get_max_value() const noexcept{
		if constexpr(std::floating_point<value_type>){
			return static_cast<value_type>(1);
		} else if constexpr(std::integral<value_type>){
			return static_cast<value_type>(segments);
		}else{
			static_assert(false, "unsupported type");
		}
	}

	constexpr void move_progress(const delta_type movement, value_type base) noexcept{
		if constexpr(std::floating_point<value_type>){
			if(is_segment_move_activated()){
				bar_progress_.temp = math::clamp(static_cast<value_type>(std::round((base + movement) / get_segment_unit()) * get_segment_unit()), static_cast<value_type>(0), get_max_value());
			} else{
				bar_progress_.temp = math::clamp(static_cast<value_type>(base + movement), static_cast<value_type>(0), get_max_value());
			}
		} else if constexpr(std::integral<value_type>){
            // 安全增减，防止无符号类型下溢/溢出
            if (movement < 0) {
                value_type abs_move = static_cast<value_type>(-movement);
                bar_progress_.temp = (base > abs_move) ? (base - abs_move) : static_cast<value_type>(0);
            } else {
                value_type move_val = static_cast<value_type>(movement);
                bar_progress_.temp = (get_max_value() - base > move_val) ? (base + move_val) : get_max_value();
            }
		}
	}

	constexpr void move_progress(const delta_type movement, typename snap_shot_type::selection_ptr base_ptr = &snap_shot_type::base) noexcept{
		this->move_progress(movement, bar_progress_.*base_ptr);
	}

	constexpr void move_minimum_delta(const delta_type move, typename snap_shot_type::selection_ptr base_ptr = &snap_shot_type::base){
		if(is_segment_move_activated()){
            delta_type sign = move > static_cast<delta_type>(0) ?
				static_cast<delta_type>(1) : (move < static_cast<delta_type>(0) ? static_cast<delta_type>(-1) : static_cast<delta_type>(0));
			this->move_progress(sign * static_cast<delta_type>(get_segment_unit()), base_ptr);
		} else{
			this->move_progress(move, base_ptr);
		}
	}

	constexpr bool clamp(value_type from, value_type to) noexcept{
        from = math::clamp(from, static_cast<value_type>(0), get_max_value());
		to = math::clamp(to, static_cast<value_type>(0), get_max_value());

        auto old_base = bar_progress_.base;
		bar_progress_.temp = math::clamp(bar_progress_.temp, from, to);
		bar_progress_.base = math::clamp(bar_progress_.base, from, to);

        return old_base != bar_progress_.base;
	}

	constexpr bool min(value_type min_val) noexcept{
		min_val = math::max(min_val, static_cast<value_type>(0));
        auto old_base = bar_progress_.base;
		bar_progress_.temp = math::max(bar_progress_.temp, min_val);
		bar_progress_.base = math::max(bar_progress_.base, min_val);
        return old_base != bar_progress_.base;
	}

	constexpr bool max(value_type max_val) noexcept{
		max_val = math::min(max_val, get_max_value());
        auto old_base = bar_progress_.base;
		bar_progress_.temp = math::min(bar_progress_.temp, max_val);
		bar_progress_.base = math::min(bar_progress_.base, max_val);
        return old_base != bar_progress_.base;
	}

	constexpr bool apply() noexcept{
		return bar_progress_.try_apply();
	}

	constexpr bool update(float delta) noexcept{
        auto old_base = bar_progress_.base;

		if constexpr(std::floating_point<value_type>){
			auto dst = bar_progress_.temp - bar_progress_.base;
			if(math::abs(dst) < std::numeric_limits<value_type>::epsilon() * 1024){
				bar_progress_.apply();
			}else{
				bar_progress_.base += dst * static_cast<value_type>(delta);
				bar_progress_.base = math::clamp(bar_progress_.base, static_cast<value_type>(0), get_max_value());
			}
            return old_base != bar_progress_.base;
		} else if constexpr(std::integral<value_type>){
            if (bar_progress_.temp == bar_progress_.base) {
                return false;
            }

            float diff = static_cast<float>(bar_progress_.temp) - static_cast<float>(bar_progress_.base);
            float step = diff * delta;

            // 保证最小步进1，防止因浮点数截断导致的卡死
            if (math::abs(step) < 1.0f) {
                step = (diff > 0.0f) ? 1.0f : -1.0f;
            } else if (math::abs(step) > math::abs(diff)) {
                // 防止由于 delta 太大而导致的过冲 (overshoot)
                step = diff;
            }

			// 区分无符号和有符号整型进行赋值
			if constexpr (std::unsigned_integral<value_type>) {
				// 安全赋值，避免无符号类型累加负数的严重 bug
				if (step > 0.0f) {
					value_type move = static_cast<value_type>(step);
					bar_progress_.base = (get_max_value() - bar_progress_.base > move) ? (bar_progress_.base + move) : get_max_value();
					// 强制对齐到 temp 防止轻微浮点漂移引发的超界
					if (bar_progress_.base > bar_progress_.temp) bar_progress_.base = bar_progress_.temp;
				} else {
					value_type move = static_cast<value_type>(-step);
					bar_progress_.base = (bar_progress_.base > move) ? (bar_progress_.base - move) : static_cast<value_type>(0);
					if (bar_progress_.base < bar_progress_.temp) bar_progress_.base = bar_progress_.temp;
				}
			} else {
				// 有符号整型可以直接相加，无需担心底层下溢错乱
				bar_progress_.base += static_cast<value_type>(step);

				// 强制对齐到 temp 防止过冲 (overshoot)
				if (step > 0.0f && bar_progress_.base > bar_progress_.temp) {
					bar_progress_.base = bar_progress_.temp;
				} else if (step < 0.0f && bar_progress_.base < bar_progress_.temp) {
					bar_progress_.base = bar_progress_.temp;
				}

				// 确保最终值在有效范围内
				bar_progress_.base = math::clamp(bar_progress_.base, static_cast<value_type>(0), get_max_value());
			}

            return old_base != bar_progress_.base;
		}
	}

	constexpr void resume(){
		bar_progress_.resume();
	}

	constexpr bool set_progress_from_segments(unsigned current, unsigned total){
		segments = total;
		if constexpr(std::floating_point<value_type>){
			value_type target = total == 0 ? static_cast<value_type>(0) : static_cast<value_type>(current) / static_cast<value_type>(total);
			if (std::isnan(target)) target = static_cast<value_type>(0);
			return set_progress(target);
		} else if constexpr(std::integral<value_type>){
			return set_progress(static_cast<value_type>(current));
		}else{
			static_assert(false, "unsupported type");
		}
	}

	constexpr bool set_progress(value_type progress) noexcept{
		progress = math::clamp(progress, static_cast<value_type>(0), get_max_value());
		if(bar_progress_.base != progress){
			this->bar_progress_ = progress;
			return true;
		}
		return false;
	}

	[[nodiscard]] constexpr value_type get_progress() const noexcept{
		return bar_progress_.base;
	}

	template <typename T>
	[[nodiscard]] constexpr T get_segments_progress() const noexcept{
		if constexpr(std::floating_point<value_type>){
			return static_cast<T>(std::round(bar_progress_.base * segments));
		} else if constexpr(std::integral<value_type>){
			return static_cast<T>(bar_progress_.base);
		}
	}

	[[nodiscard]] constexpr value_type get_temp_progress() const noexcept{
		return bar_progress_.temp;
	}

	[[nodiscard]] constexpr bool is_sliding() const noexcept{
		return bar_progress_.base != bar_progress_.temp;
	}
};

export
template <typename ValueType, std::size_t Dim>
struct slider_nd {
	using value_type = ValueType;
	using snap_shot_type = snap_shot<value_type>;
    using delta_type = typename slider_slot<value_type>::delta_type;

    std::array<slider_slot<value_type>, Dim> slots;
    std::optional<std::array<value_type, Dim>> drag_src_ = std::nullopt;

    constexpr bool set_progress(std::size_t dim_index, value_type progress) noexcept {
        if (dim_index < Dim) {
            return slots[dim_index].set_progress(progress);
        }
        return false;
    }

    constexpr bool set_progress(const std::array<value_type, Dim>& progresses) noexcept {
        bool changed = false;
        for (std::size_t i = 0; i < Dim; ++i) {
            if (slots[i].set_progress(progresses[i])) {
                changed = true;
            }
        }
        return changed;
    }

    constexpr bool apply() noexcept {
        bool changed = false;
        for (auto& slot : slots) {
            if (slot.apply()) {
                changed = true;
            }
        }
        return changed;
    }

    constexpr bool update(float delta) noexcept {
        bool updated = false;
        for (auto& slot : slots) {
            if (slot.update(delta)) {
                updated = true;
            }
        }
        return updated;
    }

    constexpr bool is_sliding() const noexcept {
        for (const auto& slot : slots) {
            if (slot.is_sliding()) return true;
        }
        return false;
    }

    constexpr void resume() noexcept {
        for (auto& slot : slots) {
            slot.resume();
        }
    }

    constexpr bool clamp(const std::array<value_type, Dim>& from, const std::array<value_type, Dim>& to) noexcept {
        bool changed = false;
        for (std::size_t i = 0; i < Dim; ++i) {
            if (slots[i].clamp(from[i], to[i])) {
                changed = true;
            }
        }
        return changed;
    }

    constexpr std::array<value_type, Dim> get_progress() const noexcept {
        std::array<value_type, Dim> result{};
        for (std::size_t i = 0; i < Dim; ++i) {
            result[i] = slots[i].get_progress();
        }
        return result;
    }

    constexpr std::array<value_type, Dim> get_temp_progress() const noexcept {
        std::array<value_type, Dim> result{};
        for (std::size_t i = 0; i < Dim; ++i) {
            result[i] = slots[i].get_temp_progress();
        }
        return result;
    }

    constexpr void move_progress(const std::array<delta_type, Dim>& movement, const std::array<value_type, Dim>& bases) noexcept {
        for (std::size_t i = 0; i < Dim; ++i) {
            slots[i].move_progress(movement[i], bases[i]);
        }
    }

    constexpr void move_progress_target(const std::array<delta_type, Dim>& movement, bool smooth = true) noexcept {
        for (std::size_t i = 0; i < Dim; ++i) {
            slots[i].move_progress(movement[i], smooth ? &snap_shot_type::temp : &snap_shot_type::base);
        }
    }

    constexpr void move_minimum_delta(const std::array<delta_type, Dim>& moves, bool smooth = true) noexcept {
        for (std::size_t i = 0; i < Dim; ++i) {
            slots[i].move_minimum_delta(moves[i], smooth ? &snap_shot_type::temp : &snap_shot_type::base);
        }
    }

    constexpr bool set_progress_from_segments(std::size_t dim_index, unsigned current, unsigned total) {
        if (dim_index < Dim) {
            return slots[dim_index].set_progress_from_segments(current, total);
        }
        return false;
    }
};

}