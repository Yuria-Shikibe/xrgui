module;

#include <cassert>

export module mo_yanxi.gui.text_edit_core;

import std;
import mo_yanxi.typesetting;
import mo_yanxi.history_stack;
import mo_yanxi.math.vector2;

namespace mo_yanxi::gui{


export
struct caret_section{
	unsigned src;
	unsigned dst;

	[[nodiscard]] constexpr auto length() const noexcept{
		return src > dst ? src - dst : dst - src;
	}

	[[nodiscard]] constexpr bool has_region() const noexcept{
		return src != dst;
	}

	[[nodiscard]] constexpr caret_section get_ordered() const noexcept{
		auto [min_val, max_val] = std::minmax(src, dst);
		return {min_val, max_val};
	}

	constexpr bool operator==(const caret_section&) const noexcept = default;
};

enum class char_class { whitespace, word, punctuation, other };

enum class text_edit_type{
	insert, del, replace
};

struct text_edit_delta{
	text_edit_type type{};
	caret_section caret_state{};
	std::size_t position{};
	std::u32string text_before{};
	std::u32string text_after{};
};

class edit_history{
private:
	mo_yanxi::procedure_history_stack<text_edit_delta, std::deque<text_edit_delta>> history_{};

public:
	edit_history() = default;

	explicit edit_history(const std::size_t capacity) : history_(capacity){
	}

	void commit_insertion(const std::u32string_view content, const caret_section caret, const std::size_t where,
		const std::u32string_view text){
		history_.push(text_edit_delta{
				.type = text_edit_type::insert,
				.caret_state = caret,
				.position = where,
				.text_after = std::u32string{text}
			});
	}

	void commit_delete(const std::u32string_view content, const caret_section caret, const std::size_t where,
		const std::size_t length){
		history_.push(text_edit_delta{
				.type = text_edit_type::del,
				.caret_state = caret,
				.position = where,
				.text_before = std::u32string{content.substr(where, length)}
			});
	}

	void commit_replace(const std::u32string_view content, const caret_section caret, const std::size_t where,
		const std::size_t length, const std::u32string_view inserted){
		history_.push(text_edit_delta{
				.type = text_edit_type::replace,
				.caret_state = caret,
				.position = where,
				.text_before = std::u32string{content.substr(where, length)},
				.text_after = std::u32string{inserted}
			});
	}

	bool undo(std::u32string& target, caret_section& caret){
		if(auto* op = history_.to_prev()){
			switch(op->type){
			case text_edit_type::insert : target.erase(op->position, op->text_after.size());
				break;
			case text_edit_type::del : target.insert(op->position, op->text_before);
				break;
			case text_edit_type::replace : target.replace(op->position, op->text_after.size(), op->text_before);
				break;
			}
			caret = op->caret_state;
			return true;
		}
		return false;
	}

	bool redo(std::u32string& target, caret_section& caret){
		if(auto* op = history_.to_next()){
			switch(op->type){
			case text_edit_type::insert : target.insert(op->position, op->text_after);
				caret = caret_section(op->caret_state.src + op->text_after.size(), op->caret_state.dst + op->text_after.size());
				break;
			case text_edit_type::del : target.erase(op->position, op->text_before.size());
				caret = op->caret_state;
				break;
			case text_edit_type::replace : target.replace(op->position, op->text_before.size(), op->text_after);
				caret = op->caret_state;
				break;
			}
			return true;
		}
		return false;
	}

	void clear_redo() noexcept {
		history_.truncate();
	}
};

export class text_editor_core{
private:
	caret_section caret_{};
	edit_history history_{64};

	std::optional<float> preferred_cross_pos_{};

	static constexpr bool cares_about(const char32_t code) noexcept{
		if(code > 127) return true;
		return std::isalnum(code) || code == U'_';
	}

	void merge_caret(unsigned index, bool select, unsigned max_size) noexcept{
		caret_.dst = std::clamp(index, unsigned{0}, max_size);
		if(!select){
			caret_.src = caret_.dst;
		}
	}

