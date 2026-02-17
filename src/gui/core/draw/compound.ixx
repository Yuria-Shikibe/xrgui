//
// Created by Matrix on 2026/2/13.
//

export module mo_yanxi.gui.fx.compound;

import std;
export import mo_yanxi.math;

namespace mo_yanxi::gui::fx::compound{
template<typename R, typename Value = float>
concept arth_range = std::ranges::forward_range<R> && std::convertible_to<std::ranges::range_value_t<R>, Value>;

export
template <std::regular PointTy, arth_range<float> Range = std::initializer_list<float>, std::invocable<math::section<PointTy>> Fn>
constexpr void dash_line(math::section<PointTy> segment, const Range& pattern, Fn fn) noexcept {
	if (std::ranges::empty(pattern)) {
		std::invoke(fn, segment);
		return;
	}

	auto delta = segment.delta();
	auto dst = math::abs(delta);
	if (dst <= 0.001f) return;

	auto unit = delta / dst;

	float currentDist{};
	bool isSolid = true;

	for(auto&& range : std::views::repeat(pattern)){
		for (float segLen : range){
			if (currentDist >= dst) return;

			segLen = math::max<float>(segLen, 1);


			auto nextDist = math::min(currentDist + segLen, dst);

			if (isSolid) {
				auto cur = math::fma(unit, currentDist, segment.from);
				auto next = math::fma(unit, nextDist, segment.from);
				std::invoke(fn, math::section<PointTy>{cur, next});
			}

			currentDist = nextDist;
			isSolid = !isSolid;
		}
	}
}
}