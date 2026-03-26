module;

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.fx.compound;

import std;
export import mo_yanxi.math;
export import mo_yanxi.math.vector2;
export import mo_yanxi.math.rect_ortho;

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

struct arrow_info{
	std::array<math::vec2, 3> vertices;
	float thick;
};

export
FORCE_INLINE CONST_FN [[nodiscard]] arrow_info generate_centered_arrow(
	const math::vec2 extent,
	float stroke,
	float length) noexcept
{
	static constexpr float INV_SQRT2 = math::sqrt2_inv;

	// --- 步骤 1: 计算原始尺寸下的完整包围盒 ---

	// 水平总宽度 = length的水平投影 + 尖端外扩 + 末端平角的水平投影
	float req_w = (length + 1.5f * stroke) * INV_SQRT2;
	// 垂直总高度 = 2 * (length的垂直投影 + 末端平角的垂直投影)
	float req_h = (2.0f * length + stroke) * INV_SQRT2;

	// --- 步骤 2: 自适应缩放 (与矩形尺寸对比) ---
	float scale_x = extent.x / req_w;
	float scale_y = extent.y / req_h;
	float scale = std::min({1.0f, scale_x, scale_y});

	float final_length = length * scale;
	float final_stroke = stroke * scale;

	float tip_x = (final_length - 0.5f * final_stroke) * INV_SQRT2 * 0.5f;
	float tip_y = 0.0f; // Y 轴天然对称，直接为 0

	// 计算两臂末端相对 Tip 的偏移
	float arm_dx = -final_length * INV_SQRT2; // 向左延伸
	float arm_dy = final_length * INV_SQRT2;  // 上下对称延伸

	std::array vertices = {
		math::vec2{tip_x + arm_dx, tip_y - arm_dy}, // 顶部的点 (Top)
		math::vec2{tip_x, tip_y},                   // 尖端节点 (Tip, 朝右)
		math::vec2{tip_x + arm_dx, tip_y + arm_dy}  // 底部的点 (Bottom)
	};

	return arrow_info{vertices, final_stroke};
}
}