	void reset_preferred_cross_pos() noexcept{
		preferred_cross_pos_ = std::nullopt;
	}

	struct line_bounds{
		unsigned start_idx;
		unsigned end_idx;
		unsigned visual_end;
	};

	[[nodiscard]] line_bounds get_line_bounds(const typesetting::glyph_layout& layout, const std::u32string_view text_buffer_, unsigned line_idx) const {
        if (line_idx >= layout.lines.size()) {
            return {0, 0, 0};
        }

        const auto& line = layout.lines[line_idx];

        if (line.cluster_range.size == 0) {
            unsigned idx = 0;
            if (line.cluster_range.pos > 0 && line.cluster_range.pos <= layout.clusters.size()) {
                const auto& prev = layout.clusters[line.cluster_range.pos - 1];
                idx = prev.cluster_index + prev.cluster_span;
            } else if (!text_buffer_.empty()) {
                idx = text_buffer_.size();
            }
            return {idx, idx, idx};
        }

        const auto& first_c = layout.clusters[line.cluster_range.pos];
        const auto& last_c = layout.clusters[line.cluster_range.pos + line.cluster_range.size - 1];

        unsigned start = first_c.cluster_index;
        unsigned end = last_c.cluster_index + last_c.cluster_span;
        unsigned visual_end = end;

        if (visual_end > start && visual_end <= text_buffer_.size()) {
            char32_t last_char = text_buffer_[visual_end - 1];
            if (last_char == U'\n' || last_char == U'\r') {
                visual_end--;
                if (visual_end > start && text_buffer_[visual_end - 1] == U'\r') {
                    visual_end--;
                }
            }
        }
        return {start, end, visual_end};
    }

    [[nodiscard]] unsigned get_line_index(const typesetting::glyph_layout& layout, const std::u32string_view text_buffer_, unsigned pos) const {
        if (layout.lines.empty()) return 0;

        for (unsigned i = 0; i < layout.lines.size(); ++i) {
            auto b = get_line_bounds(layout, text_buffer_, i);
            if (pos >= b.start_idx && pos < b.end_idx) return i;
            if (i == layout.lines.size() - 1 && pos == b.end_idx) return i;
        }
        return layout.lines.size() > 0 ? layout.lines.size() - 1 : 0;
    }

public:
	text_editor_core() = default;

	[[nodiscard]] caret_section get_caret() const noexcept{ return caret_; }

	void reset_state(){
		caret_ = {0, 0};
		history_ = edit_history{64};
		reset_preferred_cross_pos();
	}

	// --- 编辑操作 ---

	bool insert_text(std::u32string& text_buffer_, std::u32string_view inserted_text){
		if(inserted_text.empty()) return false;
		reset_preferred_cross_pos();

		if(caret_.has_region()){
			auto sorted = caret_.get_ordered();
			history_.commit_replace(text_buffer_, caret_, sorted.src, sorted.length(), inserted_text);
			text_buffer_.replace(sorted.src, sorted.length(), inserted_text);
			caret_.dst = sorted.src + inserted_text.size();
		} else{
			history_.commit_insertion(text_buffer_, caret_, caret_.dst, inserted_text);
			text_buffer_.insert(caret_.dst, inserted_text);
			caret_.dst += inserted_text.size();
		}
		caret_.src = caret_.dst;
		return true;
	}

	bool delete_selection(std::u32string& text_buffer_){
		if(!caret_.has_region()) return false;
		reset_preferred_cross_pos();
		auto sorted = caret_.get_ordered();
		history_.commit_delete(text_buffer_, caret_, sorted.src, sorted.length());
		text_buffer_.erase(sorted.src, sorted.length());
		caret_ = {sorted.src, sorted.src};
		return true;
	}

