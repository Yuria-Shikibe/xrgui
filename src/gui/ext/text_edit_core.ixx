module;

#include <cassert>

export module mo_yanxi.gui.text_edit_core;

import std;
import mo_yanxi.typesetting;
import mo_yanxi.history_stack;

namespace mo_yanxi::gui{
struct caret_identity{
	std::size_t src{0};
	std::size_t dst{0};

	[[nodiscard]] constexpr auto length() const noexcept{
		return src > dst ? src - dst : dst - src;
	}

	[[nodiscard]] constexpr bool has_region() const noexcept{
		return src != dst;
	}

	[[nodiscard]] constexpr caret_identity get_ordered() const noexcept{
		auto [min_val, max_val] = std::minmax(src, dst);
		return {min_val, max_val};
	}

	constexpr bool operator==(const caret_identity&) const noexcept = default;
};

enum class text_edit_type{
	insert, del, replace
};

struct text_edit_delta{
	text_edit_type type{};
	caret_identity caret_state{};
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

	void commit_insertion(const std::u32string_view content, const caret_identity caret, const std::size_t where,
		const std::u32string_view text){
		history_.push(text_edit_delta{
				.type = text_edit_type::insert,
				.caret_state = caret,
				.position = where,
				.text_after = std::u32string{text}
			});
	}

	void commit_delete(const std::u32string_view content, const caret_identity caret, const std::size_t where,
		const std::size_t length){
		history_.push(text_edit_delta{
				.type = text_edit_type::del,
				.caret_state = caret,
				.position = where,
				.text_before = std::u32string{content.substr(where, length)}
			});
	}

	void commit_replace(const std::u32string_view content, const caret_identity caret, const std::size_t where,
		const std::size_t length, const std::u32string_view inserted){
		history_.push(text_edit_delta{
				.type = text_edit_type::replace,
				.caret_state = caret,
				.position = where,
				.text_before = std::u32string{content.substr(where, length)},
				.text_after = std::u32string{inserted}
			});
	}

