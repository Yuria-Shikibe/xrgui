module;

#include <vulkan/vulkan.h>

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
	if(!has_drawable_text())return;

	math::mat3 mat;
	math::vec2 reg_ext = get_glyph_draw_extent();
	if(fit_){
		mat.set_rect_transform({}, glyph_layout_.extent, get_glyph_src_abs(), reg_ext);
	} else{
		mat = math::mat3_idt;
		mat.set_translation(get_glyph_src_abs());
	}

	renderer().push_constant({}, {.vk = {VK_SHADER_STAGE_FRAGMENT_BIT}}, fx::draw_mode::msdf);

	{
		transform_guard _t{renderer(), mat};
		push_text_draw_buffer();
	}
}

}