	bool action_backspace(std::u32string& text_buffer_){
		if(delete_selection(text_buffer_)) return true;
		if(caret_.dst == 0) return false;
		reset_preferred_cross_pos();

		history_.commit_delete(text_buffer_, caret_, caret_.dst - 1, 1);
		text_buffer_.erase(caret_.dst - 1, 1);
		caret_.dst -= 1;
		caret_.src = caret_.dst;
		return true;
	}

	bool action_delete(std::u32string& text_buffer_){
		if(delete_selection(text_buffer_)) return true;
		if(caret_.dst >= text_buffer_.size()) return false;
		reset_preferred_cross_pos();

		history_.commit_delete(text_buffer_, caret_, caret_.dst, 1);
		text_buffer_.erase(caret_.dst, 1);
		return true;
	}

	bool undo(std::u32string& text_buffer_){
		reset_preferred_cross_pos();
		return history_.undo(text_buffer_, caret_);
	}

	bool redo(std::u32string& text_buffer_){
		reset_preferred_cross_pos();
		return history_.redo(text_buffer_, caret_);
	}

	void clear_redo() noexcept {
		history_.clear_redo();
	}
	// --- 水平及跳跃移动 ---

	void action_move_left(const std::u32string_view text_buffer_, bool select){
		reset_preferred_cross_pos();
		if(!select && caret_.has_region()){
			merge_caret(caret_.get_ordered().src, false, text_buffer_.size());
			return;
		}
		if(caret_.dst > 0) merge_caret(caret_.dst - 1, select, text_buffer_.size());
	}

	void action_move_right(const std::u32string_view text_buffer_, bool select){
		reset_preferred_cross_pos();
		if(!select && caret_.has_region()){
			merge_caret(caret_.get_ordered().dst, false, text_buffer_.size());
			return;
		}
		if(caret_.dst < text_buffer_.size()) merge_caret(caret_.dst + 1, select, text_buffer_.size());
	}

	static constexpr char_class get_char_class(const char32_t code) noexcept {
		if (code == U' ' || code == U'\t' || code == U'\n' || code == U'\r') return char_class::whitespace;
		if (code < 128) {
			if (std::isalnum(code) || code == U'_') return char_class::word;
			return char_class::punctuation;
		}
		// 对于 CJK 等非 ASCII 字符，将其归为 other
		// 这样中文字符会连续跳过，如果需要更细粒度的中文分词跳跃，需要接入 ICU 等分词库
		return char_class::other;
	}

	void action_jump_left(const typesetting::glyph_layout& layout, const std::u32string_view text_buffer_, bool select) {
		reset_preferred_cross_pos();
		if (!select && caret_.has_region()) {
			merge_caret(caret_.get_ordered().src, false, text_buffer_.size());
			return;
		}
		if (caret_.dst == 0) return;

		unsigned p = caret_.dst;

		// 1. 往左跳过所有尾随的空白字符
		while (p > 0 && get_char_class(text_buffer_[p - 1]) == char_class::whitespace) {
			p--;
		}

		// 2. 如果前面还有字符，记录它的类别，并一直向左跳过所有同类别字符
		if (p > 0) {
			char_class target_class = get_char_class(text_buffer_[p - 1]);
			while (p > 0 && get_char_class(text_buffer_[p - 1]) == target_class) {
				p--;
			}
		}

		merge_caret(p, select, text_buffer_.size());
	}

	void action_jump_right(const typesetting::glyph_layout& layout, const std::u32string_view text_buffer_, bool select) {
		reset_preferred_cross_pos();
		if (!select && caret_.has_region()) {
			merge_caret(caret_.get_ordered().dst, false, text_buffer_.size());
			return;
		}
		if (caret_.dst >= text_buffer_.size()) return;

		unsigned p = caret_.dst;

		// 1. 记录当前位置的字符类别，并向右跳过所有同类别字符
		// （如果当前就是空白符，这里就会直接跳过空白块）
		char_class target_class = get_char_class(text_buffer_[p]);
		while (p < text_buffer_.size() && get_char_class(text_buffer_[p]) == target_class) {
			p++;
		}

		// 2. 如果刚刚跳过的不是空白符，为了符合日常认知，
		// 我们接着向右跳过紧跟的空白字符，让光标停在“下一个词的开头”
		if (target_class != char_class::whitespace) {
			while (p < text_buffer_.size() && get_char_class(text_buffer_[p]) == char_class::whitespace) {
				p++;
			}
		}

		merge_caret(p, select, text_buffer_.size());
	}

