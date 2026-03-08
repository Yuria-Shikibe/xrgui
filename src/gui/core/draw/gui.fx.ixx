module;

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.fx;

import std;
export import mo_yanxi.math;

namespace mo_yanxi::gui::fx{
export
FORCE_INLINE [[nodiscard]] constexpr unsigned get_smooth_circle_vertex_count(float radius, float percent, float max_error = 1.f) noexcept {
	// 处理异常输入，避免除以零或生成负数顶点
	CHECKED_ASSUME(max_error > 0.f);
	CHECKED_ASSUME(percent >= 0.f);

	if (radius <= 0.0f) [[unlikely]] {
		return 0;
	}

	constexpr float TWO_PI = math::pi_2;

	// 限制绘制比例最大为 1.0 (即 100% 的完整圆)
	float p = percent > 1.0f ? 1.0f : percent;

	// 核心近似逻辑：
	// 在细分程度较高时，圆弧的“弧长”极度接近“弦长”。
	// 因为 弧长 > 弦长，所以只要保证每段弧长 < max_error，其对应的弦长就必然 < max_error。
	float arc_length = TWO_PI * radius * p;

	auto segments = static_cast<unsigned>(arc_length / max_error) + 1;

	// 返回顶点数：N 个线段需要 N + 1 个圆弧上的顶点
	// (注：如果你的绘制模式是 Triangle Fan 以圆心为中心构建扇形，实际缓冲区的顶点数在外部还需要 +1)
	return math::clamp(segments + 1U, 3U, 512U);
}

}
