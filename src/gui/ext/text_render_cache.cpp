module mo_yanxi.gui.text_render_cache;

import mo_yanxi.graphic.draw.instruction;


namespace mo_yanxi::gui{

void record_glyph_draw_instructions(
	graphic::draw::instruction::draw_record_storage<mr::heap_allocator<std::byte>>& buffer,
	const typesetting::glyph_layout& glyph_layout, graphic::color color_scl, typesetting::line_alignment line_align){
	using namespace mo_yanxi::graphic;
	using namespace mo_yanxi::graphic::draw::instruction;

	buffer.clear();
	buffer.reserve_heads(glyph_layout.elems.size() + glyph_layout.underlines.size());
	buffer.reserve_bytes(glyph_layout.elems.size() * sizeof(rect_aabb) + glyph_layout.underlines.size() * sizeof(line));

	for(const auto& current_line : glyph_layout.lines){
		auto [line_src, spacing] = current_line.calculate_alignment(glyph_layout.extent, line_align,
			glyph_layout.direction);

		for(const auto& [idx, val] : std::span{
			    glyph_layout.elems.begin() + current_line.glyph_range.pos, current_line.glyph_range.size
		    } | std::views::enumerate){
			if(!val.texture->view) continue;
			auto start = math::fma(idx, spacing, line_src + val.aabb.src);
			buffer.push(rect_aabb{
					.generic = {val.texture->view},
					.v00 = start,
					.v11 = start + val.aabb.extent(),
					.uv00 = val.texture->uv.v00(),
					.uv11 = val.texture->uv.v11(),
					.vert_color = {val.color * color_scl}
				});
		}

		// for(const auto& [idx, val] : std::span{
		// 	    glyph_layout.clusters.begin() + current_line.cluster_range.pos, current_line.cluster_range.size
		//     } | std::views::enumerate){
		// 	auto start = line_src + val.logical_rect.src;
		//
		//
		// 	buffer.push(rect_aabb_outline{
		// 			.v00 = start,
		// 			.v11 = start + val.logical_rect.extent(),
		// 			.stroke = {1.25f},
		// 			.vert_color = {graphic::colors::YELLOW.copy_set_a(.5f)}
		// 		});
		// }

		for(const auto& ul : std::span{
			    glyph_layout.underlines.begin() + current_line.underline_range.pos, current_line.underline_range.size
		    }){
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
}