	bool undo(std::u32string& target, caret_identity& caret){
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

	bool redo(std::u32string& target, caret_identity& caret){
		if(auto* op = history_.to_next()){
			switch(op->type){
			case text_edit_type::insert : target.insert(op->position, op->text_after);
				caret = {op->caret_state.src + op->text_after.size(), op->caret_state.dst + op->text_after.size()};
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
};

export class text_editor_core{
private:
	std::u32string text_buffer_{};
	caret_identity caret_{};
	edit_history history_{64};

	// 幽灵坐标：记录垂直移动时期望保持的 X (或 Y) 轴坐标
	std::optional<float> preferred_cross_pos_{};

	static constexpr bool cares_about(const char32_t code) noexcept{
		if(code > 127) return true;
		return std::isalnum(code) || code == U'_';
	}

	void merge_caret(std::size_t index, bool select) noexcept{
		caret_.dst = std::clamp(index, std::size_t{0}, text_buffer_.size());
		if(!select){
			caret_.src = caret_.dst;
		}
	}

	// 重置幽灵坐标，任何导致水平位移或文本内容变化的操作都应调用此函数
	void reset_preferred_cross_pos() noexcept{
		preferred_cross_pos_ = std::nullopt;
	}

	struct line_bounds{
		std::size_t start_idx;
		std::size_t end_idx;
		std::size_t visual_end;
	};

	[[nodiscard]] line_bounds get_line_bounds(const typesetting::glyph_layout& layout, std::size_t line_idx) const {
        if (line_idx >= layout.lines.size()) {
            return {0, 0, 0};
        }

        const auto& line = layout.lines[line_idx];

        // 安全处理空行
        if (line.cluster_range.size == 0) {
            std::size_t idx = 0;
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

        std::size_t start = first_c.cluster_index;
        std::size_t end = last_c.cluster_index + last_c.cluster_span;
        std::size_t visual_end = end;

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

    [[nodiscard]] std::size_t get_line_index(const typesetting::glyph_layout& layout, std::size_t pos) const {
        if (layout.lines.empty()) return 0;
        for (std::size_t i = 0; i < layout.lines.size(); ++i) {
            auto b = get_line_bounds(layout, i);
            if (pos >= b.start_idx && pos < b.end_idx) return i;
            if (i == layout.lines.size() - 1 && pos == b.end_idx) return i;
        }
        // 兜底保护，绝对不返回超出 size() 的索引
        return layout.lines.size() > 0 ? layout.lines.size() - 1 : 0;
    }

public:
	text_editor_core() = default;

	[[nodiscard]] std::u32string_view get_text() const noexcept{ return text_buffer_; }
	[[nodiscard]] caret_identity get_caret() const noexcept{ return caret_; }

	void set_text(std::u32string text){
		text_buffer_ = std::move(text);
		caret_ = {0, 0};
		history_ = edit_history{64};
		reset_preferred_cross_pos();
	}

	// --- 编辑操作 ---

	bool insert_text(std::u32string_view inserted_text){
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

	bool delete_selection(){
		if(!caret_.has_region()) return false;
		reset_preferred_cross_pos();
		auto sorted = caret_.get_ordered();
		history_.commit_delete(text_buffer_, caret_, sorted.src, sorted.length());
		text_buffer_.erase(sorted.src, sorted.length());
		caret_ = {sorted.src, sorted.src};
		return true;
	}

	bool action_backspace(){
		if(delete_selection()) return true;
		if(caret_.dst == 0) return false;
		reset_preferred_cross_pos();

		history_.commit_delete(text_buffer_, caret_, caret_.dst - 1, 1);
		text_buffer_.erase(caret_.dst - 1, 1);
		caret_.dst -= 1;
		caret_.src = caret_.dst;
		return true;
	}

	bool action_delete(){
		if(delete_selection()) return true;
		if(caret_.dst >= text_buffer_.size()) return false;
		reset_preferred_cross_pos();

		history_.commit_delete(text_buffer_, caret_, caret_.dst, 1);
		text_buffer_.erase(caret_.dst, 1);
		return true;
	}

	bool undo(){
		reset_preferred_cross_pos();
		return history_.undo(text_buffer_, caret_);
	}

	bool redo(){
		reset_preferred_cross_pos();
		return history_.redo(text_buffer_, caret_);
	}

	// --- 水平及跳跃移动 ---

	void action_move_left(bool select){
		reset_preferred_cross_pos();
		if(!select && caret_.has_region()){
			merge_caret(caret_.get_ordered().src, false);
			return;
		}
		if(caret_.dst > 0) merge_caret(caret_.dst - 1, select);
	}

	void action_move_right(bool select){
		reset_preferred_cross_pos();
		if(!select && caret_.has_region()){
			merge_caret(caret_.get_ordered().dst, false);
			return;
		}
		if(caret_.dst < text_buffer_.size()) merge_caret(caret_.dst + 1, select);
	}

	void action_jump_left(const typesetting::glyph_layout& layout, bool select){
		reset_preferred_cross_pos();
		if(!select && caret_.has_region()){
			merge_caret(caret_.get_ordered().src, false);
			return;
		}
		if(caret_.dst == 0) return;

		std::size_t start_line = get_line_index(layout, caret_.dst);
		auto bounds = get_line_bounds(layout, start_line);

		std::size_t p = caret_.dst - 1;
		while(p > 0 && !cares_about(text_buffer_[p])) p--;
		while(p > 0 && cares_about(text_buffer_[p - 1])) p--;

		if(p > bounds.visual_end){
			if(caret_.dst < bounds.visual_end){
				p = bounds.visual_end;
			} else{
				std::size_t total_logical_lines = layout.lines.size() + (
					(!text_buffer_.empty() && text_buffer_.back() == U'\n') ? 1 : 0);
				if(start_line + 1 < total_logical_lines){
					auto next_bounds = get_line_bounds(layout, start_line + 1);
					if(p > next_bounds.visual_end) p = next_bounds.visual_end;
				}
			}
		}
		merge_caret(p, select);
	}

	void action_jump_right(const typesetting::glyph_layout& layout, bool select){
		reset_preferred_cross_pos();
		if(!select && caret_.has_region()){
			merge_caret(caret_.get_ordered().dst, false);
			return;
		}
		if(caret_.dst >= text_buffer_.size()) return;

		std::size_t start_line = get_line_index(layout, caret_.dst);
		auto bounds = get_line_bounds(layout, start_line);

		std::size_t p = caret_.dst;
		while(p < text_buffer_.size() && cares_about(text_buffer_[p])) p++;
		while(p < text_buffer_.size() && !cares_about(text_buffer_[p])) p++;

		if(p > bounds.visual_end){
			if(caret_.dst < bounds.visual_end){
				p = bounds.visual_end;
			} else if(start_line + 1 < layout.lines.size()){
				auto next_bounds = get_line_bounds(layout, start_line + 1);
				if(p > next_bounds.visual_end) p = next_bounds.visual_end;
			}
		}
		merge_caret(p, select);
	}

	void action_move_line_begin(const typesetting::glyph_layout& layout, bool select){
		reset_preferred_cross_pos();
		if(layout.empty()){
			merge_caret(0, select);
			return;
		}
		std::size_t line_idx = get_line_index(layout, caret_.dst);
		auto bounds = get_line_bounds(layout, line_idx);
		merge_caret(bounds.start_idx, select);
	}

	void action_move_line_end(const typesetting::glyph_layout& layout, bool select){
		reset_preferred_cross_pos();
		if(layout.empty()){
			merge_caret(text_buffer_.size(), select);
			return;
		}
		std::size_t line_idx = get_line_index(layout, caret_.dst);
		auto bounds = get_line_bounds(layout, line_idx);
		merge_caret(bounds.visual_end, select);
	}

	// --- 鼠标点击命中 ---
	void action_select_all() noexcept {
		reset_preferred_cross_pos();
		caret_.src = 0;
		caret_.dst = text_buffer_.size();
	}

	bool action_hit_test(const typesetting::glyph_layout& layout, math::vec2 pos, typesetting::content_alignment align,
		bool select){
		reset_preferred_cross_pos();
		if(layout.empty()){
			merge_caret(0, select);
			return false;
		}
		auto hit = layout.hit_test(pos, align);
		if(hit){
			std::size_t new_index = hit.source->cluster_index;
			if(hit.is_trailing){
				new_index += hit.source->cluster_span;
			}
			merge_caret(new_index, select);
		}
		return bool(hit);
	}

	// --- 修正后的垂直移动逻辑 ---

	void move_vertical(const typesetting::glyph_layout& layout, bool move_down, typesetting::content_alignment align, bool select) {
        if (layout.empty() || layout.lines.empty()) return;
        std::size_t line_idx = get_line_index(layout, caret_.dst);

        // 防御性编程：强制钳制 line_idx，确保上一层的任何逻辑修改都不会导致这里越界
        if (line_idx >= layout.lines.size()) {
            line_idx = layout.lines.size() - 1;
        }

        const bool is_vertical_layout = (layout.direction == typesetting::layout_direction::ttb || layout.direction == typesetting::layout_direction::btt);

        // 1. 如果没有记忆坐标，精准计算当前光标在交叉轴上的位置
        if (!preferred_cross_pos_) {
            const auto& current_line = layout.lines[line_idx];
            const typesetting::logical_cluster* current_cluster = nullptr;

            for (std::size_t j = 0; j < current_line.cluster_range.size; ++j) {
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
                // 当处于空行时，不再简单粗暴地赋值 0.0f
                // 而是从当前空行的排版对齐逻辑中，提取它的理论行首坐标作为交叉轴起点
                auto align_res = current_line.calculate_alignment(layout.extent, align, layout.direction);
                preferred_cross_pos_ = is_vertical_layout ? align_res.start_pos.y : align_res.start_pos.x;
            }
        }

        // 2. 确定目标行
        if (move_down && line_idx + 1 >= layout.lines.size()) {
            merge_caret(text_buffer_.size(), select);
            return;
        }
        if (!move_down && line_idx == 0) {
            merge_caret(0, select);
            return;
        }

        std::size_t target_line_idx = move_down ? line_idx + 1 : line_idx - 1;

        // 再次安全断言
        if(target_line_idx >= layout.lines.size()) {
            merge_caret(text_buffer_.size(), select);
            return;
        }

        const auto& target_line = layout.lines[target_line_idx];

        // 3. 构建目标点：利用保留的 preferred_cross_pos_ 作为交叉轴，结合目标行的主轴中心构建精确靶心
        math::vec2 target_pos{};
        auto target_align_offset = target_line.calculate_alignment(layout.extent, align, layout.direction);

        if (is_vertical_layout) {
            target_pos.y = *preferred_cross_pos_;
            target_pos.x = target_align_offset.start_pos.x + (target_line.rect.descender - target_line.rect.ascender) / 2.0f;
        } else {
            target_pos.x = *preferred_cross_pos_;
            target_pos.y = target_align_offset.start_pos.y + (target_line.rect.descender - target_line.rect.ascender) / 2.0f;
        }

        // 4. Hit Test，并将索引严格钳制（Clamp）在目标视觉行的边界内，拦截溢出 Bug
        auto hit = layout.hit_test(target_pos, align);

        if (hit) {
            std::size_t new_index = hit.source->cluster_index;
            if (hit.is_trailing) {
                new_index += hit.source->cluster_span;
            }
            auto bounds = get_line_bounds(layout, target_line_idx);
            new_index = std::clamp(new_index, bounds.start_idx, bounds.visual_end);
            merge_caret(new_index, select);
        } else {
            auto bounds = get_line_bounds(layout, target_line_idx);
            merge_caret(bounds.visual_end, select);
        }
    }

	void action_move_up(const typesetting::glyph_layout& layout, typesetting::content_alignment align, bool select){
		move_vertical(layout, false, align, select);
	}

	void action_move_down(const typesetting::glyph_layout& layout, typesetting::content_alignment align, bool select){
		move_vertical(layout, true, align, select);
	}
};
} // namespace mo_yanxi::gui
