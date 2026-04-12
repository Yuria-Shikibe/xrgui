module;

module mo_yanxi.gui.infrastructure;

import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.gui.fx.instruction_extension;
import mo_yanxi.gui.fx.fringe;

namespace mo_yanxi::gui{
 constexpr inline float fringe_range = 2.f;

 rect cursor_drawer::draw(gui::scene& scene, vec2 cursor_size_) const{
	rect region{};

	if(main){
		region = main->draw(scene.renderer(), {scene.get_cursor_pos(), cursor_size_}, scene.get_inbounds());
	}

	for (auto dcor : dcor){
		if(!dcor)break;
		auto reg = dcor->draw(scene.renderer(), {scene.get_cursor_pos(), cursor_size_}, scene.get_inbounds());
		region.expand_by(reg);
	}

 	return region;
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

	vec2 center = src + extent * 0.5f;


	float min_dim = math::min(extent.x, extent.y);


	float radius = min_dim * 0.072f;



	float spacing_x = min_dim * 0.4f;
	float spacing_y = min_dim * 0.4f;


	float left_x = center.x - spacing_x * 0.5f;
	float right_x = center.x + spacing_x * 0.5f;


	float top_y = center.y - spacing_y;
	float mid_y = center.y;
	float bottom_y = center.y + spacing_y;


	return drag_icon_layout{
		std::array{
			vec2{left_x, top_y},    vec2{right_x, top_y},
			vec2{left_x, mid_y},    vec2{right_x, mid_y},
			vec2{left_x, bottom_y}, vec2{right_x, bottom_y}
		},
		radius
	};
}

rect default_cursor_arrow::draw(gui::renderer_frontend& renderer, math::raw_frect region,
	std::span<const elem* const> inbound_stack) const{
	region.src -= region.extent * .5f;

	auto rst = style::calculate_rect_arrow(region.src, region.extent, direction);
	fx::fringe::inplace_line_context<(7 + 4) * 2> context{};
	context.push(rst.p1, 1.8f, graphic::colors::white);
	context.push(rst.p2, 1.8f, graphic::colors::white);
	context.push(rst.p3, 1.8f, graphic::colors::white);
	context.add_cap();
	context.add_fringe_cap(fringe_range, fringe_range);
	context.dump_mid(renderer, line_segments{});
	context.dump_fringe_inner(renderer, line_segments{}, fringe_range);
	context.dump_fringe_outer(renderer, line_segments{}, fringe_range);

	return style::calculate_arrow_aabb(rst);
}

rect default_cursor_drag::draw(gui::renderer_frontend& renderer, math::raw_frect region,
	std::span<const elem* const> inbound_stack) const{
	region.src -= region.extent * .5f;
	region.expand({-2.5, -2.5});

	auto [ps, radius] = calculate_drag_icon(region.src, region.extent);
	fx::circle circle{
		.radius = {0, radius},
		.color = {graphic::colors::white, graphic::colors::white}
	};
	for (const auto & p : ps){
		circle.pos = p;
		fx::fringe::poly(renderer, circle, fringe_range);
	}

	return get_bound(region);
}

rect default_cursor_regular::draw(renderer_frontend& renderer, math::raw_frect region,
	std::span<const elem* const> inbound_stack) const{
	static constexpr float stroke = fringe_range;
	static constexpr float scl = .75f;


	math::section<graphic::float4> color{graphic::colors::white.copy_set_a(0), graphic::colors::white};



	const auto verts = std::to_array({
		region.src + math::vec2{0, region.extent.y * .75f * scl },
		region.src,
		region.src + math::vec2{region.extent.x * .225f * scl, region.extent.y * .5f * scl},
		region.src + region.extent * (1.18f / 2) * scl,
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


