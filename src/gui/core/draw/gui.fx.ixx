module;

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.fx;

import std;
export import mo_yanxi.math;

namespace mo_yanxi::gui::fx{
export
FORCE_INLINE [[nodiscard]] constexpr unsigned get_smooth_circle_vertex_count(float radius, float percent, float max_error = 1.f) noexcept {

	CHECKED_ASSUME(max_error > 0.f);
	CHECKED_ASSUME(percent >= 0.f);

	if (radius <= 0.0f) [[unlikely]] {
		return 0;
	}

	constexpr float TWO_PI = math::pi_2;


	float p = percent > 1.0f ? 1.0f : percent;




	float arc_length = TWO_PI * radius * p;

	auto segments = static_cast<unsigned>(arc_length / max_error) + 1;



	return math::clamp(segments + 1U, 3U, 512U);
}

}
