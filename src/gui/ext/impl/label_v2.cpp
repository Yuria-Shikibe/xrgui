module mo_yanxi.gui.elem.label_v2;

import mo_yanxi.gui.renderer.frontend;
import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.math.matrix3;
import mo_yanxi.math.vector2;
import mo_yanxi.math;

namespace mo_yanxi::gui{
void layout_record<typesetting::glyph_layout>::record_glyph_draw_instructions(
	graphic::draw::instruction::draw_record_storage<mr::heap_allocator<std::byte>>& buffer,
	const typesetting::glyph_layout& layout,
	graphic::color color_scl
	){
	using namespace mo_yanxi::graphic;
	using namespace mo_yanxi::graphic::draw::instruction;
	color tempColor{};

	buffer.clear();

	for (const auto & layout_result : layout.elems){
		if(!layout_result.texture->view)continue;
		buffer.push(rect_aabb{
			.generic = {layout_result.texture->view},
			.v00 = layout_result.aabb.get_src(),
			.v11 = layout_result.aabb.get_end(),
			.uv00 = layout_result.texture->uv.v00(),
			.uv11 = layout_result.texture->uv.v11(),
			.vert_color = {layout_result.color}
		});
	}

	for (const auto & layout_result : layout.underlines){
		buffer.push(line{
			.src = layout_result.start,
			.dst = layout_result.end,
			.color = {layout_result.color, layout_result.color},
			.stroke = layout_result.thickness,
		});
	}
}

void label_v2::draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const{
	text_holder::draw_layer(clipSpace, param);
	draw_style(param);
	if(param == 0)draw_text();
}

void label_v2::draw_text() const{
	math::mat3 mat;
	math::vec2 reg_ext = get_glyph_draw_extent();
	if(fit_){
		mat.set_rect_transform({}, glyph_layout_.extent, get_glyph_src_abs(), reg_ext);
	} else{
		mat = math::mat3_idt;
		mat.set_translation(get_glyph_src_abs());
	}

	auto& renderer = get_scene().renderer();
	{
		transform_guard _t{renderer, mat};
		push_text_draw_buffer();
	}
}
//
// void async_label_terminal::on_update(const exclusive_glyph_layout& data){
// 	terminal<exclusive_glyph_layout>::on_update(data);
// 	if(!data->extent().equals(label->content_extent(), 1)){
// 		label->extent_state_ = async_label::layout_extent_state::waiting_correction;
// 		label->notify_layout_changed(propagate_mask::local | propagate_mask::force_upper);
// 	}else{
// 		label->extent_state_ = async_label::layout_extent_state::valid;
// 	}
//
// 	label->last_layout_extent_ = data->extent();
// 	label->update_draw_buffer(*data);
// }
//
// void label::draw_layer(const rect clipSpace, gfx_config::layer_param_pass_t param) const{
// 	draw_style(param);
// 	if(param == 0)draw_text();
// }
//
//
// void async_label::draw_layer(const rect clipSpace, gfx_config::layer_param_pass_t param) const{
// 	draw_style(param);
//
// 	if(!terminal)return;
// 	if(extent_state_ != async_label::layout_extent_state::valid)return;
//
// 	auto& renderer = get_scene().renderer();
//
// 	using namespace graphic;
// 	using namespace graphic::draw::instruction;
//
// 	math::mat3 mat;
// 	const auto reg_ext = align::embed_to(align::scale::fit, last_layout_extent_, content_extent());
// 	mat.set_rect_transform({}, last_layout_extent_, content_src_pos_abs(), reg_ext);
//
// 	{
// 		transform_guard _t{renderer, mat};
// 		push_text_draw_buffer();
// 	}
// }


}
