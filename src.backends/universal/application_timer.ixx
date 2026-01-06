module;

#include <cassert>

export module mo_yanxi.backend.application_timer;

export import mo_yanxi.backend.unit;
import std;

namespace mo_yanxi::backend{
double app_get_time();

void app_reset_time(double t);

double app_get_delta(double last);

export
template <typename T>
	requires (std::is_floating_point_v<T>)
class application_timer{
	using RawSec = double;
	using RawTick = direct_access_time_unit<double, tick_ratio>;

	using Tick = direct_access_time_unit<T, tick_ratio>;
	using Sec = direct_access_time_unit<T>;

	RawSec globalTime{};
	RawSec globalDelta{};
	RawSec updateTime{};
	RawSec updateDelta{};

	delta_setter deltaSetter{nullptr};
	timer_setter timerSetter{nullptr};
	time_reseter timeReseter{nullptr};

	bool paused = false;
	//TODO time scale?

public:
	static constexpr T ticks_per_second{tick_ratio::den};

	[[nodiscard]] constexpr application_timer() noexcept = default;

	[[nodiscard]] constexpr application_timer(
		const delta_setter deltaSetter,
		const timer_setter timerSetter,
		const time_reseter timeReseter
	) noexcept :
	deltaSetter{deltaSetter},
	timerSetter{timerSetter},
	timeReseter{timeReseter}{
	}

	constexpr static application_timer get_default() noexcept{
		return application_timer{
				app_get_delta, app_get_time, app_reset_time
			};
	}

	constexpr void pause() noexcept{ paused = true; }

	[[nodiscard]] constexpr bool is_paused() const noexcept{ return paused; }

	constexpr void resume() noexcept{ paused = false; }

	constexpr void set_pause(const bool v) noexcept{ paused = v; }

	void fetch_time(){
#if DEBUG_CHECK
		if(!timerSetter || !deltaSetter){
			throw std::invalid_argument{"Missing Timer Function Pointer"};
		}
#endif

		globalDelta = std::clamp<double>(deltaSetter(globalTime), 0, 5. / 60.);
		globalTime = timerSetter();

		updateDelta = paused ? RawSec{0} : globalDelta;
		updateTime += updateDelta;
	}

	void reset_time(){
		assert(timeReseter != nullptr);

		updateTime = globalTime = 0;
		timeReseter(0);
	}

	[[nodiscard]] constexpr double raw_global_time() const noexcept{ return globalTime; }


	[[nodiscard]] constexpr Sec global_delta() const noexcept{ return Sec{static_cast<T>(globalDelta)}; }
	[[nodiscard]] constexpr Sec update_delta() const noexcept{ return Sec{static_cast<T>(updateDelta)}; }

	[[nodiscard]] constexpr Sec global_time() const noexcept{ return Sec{static_cast<T>(globalTime)}; }
	[[nodiscard]] constexpr Sec update_time() const noexcept{ return Sec{static_cast<T>(updateTime)}; }

	template <typename Ty = T>
	[[nodiscard]] constexpr Tick global_delta_tick() const noexcept{
		return direct_access_time_unit<Ty, tick_ratio>{static_cast<Ty>(globalDelta * tick_ratio::den)};
	}

	template <typename Ty = T>
	[[nodiscard]] constexpr Tick update_delta_tick() const noexcept{
		return direct_access_time_unit<Ty, tick_ratio>{static_cast<Ty>(updateDelta * tick_ratio::den)};
	}
};
}
