module mo_yanxi.gui.infrastructure;

import mo_yanxi.graphic.draw.instruction;

namespace mo_yanxi::gui{

void cursor_collection::draw(scene& scene) const{
	rect region;

	if(current_drawers_.main){
		current_drawers_.main->draw(scene.renderer(), {scene.get_cursor_pos(), cursor_size_}, scene.get_inbounds());
		region = current_drawers_.main->get_bound({scene.get_cursor_pos(), cursor_size_}, scene.get_inbounds());
	}else{
		assets::builtin::default_cursor.draw(scene.renderer(), {scene.get_cursor_pos(), cursor_size_}, scene.get_inbounds());
		region = assets::builtin::default_cursor.get_bound({scene.get_cursor_pos(), cursor_size_}, scene.get_inbounds());
	}
	if(current_drawers_.dcor){
		current_drawers_.dcor->draw(scene.renderer(), {scene.get_cursor_pos(), cursor_size_}, scene.get_inbounds());
		region.expand_by(current_drawers_.dcor->get_bound({scene.get_cursor_pos(), cursor_size_}, scene.get_inbounds()));
	}


	scene.renderer().update_state(graphic::draw::instruction::state_push_config{
			graphic::draw::instruction::state_push_target::immediate
	}, gui::fx::blit_config{
		{
			.src = region.src.as<int>(),
			.extent = region.extent().as<int>()
		},
		{.pipeline_index = 1}});
}

namespace assets::builtin{
using namespace graphic::draw::instruction;

void default_cursor_regular::draw(renderer_frontend& renderer, math::raw_frect region,
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
}

}

}


