module;

#include <vulkan/vulkan.h>

module mo_yanxi.gui.elem.label_v2;

import mo_yanxi.gui.renderer.frontend;
import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.graphic.draw.instruction.general;
import mo_yanxi.math.matrix3;
import mo_yanxi.math.vector2;
import mo_yanxi.math;

namespace mo_yanxi::gui{

void label_v2_text_prov::on_update(react_flow::data_carrier<std::string>& data){
	label->set_text(data.get());
}

void label_v2::draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const{
	text_holder::draw_layer(clipSpace, param);
	draw_style(param);
	if(param == 0)draw_text();
}

void label_v2::draw_text() const{
	if(!has_drawable_text())return;

	math::mat3 mat;
	math::vec2 reg_ext = get_glyph_draw_extent();
	math::vec2 localpos = util::transform_scene2local(*this, get_scene().get_cursor_pos()) - get_glyph_src_local();
	if(fit_){
		mat.set_rect_transform({}, glyph_layout_.extent, get_glyph_src_abs(), reg_ext);
	} else{
		mat = math::mat3_idt;
		mat.set_translation(get_glyph_src_abs());
	}

	auto hit = glyph_layout_.hit_test(localpos, text_line_align);

	state_guard guard{
		renderer(),
		fx::batch_draw_mode::msdf,
		graphic::draw::instruction::make_state_tag(fx::state_type::push_constant, VK_SHADER_STAGE_FRAGMENT_BIT)
		};

	transform_guard _t{renderer(), mat};
	push_text_draw_buffer();
	renderer() << graphic::draw::instruction::rect_aabb_outline{
		.v00 = {},
		.v11 = reg_ext,
		.stroke = {2},
		.vert_color = {graphic::colors::CRIMSON.copy_set_a(.6f)}
	};
	if(hit){
		auto src = hit.source_line->calculate_alignment(glyph_layout_.extent, text_line_align, glyph_layout_.direction);
		renderer() << graphic::draw::instruction::rect_aabb{
			.v00 = src.start_pos + hit.source->logical_rect.vert_00(),
			.v11 = src.start_pos + hit.source->logical_rect.vert_11(),
			.vert_color = {graphic::colors::ACID.copy_set_a(.6f)}
		};
	}

}

}
