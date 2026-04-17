module;

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.util.animator;

import std;
import mo_yanxi.cond_exist;

namespace mo_yanxi::gui::util {

export
enum class anim_state : std::uint8_t {
    idle,
    waiting_to_enter,
    entering,
    active,
    waiting_to_exit,
    exiting
};

export
template <typename T>
constexpr inline T anime_dynamic_spec_v = []{
	if constexpr (std::floating_point<T>){
		return std::numeric_limits<T>::signaling_NaN();
	} else{
		return std::numeric_limits<T>::max();
	}
}();

template <typename T>
consteval bool is_dynamic(T val) noexcept{
	if constexpr (std::floating_point<T>){
		return val != val;
	}else{
		return val == anime_dynamic_spec_v<T>;
	}

}

export
template <typename T, T Speed, T EnterDelay, T ExitDelay>
struct animator {
    using value_type = T;

	static constexpr bool has_dynamic_speed = util::is_dynamic(Speed);
	static constexpr bool has_dynamic_enter_delay = util::is_dynamic(EnterDelay);
	static constexpr bool has_dynamic_exit_delay = util::is_dynamic(ExitDelay);
	static constexpr bool has_dynamic_timer = (ExitDelay != 0) || (EnterDelay != 0);

private:
    value_type progress{};
    anim_state state{anim_state::idle};

    ADAPTED_NO_UNIQUE_ADDRESS cond_exist<value_type, has_dynamic_speed> speed{};
    ADAPTED_NO_UNIQUE_ADDRESS cond_exist<value_type, has_dynamic_enter_delay> enter_delay{};
    ADAPTED_NO_UNIQUE_ADDRESS cond_exist<value_type, has_dynamic_exit_delay> exit_delay{};
    ADAPTED_NO_UNIQUE_ADDRESS cond_exist<value_type, has_dynamic_timer> timer{};
public:

#pragma region Getters & Setters

    [[nodiscard]] constexpr value_type get_speed() const noexcept {
        if constexpr (has_dynamic_speed) return *speed;
        else return Speed;
    }

    constexpr void set_speed(value_type v) noexcept requires(has_dynamic_speed) {
        speed = v;
    }

    [[nodiscard]] constexpr value_type get_enter_delay() const noexcept {
        if constexpr (has_dynamic_enter_delay) return *enter_delay;
        else return EnterDelay;
    }

    constexpr void set_enter_delay(value_type v) noexcept requires(has_dynamic_enter_delay) {
        enter_delay = v;
    }

    [[nodiscard]] constexpr value_type get_exit_delay() const noexcept {
        if constexpr (has_dynamic_exit_delay) return *exit_delay;
        else return ExitDelay;
    }

    constexpr void set_exit_delay(value_type v) noexcept requires(has_dynamic_exit_delay) {
        exit_delay = v;
    }

	[[nodiscard]] constexpr anim_state get_state() const noexcept {
    	return state;
    }

	[[nodiscard]] constexpr bool is_animating() const noexcept {
    	return state == anim_state::entering || state == anim_state::exiting;
    }

	[[nodiscard]] constexpr bool is_tobe_idle() const noexcept {
    	return state == anim_state::idle || state == anim_state::exiting;
    }

	[[nodiscard]] constexpr bool is_tobe_active() const noexcept {
    	return state == anim_state::active || state == anim_state::entering;
    }

	[[nodiscard]] constexpr value_type get_progress() const noexcept {
    	return progress;
    }

#pragma endregion

    template <typename OnEnterComplete, typename OnExitComplete>
    constexpr void update(value_type delta, OnEnterComplete&& on_enter, OnExitComplete&& on_exit) {
        switch (state) {
            case anim_state::waiting_to_enter:
                // 仅当 timer 存在时才进行操作
                if constexpr (has_dynamic_timer) {
                    *timer += delta;
                    if (*timer >= get_enter_delay()) {
                        *timer = 0;
                        state = anim_state::entering;
                    }
                }
                break;

            case anim_state::entering:
                progress += delta * get_speed();
                if (progress >= value_type(1)) {
                    progress = value_type(1);
                    state = anim_state::active;
                    std::invoke(std::forward<OnEnterComplete>(on_enter));
                }
                break;

            case anim_state::waiting_to_exit:
                if constexpr (has_dynamic_timer) {
                    *timer += delta;
                    if (*timer >= get_exit_delay()) {
                        *timer = 0;
                        state = anim_state::exiting;
                    }
                }
                break;

            case anim_state::exiting:
                progress -= delta * get_speed();
                if (progress <= value_type(0)) {
                    progress = value_type(0);
                    state = anim_state::idle;
                	std::invoke(std::forward<OnExitComplete>(on_exit));
                }
                break;

            case anim_state::idle:
            case anim_state::active:
                break;
        }
    }

    constexpr void set_target(bool target_active) noexcept {
        if (target_active) {
            if (state == anim_state::exiting || state == anim_state::waiting_to_exit) {
                state = anim_state::entering;
            } else if (state == anim_state::idle) {
                if constexpr (has_dynamic_timer) {
                    state = get_enter_delay() > 0 ? anim_state::waiting_to_enter : anim_state::entering;
                    *timer = 0;
                } else {
                    state = anim_state::entering;
                }
            }
        } else {
            if (state == anim_state::entering || state == anim_state::waiting_to_enter) {
                state = anim_state::exiting;
            } else if (state == anim_state::active) {
                if constexpr (has_dynamic_timer) {
                    state = get_exit_delay() > 0 ? anim_state::waiting_to_exit : anim_state::exiting;
                    *timer = 0;
                } else {
                    state = anim_state::exiting;
                }
            }
        }
    }

    [[nodiscard]] constexpr bool is_updating() const noexcept {
        return state != anim_state::idle && state != anim_state::active;
    }
};

export template <typename T = float>
using simple_animator = animator<T, anime_dynamic_spec_v<T>, 0, 0>;

export template <typename T = float>
using delayed_animator = animator<T, anime_dynamic_spec_v<T>, anime_dynamic_spec_v<T>, anime_dynamic_spec_v<T>>;

}