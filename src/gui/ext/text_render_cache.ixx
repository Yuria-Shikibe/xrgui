//
// Created by Matrix on 2026/3/15.
//

export module mo_yanxi.gui.text_render;

import std;

export import mo_yanxi.typesetting;

import mo_yanxi.graphic.draw.instruction.recorder;
import mo_yanxi.graphic.color;
import mo_yanxi.gui.renderer.frontend;
import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.gui.alloc;
import mo_yanxi.gui.util;
import align;

namespace mo_yanxi::gui {
export
template <std::invocable<graphic::draw::instruction::rect_aabb&&> Fn>
void record_elems(
	const typesetting::glyph_layout_draw_only& glyph_layout,
	typesetting::line_alignment line_align, typesetting::layout_direction direction,
	Fn&& fn
){
	using namespace mo_yanxi::graphic::draw::instruction;

	for(const auto& current_line : glyph_layout.lines){
		auto [line_src, spacing] = current_line.calculate_alignment(glyph_layout.extent, line_align,
		                                                            direction);

		for(const auto& [idx, val] : std::span{
			    glyph_layout.elems.begin() + current_line.glyph_range.pos, current_line.glyph_range.size
		    } | std::views::enumerate){
			if(!val.texture->view) continue;
			auto start = math::fma(idx, spacing, line_src + val.aabb.src);
			std::invoke(
				std::forward<Fn>(fn),
				rect_aabb{
					.generic = {val.texture->view},
					.v00 = start,
					.v11 = start + val.aabb.extent(),
					.uv00 = val.texture->uv.v00(),
					.uv11 = val.texture->uv.v11(),
					.vert_color = {val.color},
					.slant_factor_asc = val.slant_factor_asc,
					.slant_factor_desc = val.slant_factor_desc,
					.sdf_expand = -val.weight_offset
				});
		}
	}
}

void record_glyph_draw_instructions(
	graphic::draw::instruction::draw_record_storage<mr::unvs_allocator<std::byte>>& buffer,
	const typesetting::glyph_layout& glyph_layout,
	typesetting::line_alignment line_align
);

void record_glyph_draw_instructions_draw_only(
	graphic::draw::instruction::draw_record_storage<mr::unvs_allocator<std::byte>>& buffer,
	const typesetting::glyph_layout_draw_only& glyph_layout,
	typesetting::line_alignment line_align, typesetting::layout_direction direction
);

template <typename Alloc>
void push(renderer_frontend& r, const graphic::draw::instruction::draw_record_storage<Alloc>& buf){
	graphic::draw::emit(r, buf);
}

export struct text_render_cache {
private:
	graphic::draw::instruction::draw_record_storage<mr::unvs_allocator<std::byte>> draw_instr_buffer_{};
	typesetting::line_alignment line_align_{};

public:

	[[nodiscard]] bool has_drawable_text() const noexcept {
		return !draw_instr_buffer_.heads().empty();
	}

	[[nodiscard]] typesetting::line_alignment get_line_align() const noexcept {
		return line_align_;
	}

	bool set_line_align(const typesetting::line_alignment line_align){
		if(util::try_modify(line_align_, line_align)){
			return true;
		}
		return false;
	}

	// 核心的指令录制，原 update_draw_buffer
	void update_buffer(const typesetting::glyph_layout& layout) {
		record_glyph_draw_instructions(draw_instr_buffer_, layout, line_align_);
	}

	void update_buffer(const typesetting::glyph_layout_draw_only& layout, typesetting::layout_direction direction = typesetting::layout_direction::ltr) {
		record_glyph_draw_instructions_draw_only(draw_instr_buffer_, layout, line_align_, direction);
	}

	void operator()(graphic::draw::emit_t emit, auto& sink) const {
		emit(sink, draw_instr_buffer_);
	}

	void push_to_renderer(renderer_frontend& r) const {
		graphic::draw::emit(r, *this);
	}
};

} // namespace mo_yanxi::gui
