export module mo_yanxi.typesetting.segmented_layout;

import std;
import mo_yanxi.typesetting;
import mo_yanxi.typesetting.rich_text;
import mo_yanxi.math.vector2;

namespace mo_yanxi::typesetting {
template<typename T, std::ranges::forward_range Rng>
void replace_range(std::vector<T>& vec,
				   typename std::vector<T>::iterator first,
				   typename std::vector<T>::iterator last,
				   Rng&& rng){
	auto it_old = first;
	auto it_new = std::ranges::begin(rng);
	auto end_new = std::ranges::end(rng);


	while (it_old != last && it_new != end_new) {
		*it_old = *it_new; // 如果新旧元素类型支持转移，这里也可以写 std::move(*it_new)
		++it_old;
		++it_new;
	}

	if (it_new == end_new) {
		// 情况 1: 新区间较短或长度恰好相等，删除旧区间多余的部分
		vec.erase(it_old, last);
	} else {
		vec.insert_range(last, std::ranges::subrange{it_new, end_new});
	}
}

export struct text_segment {
    std::uint32_t start_pos{};
    std::uint32_t end_pos{};
    bool is_dirty{true};

    // 局部的排版结果（内部坐标从 0,0 起算）
    glyph_layout local_layout{};

    // 组装后的全局偏移量（例如该段落左上角在整个文档中的坐标）
    math::vec2 global_offset{};

    [[nodiscard]] constexpr std::uint32_t length() const noexcept {
        return end_pos - start_pos;
    }

    [[nodiscard]] text_segment() noexcept = default;

    [[nodiscard]] text_segment(std::uint32_t start_pos, std::uint32_t end_pos) noexcept
	    : start_pos(start_pos),
	    end_pos(end_pos){
    }
};

export class segmented_layout_manager {
private:
    std::vector<text_segment> segments_;
    const tokenized_text* source_text_{nullptr};
    math::vec2 total_extent_{};

public:
    [[nodiscard]] segmented_layout_manager() = default;
    [[nodiscard]] explicit(false) segmented_layout_manager(const tokenized_text& text){
	    bind_text(text);
    }

    void bind_text(const tokenized_text& text) {
    	if(source_text_ == &text)return;
        source_text_ = &text;
        invalidate_all();
    }

	template <typename Pat>
	void split(Pat&& pat){
    	auto text = source_text_->get_text();
    	auto src = text.data();
    	segments_.clear();
    	for(auto subrange : text | std::views::split(std::forward<Pat>(pat))){
    		std::u32string_view substr{subrange};
    		auto beg = substr.data() - src;
    		auto end = beg + substr.size();
    		segments_.emplace_back(beg, end);
    	}
    }

    // 1. 增量更新段落：精准剔除受影响的段落并重新划分，保留安全段落的缓存
    void apply_edit(std::uint32_t edit_start, std::uint32_t old_len, std::uint32_t new_len) {
        if (!source_text_) return;
        if (segments_.empty()) {
            invalidate_all();
            return;
        }

        const std::uint32_t old_end = edit_start + old_len;
        const std::int32_t diff = static_cast<std::int32_t>(new_len) - static_cast<std::int32_t>(old_len);

        auto start_it = segments_.end();
        auto end_it = segments_.begin();

        // 寻找受影响的段落区间 [start_it, end_it)
        for (auto it = segments_.begin(); it != segments_.end(); ++it) {
            bool intersects = false;
            if (old_len == 0) {
                // 纯插入情况：判断插入点是否在段落内 [start_pos, end_pos)
                intersects = (it->start_pos <= edit_start && edit_start < it->end_pos);
                // 边缘情况：插入点在文本最末尾
                if (edit_start == it->end_pos && (it + 1) == segments_.end()) {
                    intersects = true;
                }
            } else {
                // 替换/删除情况：判断区间是否相交
                intersects = (std::max(it->start_pos, edit_start) < std::min(it->end_pos, old_end));
            }

            if (intersects) {
                if (start_it == segments_.end()) start_it = it;
                end_it = it + 1;
            }
        }

        // 如果没有找到相交段落（可能是超界等异常情况），触发全量重建
        if (start_it == segments_.end()) {
            invalidate_all();
            return;
        }

        std::uint32_t affected_old_start = start_it->start_pos;
        std::uint32_t affected_old_end = (end_it - 1)->end_pos;

        // 平移未受影响的后续段落的索引
        for (auto it = end_it; it != segments_.end(); ++it) {
            it->start_pos += diff;
            it->end_pos += diff;
        }

        std::uint32_t affected_new_end = affected_old_end + diff;
        std::vector<text_segment> new_sub_segments;
        const auto chars = source_text_->get_text();

        // 在新的受影响区间内，重新扫描换行符划分段落
        std::uint32_t current_start = affected_old_start;
        for (std::uint32_t i = affected_old_start; i < affected_new_end && i < chars.size(); ++i) {
            if (chars[i] == U'\n') {
                new_sub_segments.push_back(text_segment{current_start, i + 1});
                current_start = i + 1;
            }
        }
        if (current_start < affected_new_end) {
            new_sub_segments.push_back(text_segment{current_start, affected_new_end});
        }

    	replace_range(segments_, start_it, end_it, new_sub_segments | std::views::as_rvalue);
    }