	void action_move_line_begin(const typesetting::glyph_layout& layout, const std::u32string_view text_buffer_, bool select){
		reset_preferred_cross_pos();
		if(layout.empty()){
			merge_caret(0, select, text_buffer_.size());
			return;
		}
		unsigned line_idx = get_line_index(layout, text_buffer_, caret_.dst);
		auto bounds = get_line_bounds(layout, text_buffer_, line_idx);
		merge_caret(bounds.start_idx, select, text_buffer_.size());
	}

	void action_move_line_end(const typesetting::glyph_layout& layout, const std::u32string_view text_buffer_, bool select){
		reset_preferred_cross_pos();
		if(layout.empty()){
			merge_caret(text_buffer_.size(), select, text_buffer_.size());
			return;
		}
		unsigned line_idx = get_line_index(layout, text_buffer_, caret_.dst);
		auto bounds = get_line_bounds(layout, text_buffer_, line_idx);
		merge_caret(bounds.visual_end, select, text_buffer_.size());
	}

	// --- 鼠标点击命中 ---
	void action_select_all(const std::u32string_view text_buffer_) noexcept {
		reset_preferred_cross_pos();
		caret_.src = 0;
		caret_.dst = text_buffer_.size();
	}

	bool action_hit_test(const typesetting::glyph_layout& layout, const std::u32string_view text_buffer_, math::vec2 pos, typesetting::line_alignment align,
		bool select){
		reset_preferred_cross_pos();
		if(layout.empty()){
			merge_caret(0, select, text_buffer_.size());
			return false;
		}

		auto hit = layout.hit_test(pos, align);
		// 增加 hit.source 的判空，以防底层引擎对于空行返回了 true 但携带空指针
		if(hit && hit.source){
			unsigned new_index = hit.source->cluster_index + hit.span_offset;
			unsigned line_idx = static_cast<unsigned>(hit.source_line - layout.lines.data());
			auto bounds = get_line_bounds(layout, text_buffer_, line_idx);
			new_index = std::clamp(new_index, bounds.start_idx, bounds.visual_end);

			merge_caret(new_index, select, text_buffer_.size());
			return true;
		}

		// Fallback: 用户点击在了没有任何字符的区域（例如幽灵行或文本末尾的纯白区）
		// 我们需要计算距离鼠标 Y 轴（如果是横排版）最近的行，并将光标吸附过去
		const bool is_vertical_layout = (layout.direction == typesetting::layout_direction::ttb || layout.direction == typesetting::layout_direction::btt);
		unsigned closest_line_idx = 0;
		float min_dist = std::numeric_limits<float>::max();

		for (unsigned i = 0; i < layout.lines.size(); ++i) {
			auto align_res = layout.lines[i].calculate_alignment(layout.extent, align, layout.direction);
			float dist = is_vertical_layout ? std::abs(align_res.start_pos.x - pos.x) : std::abs(align_res.start_pos.y - pos.y);
			if (dist < min_dist) {
				min_dist = dist;
				closest_line_idx = i;
			}
		}

		// 找到最近的行后，将光标对齐到该行的行尾（由于空行的 start_idx 等于 visual_end，这会自然对齐到行首）
		auto bounds = get_line_bounds(layout, text_buffer_, closest_line_idx);
		merge_caret(bounds.visual_end, select, text_buffer_.size());

		// 即使是 fallback 命中，我们也返回 true，以确保 UI 系统能正确保持光标存活
		return true;
	}

	// --- 修正后的垂直移动逻辑 ---

