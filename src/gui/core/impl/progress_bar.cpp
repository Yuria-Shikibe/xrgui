module mo_yanxi.gui.elem.progress_bar;

import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.gui.fx.fringe;

namespace mo_yanxi::gui{

unsigned GetSmoothCircleVertexCount(float radius, float percent, float max_error = 1.f) {
	if (radius <= max_error) {
		return 3;
	}

	float half_angle = std::acos(1.0f - (max_error / radius));
	int vertex_count = static_cast<int>(std::ceil(math::pi / half_angle) * percent);

	return std::clamp(vertex_count, 3, 4096);
}

// int GetSmoothCircleVertexCountByArcLength(float radius, float circle_percent = 2.0f) {
// 	// 基础校验：防止非法的半径或弧长参数导致除零或负数
// 	if (radius <= 0.0f || circle_percent <= 0.0f) {
// 		return 3;
// 	}
//
// 	// 计算总周长
// 	float circumference = 2.0f * math::pi * radius;
//
// 	// 计算所需顶点数并向上取整
// 	int vertex_count = static_cast<int>(std::ceil(circumference / (circle_percent * radius)));
//
// 	// 限制范围：最少3个点（三角形），最大限制在4096个点以保障内存和渲染性能
// 	return std::clamp(vertex_count, 3, 4096);
// }

struct SectorLayout {
	float radius;
	math::vec2 center;
};

bool is_angle_inside(float alpha, float theta1, float delta_theta) {
    auto diff = std::fmod(alpha - theta1, math::pi_2);
    if (diff < 0.0) {
        diff += math::pi_2;
    }
    // 加入 1e-9 的容差处理浮点数精度误差 (根据你之前的设定，这里用1e-5)
    return diff <= delta_theta + 1e-5;
}

SectorLayout calculateSectorFit(float w, float h, float a1, float a2) {
    // 【新增核心逻辑】：处理负数跨度，将顺时针扫描等效转化为逆时针扫描
    if (a2 < 0.0f) {
        a1 = a1 + a2; // 起点后退
        a2 = -a2;     // 跨度取正
    }

    // 此时 a2 必然 >= 0，只需限制最大为 1.0 (一整圈)
    a2 = std::min(1.0f, a2);

    const auto theta1 = a1 * math::pi_2;
    const auto delta_theta = a2 * math::pi_2;
    const auto theta2 = theta1 + delta_theta;

    const auto [cos1, sin1] = math::cos_sin(theta1);
    const auto [cos2, sin2] = math::cos_sin(theta2);

    auto x_min = std::min({0.0f, cos1, cos2});
    auto x_max = std::max({0.0f, cos1, cos2});
    auto y_min = std::min({0.0f, sin1, sin2});
    auto y_max = std::max({0.0f, sin1, sin2});

    // 步骤 2: 检查坐标轴极值点是否在扇形圆弧上
    // 如果 a2 >= 1.0，直接是一个整圆
    if (a2 >= 1.0f) {
        x_min = -1.0f; x_max = 1.0f;
        y_min = -1.0f; y_max = 1.0f;
    } else {
        if (is_angle_inside(0.0f, theta1, delta_theta)) {
            x_max = std::max(x_max, 1.0f);
        }
        if (is_angle_inside(math::pi / 2.0f, theta1, delta_theta)) {
            y_max = std::max(y_max, 1.0f);
        }
        if (is_angle_inside(math::pi, theta1, delta_theta)) {
            x_min = std::min(x_min, -1.0f);
        }
        if (is_angle_inside(1.5f * math::pi, theta1, delta_theta)) {
            y_min = std::min(y_min, -1.0f);
        }
    }

    // 计算单位包围盒的长宽
    const auto B_x = x_max - x_min;
    const auto B_y = y_max - y_min;

    // 步骤 3: 计算允许的最大半径 R
    constexpr auto INF = std::numeric_limits<float>::infinity();
    auto R_x = (B_x > 0.0001f) ? (w / B_x) : INF;
    auto R_y = (B_y > 0.0001f) ? (h / B_y) : INF;
    auto R = std::min(R_x, R_y);

    // 计算圆心将其放置在矩形内（这里取可用区间的正中点）
    auto x_c = w / 2.f - R * (x_min + x_max) / 2.f;
    auto y_c = h / 2.f - R * (y_min + y_max) / 2.f;

    // 根据你的结构体定义，返回 vec2 格式的 center
    return {R, {x_c, y_c}};
}

void style::progress_drawer_flat::draw_layer_impl(const progress_bar& element, math::frect region, float opacityScl,
	fx::layer_param layer_param) const{
	if(layer_param == 0){
		using namespace graphic;

		auto prog = element.progress.get_current();

		element.get_scene().renderer().push(draw::instruction::rect_aabb{
			.v00 = region.vert_00(),
			.v11 = region.vert_00() + region.extent().mul_x(prog),
			.vert_color = {
				element.draw_config.color.src,element.draw_config.color[prog],
				element.draw_config.color.src,element.draw_config.color[prog]
			}
		});
	}
}

void style::progress_drawer_arc::draw_layer_impl(const progress_bar& element, math::frect region, float opacityScl,
	fx::layer_param layer_param) const{
	const auto prog = element.progress.get_current();
	using namespace graphic;


	const auto extent = align::embed_to(align::scale::fit, {1, 1}, region.extent());
	const auto [Rraw, center] = calculateSectorFit(extent.x, extent.y, angle_range.base, angle_range.extent);
	const auto radius = math::fdim(Rraw, 6.f);
	const auto total = math::abs(this->radius.dst());
	const auto inner =  this->radius.base / total * radius;
	const auto outer = inner + this->radius.extent / total * radius;

	const auto off = align::get_offset_of(align, extent, region);

	const auto cap_size = math::curve(prog, 0.f, 0.05f);


	element.get_scene().renderer().push(draw::instruction::rect_aabb_outline{
		.v00 = off,
		.v11 = off + extent,
		.stroke = {2},
		.vert_color = {
			colors::YELLOW.copy_set_a(.55f)
		}
	});

	if(prog < 0.001f)return;

	auto count = GetSmoothCircleVertexCount(radius, math::abs(prog * angle_range.extent), .5f);

	if(prog == 1.f && angle_range.extent >= 1.f){
		fx::fringe::poly(element.renderer(), draw::instruction::poly{
			.pos = center + off,
			.segments = count,
			.initial_angle = angle_range.base,
			.radius = {inner, outer},
			.color = {
				element.draw_config.color.src, element.draw_config.color[prog],
			}
		}, 1.5f);
		return;
	}

	fx::fringe::poly_partial_with_cap(element.renderer(), draw::instruction::poly_partial{
			.pos = center + off,
			.segments = count,
			.range = {angle_range.base, angle_range.extent * prog},
			.radius = {inner, outer},
			.color = {
				element.draw_config.color.src, element.draw_config.color.src,
				element.draw_config.color[prog], element.draw_config.color[prog]
			}
		}, 1.5f * cap_size, 1.5f* cap_size, 1.5f);
}
}
