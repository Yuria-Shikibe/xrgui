module mo_yanxi.gui.text_render_cache;

import mo_yanxi.gui.image_regions;
import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.gui.fx.instruction_extension;


namespace mo_yanxi::gui{

void record_glyph_draw_instructions(
	graphic::draw::instruction::draw_record_storage<mr::heap_allocator<std::byte>>& buffer,
	const typesetting::glyph_layout& glyph_layout, graphic::color color_scl, typesetting::line_alignment line_align){
	using namespace mo_yanxi::graphic;
	using namespace mo_yanxi::graphic::draw::instruction;

	buffer.clear();
	buffer.reserve_heads(glyph_layout.elems.size() + glyph_layout.underlines.size());
	buffer.reserve_bytes(glyph_layout.elems.size() * sizeof(rect_aabb) + glyph_layout.underlines.size() * sizeof(line));

	const auto& roundRegion = gui::assets::builtin::default_round_square_base;
	const bool hasRound = static_cast<bool>(roundRegion);
	for (const auto & range : glyph_layout.wrap_frames | std::views::chunk_by(&typesetting::sub_line_decoration::chunk_by_line)){
		auto [line_src, spacing] = glyph_layout.lines[range.front().line_index].calculate_alignment(glyph_layout.extent, line_align, glyph_layout.direction);

		for (const auto & val : range){
			const auto start = math::fma(spacing, static_cast<float>(val.start_gap_count), line_src + val.start);
			const auto end = math::fma(spacing, static_cast<float>(val.end_gap_count), line_src + val.end);

			switch(val.type){
			case typesetting::rich_text_token::wrap_frame_type::rect : buffer.push(rect_aabb_outline{
						.v00 = start,
						.v11 = end,
						.stroke = {2},
						.vert_color = {val.color}
					});
				break;
			case typesetting::rich_text_token::wrap_frame_type::round :
				if(hasRound){
					gui::fx::nine_patch_draw<&image_nine_region::get_row_coords_axis_scaled>{
						.patch = &roundRegion,
						.region = {start, end - start},
						.color = {val.color.copy_set_a(.6f).mul_rgb(.7f)}
					}.for_each([&](auto&& instr){
						buffer.push(instr);
					});
				}
				break;
			default : break;
			}
		}

	}

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
					.vert_color = {val.color * color_scl},
					.slant_factor_asc = val.slant_factor_asc,
					.slant_factor_desc = val.slant_factor_desc,
					.sdf_expand = -val.weight_offset
				});
		}
	}

	for (const auto & underlines : glyph_layout.underlines | std::views::chunk_by(&typesetting::sub_line_decoration::chunk_by_line)){
		auto [line_src, spacing] = glyph_layout.lines[underlines.front().line_index].calculate_alignment(glyph_layout.extent, line_align, glyph_layout.direction);

		for (const auto & val : underlines){
			const auto start = math::fma(spacing, static_cast<float>(val.start_gap_count), line_src + val.start);
			const auto end = math::fma(spacing, static_cast<float>(val.end_gap_count), line_src + val.end);

			buffer.push(line{
					.src = start,
					.dst = end,
					.color = {val.color * color_scl, val.color * color_scl},
					.stroke = val.thickness,
				});
		}
	}


	// buffer.push(rect_aabb_outline{
	// 		.v00 = {},
	// 		.v11 = glyph_layout.extent,
	// 		.stroke = {2},
	// 		.vert_color = graphic::colors::ACID
	// 	});

}
}
