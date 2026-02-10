module mo_yanxi.gui.elem.label;

import mo_yanxi.gui.renderer.frontend;
import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.math.matrix3;
import mo_yanxi.math.vector2;
import mo_yanxi.math;

namespace mo_yanxi::gui{
void layout_record<font::typesetting::glyph_layout>::record_glyph_draw_instructions(
	graphic::draw::instruction::draw_record_storage<mr::heap_allocator<std::byte>>& buffer,
	const font::typesetting::glyph_layout& layout,
	graphic::color color_scl
	){
		using namespace mo_yanxi::graphic;
		color tempColor{};

		static constexpr auto instr_sz = draw::instruction::get_payload_size<draw::instruction::rect_aabb>();
		buffer.clear();

		for(const auto& row : layout.rows()){
			const auto lineOff = row.src;
			for(auto&& glyph : row.glyphs){
				if(!glyph.glyph) continue;
				tempColor = glyph.color * color_scl;

				if(glyph.code.code == U'\0'){
					tempColor.mul_a(.65f);
				}

				const auto region = glyph.get_draw_bound().move(lineOff);

				buffer.push(draw::instruction::rect_aabb{
					.generic = {
						.image = glyph.glyph->view,
					},
					.v00 = region.vert_00(),
					.v11 = region.vert_11(),
					.uv00 = glyph.glyph->uv.v00(),
					.uv11 = glyph.glyph->uv.v11(),
					.vert_color = tempColor
				});
				// buffer.push(draw::instruction::rect_aabb_outline{
				// 	.v00 = lineOff + glyph.region.vert_00(),
				// 	.v11 = lineOff + glyph.region.vert_11(),
				// 	.stroke = {2},
				// 	.vert_color = tempColor
				// });
			}
		}
	}

void draw_glyph_draw_instructions(
	renderer_frontend& renderer,
	const font::typesetting::glyph_layout& layout,
	graphic::color color_scl,
	math::vec2 scale,
	math::vec2 offset
	){
	using namespace mo_yanxi::graphic;
	color tempColor{};

	for(const auto& row : layout.rows()){
		const auto lineOff = row.src;
		for(auto&& glyph : row.glyphs){
			if(!glyph.glyph) continue;
			tempColor = glyph.color * color_scl;

			if(glyph.code.code == U'\0'){
				tempColor.mul_a(.65f);
			}

			const auto region = glyph.get_draw_bound().move(lineOff);

			renderer.push(draw::instruction::rect_aabb{
				.generic = {
					.image = glyph.glyph->view,
				},
				.v00 = math::cpo::fma(region.vert_00(), scale, offset),
				.v11 = math::cpo::fma(region.vert_11(), scale, offset),
				.uv00 = glyph.glyph->uv.v00(),
				.uv11 = glyph.glyph->uv.v11(),
				.vert_color = tempColor
			});
		}
	}

}


void sync_label_terminal::on_update(react_flow::data_pass_t<std::string> data){
	terminal<std::string>::on_update(data);
	label_->set_text(data.get());
}


std::optional<mo_yanxi::font::typesetting::layout_pos_t> label::get_layout_pos(
	const math::vec2 globalPos) const{
	if(glyph_layout.empty()/* || !contains(globalPos)*/){
		return std::nullopt;
	}

	using namespace font::typesetting;
	auto textLocalPos = globalPos - get_glyph_src_local();

	auto row =
		std::ranges::lower_bound(
			glyph_layout.rows(), textLocalPos.y,
			{}, &glyph_layout::row::bottom);


	if(row == glyph_layout.rows().end()){
		if(glyph_layout.rows().empty()){
			return std::nullopt;
		} else{
			row = std::ranges::prev(glyph_layout.rows().end());
		}
	}

	auto elem = row->line_nearest(textLocalPos.x);

	if(elem == row->glyphs.end() && !row->glyphs.empty()){
		elem = std::prev(row->glyphs.end());
	}

	return layout_pos_t{
			static_cast<layout_pos_t::value_type>(std::ranges::distance(row->glyphs.begin(), elem)),
			static_cast<layout_pos_t::value_type>(std::ranges::distance(glyph_layout.rows().begin(), row))
		};
}

void label::draw_text() const{
	math::mat3 mat;
	math::vec2 reg_ext = get_glyph_draw_extent();
	if(fit_){
		mat.set_rect_transform({}, glyph_layout.extent(), get_glyph_src_abs(), reg_ext);
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

void label::draw_layer(const rect clipSpace, gfx_config::layer_param_pass_t param) const{
	draw_style(param);
	if(param == 0)draw_text();
}


}
