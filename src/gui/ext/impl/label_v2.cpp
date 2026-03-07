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
void layout_record<typesetting::glyph_layout>::record_glyph_draw_instructions(
	graphic::draw::instruction::draw_record_storage<mr::heap_allocator<std::byte>>& buffer,
	const typesetting::glyph_layout& layout,
	graphic::color color_scl, typesetting::content_alignment line_align
){
	using namespace mo_yanxi::graphic;
	using namespace mo_yanxi::graphic::draw::instruction;

	buffer.clear();

	for (const auto & current_line : layout.lines){
		auto [line_src, spacing] = current_line.calculate_alignment(layout.extent, line_align, layout.direction);
		for (const auto & [idx, layout_result] : std::span{layout.elems.begin() + current_line.glyph_range.pos, current_line.glyph_range.size} | std::views::enumerate){
			if(!layout_result.texture->view)continue;
			auto start = math::fma(idx, spacing, line_src + layout_result.aabb.src);
			buffer.push(rect_aabb{
				.generic = {layout_result.texture->view},
				.v00 = start,
				.v11 = start + layout_result.aabb.extent(),
				.uv00 = layout_result.texture->uv.v00(),
				.uv11 = layout_result.texture->uv.v11(),
				.vert_color = {layout_result.color * color_scl}
			});
		}

		for (const auto & ul : std::span{layout.underlines.begin() + current_line.underline_range.pos, current_line.underline_range.size}){

			const auto start = math::fma(spacing, static_cast<float>(ul.start_gap_count), line_src + ul.start);
			const auto end = math::fma(spacing, static_cast<float>(ul.end_gap_count), line_src + ul.end);

			buffer.push(line{
				.src = start,
				.dst = end,
				.color = {ul.color * color_scl, ul.color * color_scl},
				.stroke = ul.thickness,
			});
		}
	}

}

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
	if(fit_){
		mat.set_rect_transform({}, glyph_layout_.extent, get_glyph_src_abs(), reg_ext);
	} else{
		mat = math::mat3_idt;
		mat.set_translation(get_glyph_src_abs());
	}

	state_guard guard{
		renderer(),
		fx::batch_draw_mode::msdf,
		graphic::draw::instruction::make_state_tag(fx::state_type::push_constant, VK_SHADER_STAGE_FRAGMENT_BIT)
		};

	transform_guard _t{renderer(), mat};
	push_text_draw_buffer();

}

}