	void move_vertical(const typesetting::glyph_layout& layout, const std::u32string_view text_buffer_, bool move_down, typesetting::line_alignment align, bool select) {
        if (layout.empty() || layout.lines.empty()) return;
        unsigned line_idx = get_line_index(layout, text_buffer_, caret_.dst);

        if (line_idx >= layout.lines.size()) {
            line_idx = layout.lines.size() - 1;
        }

        const bool is_vertical_layout = (layout.direction == typesetting::layout_direction::ttb || layout.direction == typesetting::layout_direction::btt);

        if (!preferred_cross_pos_) {
            const auto& current_line = layout.lines[line_idx];
            const typesetting::logical_cluster* current_cluster = nullptr;

            for (unsigned j = 0; j < current_line.cluster_range.size; ++j) {
                const auto& c = layout.clusters[current_line.cluster_range.pos + j];
                if (caret_.dst >= c.cluster_index && caret_.dst < c.cluster_index + c.cluster_span) {
                    current_cluster = &c;
                    break;
                }
            }
            if (!current_cluster && current_line.cluster_range.size > 0) {
                current_cluster = &layout.clusters[current_line.cluster_range.pos + current_line.cluster_range.size - 1];
            }

            if (current_cluster) {
                if (is_vertical_layout) {
                    preferred_cross_pos_ = (caret_.dst > current_cluster->cluster_index) ?
                        current_cluster->logical_rect.vert_11().y : current_cluster->logical_rect.vert_00().y;
                } else {
                    preferred_cross_pos_ = (caret_.dst > current_cluster->cluster_index) ?
                        current_cluster->logical_rect.vert_11().x : current_cluster->logical_rect.vert_00().x;
                }
            } else {
                auto align_res = current_line.calculate_alignment(layout.extent, align, layout.direction);
                preferred_cross_pos_ = is_vertical_layout ? align_res.start_pos.y : align_res.start_pos.x;
            }
        }

        if (move_down && line_idx + 1 >= layout.lines.size()) {
            merge_caret(text_buffer_.size(), select, text_buffer_.size());
            return;
        }
        if (!move_down && line_idx == 0) {
            merge_caret(0, select, text_buffer_.size());
            return;
        }

        unsigned target_line_idx = move_down ? line_idx + 1 : line_idx - 1;

        if(target_line_idx >= layout.lines.size()) {
            merge_caret(text_buffer_.size(), select, text_buffer_.size());
            return;
        }

        const auto& target_line = layout.lines[target_line_idx];
        math::vec2 target_pos{};
        auto target_align_offset = target_line.calculate_alignment(layout.extent, align, layout.direction);

        if (is_vertical_layout) {
            target_pos.y = *preferred_cross_pos_;
            target_pos.x = target_align_offset.start_pos.x + (target_line.rect.descender - target_line.rect.ascender) / 2.0f;
        } else {
            target_pos.x = *preferred_cross_pos_;
            target_pos.y = target_align_offset.start_pos.y + (target_line.rect.descender - target_line.rect.ascender) / 2.0f;
        }

		auto hit = layout.hit_test(target_pos, align);
        if (hit) {
			unsigned new_index = hit.source->cluster_index + hit.span_offset;
			auto bounds = get_line_bounds(layout, text_buffer_, target_line_idx);
			new_index = std::clamp(new_index, bounds.start_idx, bounds.visual_end);
			merge_caret(new_index, select, text_buffer_.size());
		} else {
			auto bounds = get_line_bounds(layout, text_buffer_, target_line_idx);
			merge_caret(bounds.visual_end, select, text_buffer_.size());
		}
    }

	void action_move_up(const typesetting::glyph_layout& layout, const std::u32string_view text_buffer_, typesetting::line_alignment align, bool select){
		move_vertical(layout, text_buffer_, false, align, select);
	}

	void action_move_down(const typesetting::glyph_layout& layout, const std::u32string_view text_buffer_, typesetting::line_alignment align, bool select){
		move_vertical(layout, text_buffer_, true, align, select);
	}
};
} // namespace mo_yanxi::gui