    // 全量重建所有段落
    void invalidate_all(){
	    segments_.clear();
	    total_extent_ = {};
	    if(!source_text_ || source_text_->empty()) return;
	    split(std::u32string_view{U"\n\n"});
    }

    // 2. 核心更新逻辑：只排版脏段落
    void update_layouts(layout_context& ctx, const layout_config& config = {}) {
        if (!source_text_) return;

        bool layout_changed = false;

        for (auto& seg : segments_) {
            if (!seg.is_dirty) continue;

            tokenized_text_view view{*source_text_, seg.start_pos, seg.length()};
            ctx.layout(view, config, seg.local_layout);

            // 重要：HarfBuzz 产生的 cluster_index 是基于局部视图的 (从 0 开始)，
            // 我们必须将其补偿回全局坐标，以确保外层命中测试和游标逻辑的正确性。
            for (auto& cluster : seg.local_layout.clusters) {
                cluster.cluster_index += seg.start_pos;
            }

            seg.is_dirty = false;
            layout_changed = true;
        }

        if (layout_changed) {
            assemble_coordinates();
        }
    }

    [[nodiscard]] const std::vector<text_segment>& get_segments() const noexcept {
        return segments_;
    }

    [[nodiscard]] math::vec2 get_extent() const noexcept {
        return total_extent_;
    }

	template <typename S>
	auto begin(this S&& self) noexcept{
	    return std::forward_like<S>(self.segments_).begin();
    }

	template <typename S>
	auto end(this S&& self) noexcept{
	    return std::forward_like<S>(self.segments_).begin();
    }

    // 4. 全局命中测试代理
    [[nodiscard]] glyph_layout::hit_result hit_test(math::vec2 pos, line_alignment align) const noexcept {
        if (segments_.empty()) return {};

        const text_segment* target_seg = &segments_.front();
        bool is_vertical = false;

        // 根据排版方向寻找命中的段落
        for (const auto& seg : segments_) {
            const auto dir = seg.local_layout.direction;
            is_vertical = (dir == layout_direction::ttb || dir == layout_direction::btt);

            if (is_vertical) {
                // 垂直文本，段落沿 X 轴堆叠
                if (pos.x >= seg.global_offset.x && pos.x <= seg.global_offset.x + seg.local_layout.extent.x) {
                    target_seg = &seg;
                    break;
                }
            } else {
                // 水平文本，段落沿 Y 轴堆叠
                if (pos.y >= seg.global_offset.y && pos.y <= seg.global_offset.y + seg.local_layout.extent.y) {
                    target_seg = &seg;
                    break;
                }
            }
            // 如果没 break，target_seg 会保留最后一个段落（作为越界兜底）
            target_seg = &seg;
        }

        // 将全局坐标转换为目标段落的局部坐标进行精确碰撞
        math::vec2 local_pos = pos - target_seg->global_offset;
        return target_seg->local_layout.hit_test(local_pos, align);
    }

private:
    // 3. 线性组装：计算每个段落的全局坐标
    void assemble_coordinates() {
        total_extent_ = {0.f, 0.f};
        math::vec2 current_offset{0.f, 0.f};

        for (auto& seg : segments_) {
            seg.global_offset = current_offset;
            const auto& ext = seg.local_layout.extent;

            // 根据局部排版方向动态堆叠段落
            if (seg.local_layout.direction == layout_direction::ttb || seg.local_layout.direction == layout_direction::btt) {
                // 垂直排版：段落横向铺开
                total_extent_.y = std::max(total_extent_.y, ext.y);
                current_offset.x += ext.x;
            } else {
                // 水平排版：段落纵向铺开
                total_extent_.x = std::max(total_extent_.x, ext.x);
                current_offset.y += ext.y;
            }
        }

        // 结算最终高度或宽度
        if (!segments_.empty()) {
            const auto last_dir = segments_.back().local_layout.direction;
            if (last_dir == layout_direction::ttb || last_dir == layout_direction::btt) {
                total_extent_.x = current_offset.x;
            } else {
                total_extent_.y = current_offset.y;
            }
        }
    }
};

}