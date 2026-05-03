module mo_yanxi.gui.text_render;

import mo_yanxi.gui.image_regions;
import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.gui.fx.instruction_extension;


namespace mo_yanxi::gui{
void record_elems(graphic::draw::instruction::draw_record_storage<mr::unvs_allocator<std::byte>>& buffer,
                  const typesetting::glyph_layout_draw_only& glyph_layout,
                  typesetting::line_alignment line_align,
                  typesetting::layout_direction direction
){
	using namespace mo_yanxi::graphic::draw::instruction;

	record_elems(glyph_layout, line_align, direction, [&](rect_aabb&& r){
		buffer.push(r);
	});
}

void record_glyph_draw_instructions(
	graphic::draw::instruction::draw_record_storage<mr::unvs_allocator<std::byte>>& buffer,
	const typesetting::glyph_layout& glyph_layout, typesetting::line_alignment line_align){
	using namespace mo_yanxi::graphic;
	using namespace mo_yanxi::graphic::draw::instruction;

	buffer.clear();
	if(glyph_layout.lines.empty())return;
	buffer.reserve_heads(glyph_layout.elems.size() + glyph_layout.underlines.size() + glyph_layout.wrap_frames.size() * 3);
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

	record_elems(buffer, glyph_layout, line_align, glyph_layout.direction);

	for (const auto & underlines : glyph_layout.underlines | std::views::chunk_by(&typesetting::sub_line_decoration::chunk_by_line)){
		auto [line_src, spacing] = glyph_layout.lines[underlines.front().line_index].calculate_alignment(glyph_layout.extent, line_align, glyph_layout.direction);

		for (const auto & val : underlines){
			const auto start = math::fma(spacing, static_cast<float>(val.start_gap_count), line_src + val.start);
			const auto end = math::fma(spacing, static_cast<float>(val.end_gap_count), line_src + val.end);

			buffer.push(line{
					.src = start,
					.dst = end,
					.color = {val.color, val.color},
					.stroke = val.thickness,
				});
		}
	}
}

void record_glyph_draw_instructions_draw_only(
	graphic::draw::instruction::draw_record_storage<mr::unvs_allocator<std::byte>>& buffer,
	const typesetting::glyph_layout_draw_only& glyph_layout,
	typesetting::line_alignment line_align, typesetting::layout_direction direction){
	using namespace mo_yanxi::graphic;
	using namespace mo_yanxi::graphic::draw::instruction;

	buffer.clear();
	buffer.reserve_heads(glyph_layout.elems.size());
	buffer.reserve_bytes(glyph_layout.elems.size() * sizeof(rect_aabb));

	record_elems(buffer, glyph_layout, line_align, direction);
}
}
