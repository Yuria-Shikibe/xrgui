module;

#ifndef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
#include <beman/inplace_vector.hpp>
#endif

module mo_yanxi.gui.infrastructure;

import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.gui.fx.instruction_extension;
import mo_yanxi.gui.fx.fringe;

#ifdef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
import <beman/inplace_vector.hpp>;
#endif

namespace mo_yanxi::gui{

void cursor_collection::draw(scene& scene) const{
	rect region{};

	if(current_drawers_.main){
		region = current_drawers_.main->draw(scene.renderer(), {scene.get_cursor_pos(), cursor_size_}, scene.get_inbounds());
	}else{
		region = assets::builtin::cursor::default_cursor.draw(scene.renderer(), {scene.get_cursor_pos(), cursor_size_}, scene.get_inbounds());
	}

	for (auto dcor : current_drawers_.dcor){
		if(!dcor)break;
		auto reg = dcor->draw(scene.renderer(), {scene.get_cursor_pos(), cursor_size_}, scene.get_inbounds());
		region.expand_by(reg);
	}

	scene.renderer().update_state(gui::fx::blit_config{
		{
			.src = region.src.as<int>(),
			.extent = region.extent().as<int>()
		},
		{.pipeline_index = 2}});
}

namespace assets::builtin::cursor{
using namespace graphic::draw::instruction;

struct drag_icon_layout {
	std::array<vec2, 6> points;
	float radius;
};

/**
 * @brief 计算矩形内拖拽图标(2x3点阵)的 6 个圆心位置和半径
 * @param src 矩形的左上角/起始坐标
 * @param extent 矩形的尺寸(宽高)
 * @return 包含 6 个点的 std::array 和 1 个 float 半径的结构体
 */
constexpr drag_icon_layout calculate_drag_icon(const vec2& src, const vec2& extent) {
	// 1. 计算矩形的中心点
	vec2 center = src + extent * 0.5f;

	// 2. 获取宽高中的较小值，以保证点阵等比缩放且不越界
	float min_dim = math::min(extent.x, extent.y);

	// 3. 设定圆点的半径 (例如：取较短边的 8%)
	float radius = min_dim * 0.09f;

	// 4. 设定点阵在 X 和 Y 方向上的间距 (例如：取较短边的 35%)
	// 这样能够保证点阵本身是个规整的网格，而不会因为外层矩形被拉伸而变形
	float spacing_x = min_dim * 0.385f;
	float spacing_y = min_dim * 0.385f;

	// 5. 计算左右两列的 X 坐标
	float left_x = center.x - spacing_x * 0.5f;
	float right_x = center.x + spacing_x * 0.5f;

	// 6. 计算上、中、下三行的 Y 坐标
	float top_y = center.y - spacing_y;
	float mid_y = center.y;
	float bottom_y = center.y + spacing_y;

	// 7. 组装并返回 6 个点的数据
	return drag_icon_layout{
		std::array{
			vec2{left_x, top_y},    vec2{right_x, top_y},    // 第一行
			vec2{left_x, mid_y},    vec2{right_x, mid_y},    // 第二行
			vec2{left_x, bottom_y}, vec2{right_x, bottom_y}  // 第三行
		},
		radius
	};
}

rect default_cursor_arrow::draw(gui::renderer_frontend& renderer, math::raw_frect region,
	std::span<const elem* const> inbound_stack) const{
	region.src -= region.extent * .5f;

	auto rst = style::calculate_rect_arrow(region.src, region.extent, direction);
	fx::fringe::line_context context{std::in_place_type<beman::inplace_vector::inplace_vector<line_node, (7 + 4) * 2>>};
	context.push(rst.p1, 3, graphic::colors::white);
	context.push(rst.p2, 3, graphic::colors::white);
	context.push(rst.p3, 3, graphic::colors::white);
	context.add_cap();
	context.add_fringe_cap();
	context.dump_mid(renderer, line_segments{});
	context.dump_fringe_inner(renderer, line_segments{});
	context.dump_fringe_outer(renderer, line_segments{});

	return style::calculate_arrow_aabb(rst);
}

rect default_cursor_drag::draw(gui::renderer_frontend& renderer, math::raw_frect region,
	std::span<const elem* const> inbound_stack) const{
	region.src -= region.extent * .5f;

	auto [ps, radius] = calculate_drag_icon(region.src, region.extent);
	fx::circle circle{
		.radius = radius,
		.color = {graphic::colors::white, graphic::colors::white}
	};
	for (const auto & p : ps){
		circle.pos = p;
		fx::fringe::poly(renderer, circle);
	}

	return get_bound(region);
}

rect default_cursor_regular::draw(renderer_frontend& renderer, math::raw_frect region,
	std::span<const elem* const> inbound_stack) const{
	static constexpr float stroke = 1.35f;


	math::section<graphic::float4> color{graphic::colors::white.copy_set_a(0), graphic::colors::white};

	const auto verts = std::to_array({
		region.src + math::vec2{0, region.extent.y * .75f},
		region.src,
		region.src + math::vec2{region.extent.x * .225f, region.extent.y * .5f},
		region.src + region.extent * (1.18f / 2),
	});

	//Body
	renderer.push(quad{
			.vert = verts,
			.vert_color = {graphic::colors::white}
		});

	//AA
	renderer.push(line_segments_closed{
		line_segments{
			.generic = {}
		}
	}, line_node{
		.pos = verts[0],
		.stroke = stroke,
		.offset = stroke * -.5f,
		.color = color,
	}, line_node{
		.pos = verts[1],
		.stroke = stroke,
		.offset = stroke * -.5f,
		.color = color,
	}, line_node{
		.pos = verts[3],
		.stroke = stroke,
		.offset = stroke * -.5f,
		.color = color,
	}, line_node{
		.pos = verts[2],
		.stroke = stroke,
		.offset = stroke * -.5f,
		.color = color,
	});

	return get_bound(region);
}

}

}


