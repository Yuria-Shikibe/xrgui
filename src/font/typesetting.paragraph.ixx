//
// Created by Matrix on 2026/3/18.
//

export module mo_yanxi.typesetting.paragraph;

import std;
export import mo_yanxi.typesetting;

/*
namespace mo_yanxi::typesetting{
export struct text_segment {
    std::uint32_t start_pos{0};
    std::uint32_t end_pos{0};
    bool is_dirty{true};

    // 局部的排版结果（内部坐标全部从 0,0 起算）
    glyph_layout local_layout{};

    // 组装后的全局偏移量（例如该段落左上角在整个文档中的坐标）
    math::vec2 global_offset{};

    [[nodiscard]] constexpr std::uint32_t length() const noexcept {
        return end_pos - start_pos;
    }
};

export class segmented_layout_manager {
private:
    std::vector<text_segment> segments_;
    const tokenized_text* source_text_{nullptr};
    math::vec2 total_extent_{};

public:
    void bind_text(const tokenized_text* text) {
        source_text_ = text;
        invalidate_all();
    }

    // 1. 根据换行符 \n 重新划分段落，并保留未修改段落的缓存
    void resplit_and_mark_dirty(std::uint32_t edit_start, std::uint32_t edit_length, std::int32_t length_diff) {
        // ... (此处需要实现区间求交逻辑)
        // 简单暴力且足够快的做法（应对绝大部分富文本场景）：
        // 扫描 chars_，找到所有 \n 的位置，与现有的 segments_ 进行对比。
        // 如果 start_pos 和 end_pos 对应的原字符串内容未变，则保留，否则标记 is_dirty = true。
    }

    // 全局强制脏标记（初始化时使用）
    void invalidate_all() {
        segments_.clear();
        if (!source_text_ || source_text_->empty()) return;

        const auto chars = source_text_->get_text();
        std::uint32_t current_start = 0;

        for (std::uint32_t i = 0; i < chars.size(); ++i) {
            if (chars[i] == U'\n') {
                segments_.push_back(text_segment{current_start, i + 1, true});
                current_start = i + 1;
            }
        }
        if (current_start < chars.size()) {
            segments_.push_back(text_segment{current_start, static_cast<std::uint32_t>(chars.size()), true});
        }
    }

    // 2. 核心更新逻辑：只排版脏段落
    void update_layouts(layout_context& ctx, font::font_face_view default_font_face = {}) {
        if (!source_text_) return;

        const auto full_chars = source_text_->get_text();

        for (auto& seg : segments_) {
            if (!seg.is_dirty) continue;

            // 构造局部视图
            auto local_chars = full_chars.substr(seg.start_pos, seg.length());
            auto token_range = source_text_->get_token_group(seg.start_pos, source_text_->get_init_token()); // 获取此区间的首个 token
            // 注意：这里需要根据 end_pos 截断 token_range

            tokenized_text_view view{local_chars, /* 计算出的 token_subrange #1#, seg.start_pos};

            // 独立排版（每次调用 ctx.layout 内部都会调用 ctx.initialize_state 清空上下文）
            ctx.layout(seg.local_layout, view, default_font_face);
            seg.is_dirty = false;
        }

        assemble_coordinates();
    }

private:
    // 3. 线性组装：计算每个段落的全局坐标
    void assemble_coordinates() {
        total_extent_ = {0.f, 0.f};
        math::vec2 current_offset{0.f, 0.f};

        for (auto& seg : segments_) {
            seg.global_offset = current_offset;

            // 假设从上往下排版 (TTB/LTR)，Y 轴累加。若包含多种排版方向，需读取 seg.local_layout.direction
            total_extent_.x = std::max(total_extent_.x, seg.local_layout.extent.x);
            current_offset.y += seg.local_layout.extent.y;
        }
        total_extent_.y = current_offset.y;
    }
};
}*/