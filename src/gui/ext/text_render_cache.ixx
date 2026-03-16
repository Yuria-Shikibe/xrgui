//
// Created by Matrix on 2026/3/15.
//

export module mo_yanxi.gui.text_render_cache;

import std;

export import mo_yanxi.typesetting;

import mo_yanxi.graphic.draw.instruction.recorder;
import mo_yanxi.graphic.color;
import mo_yanxi.gui.renderer.frontend;
import mo_yanxi.gui.alloc;
import mo_yanxi.gui.util;
import align;

namespace mo_yanxi::gui {

void record_glyph_draw_instructions(
	graphic::draw::instruction::draw_record_storage<mr::heap_allocator<std::byte>>& buffer,
	const typesetting::glyph_layout& glyph_layout,
	graphic::color color_scl, typesetting::line_alignment line_align
);

void push(gui::renderer_frontend& r, const graphic::draw::instruction::draw_record_storage<mr::heap_allocator<>>& buf){
	r.push(buf.heads(), buf.data());
}

export struct text_render_cache {
private:
	graphic::draw::instruction::draw_record_storage<mr::heap_allocator<>> draw_instr_buffer_{
		mr::get_default_heap_allocator()
	};
	std::optional<graphic::color> text_color_scl_{};
	typesetting::line_alignment line_align_{};


public:

	void set_allocator(const mr::heap_allocator<std::byte>& alloc) {
		draw_instr_buffer_ = {alloc};
	}

	[[nodiscard]] bool has_drawable_text() const noexcept {
		return !draw_instr_buffer_.heads().empty();
	}

	[[nodiscard]] std::optional<graphic::color> get_text_color_scl() const noexcept {
		return text_color_scl_;
	}

	bool set_text_color_scl(const std::optional<graphic::color>& color) {
		if (text_color_scl_ != color) {
			text_color_scl_ = color;
			return true;
		}
		return false;
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

	[[nodiscard]] graphic::color get_draw_color(float opacity, bool is_disabled) const noexcept {
		auto color = text_color_scl_.value_or(graphic::colors::white);
		color.mul_a(opacity);
		if (is_disabled) {
			color.mul_a(0.5f);
		}
		return color;
	}

	// 核心的指令录制，原 update_draw_buffer
	void update_buffer(const typesetting::glyph_layout& layout, graphic::color color) {
		mo_yanxi::gui::record_glyph_draw_instructions(draw_instr_buffer_, layout, color, line_align_);
	}

	void push_to_renderer(gui::renderer_frontend& r) const {
		mo_yanxi::gui::push(r, draw_instr_buffer_);
	}
};

} // namespace mo_yanxi::gui
