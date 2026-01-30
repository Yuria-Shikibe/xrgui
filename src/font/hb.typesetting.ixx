module;

#include <hb.h>
#include <hb-ft.h>
#include <freetype/freetype.h>
#include <vulkan/vulkan.h>

export module mo_yanxi.hb.typesetting;

import mo_yanxi.math;
import mo_yanxi.font;
import mo_yanxi.font.manager;
import mo_yanxi.hb.wrap;
import mo_yanxi.graphic.image_region;

import mo_yanxi.encode;

import std;

namespace mo_yanxi::font::hb {

export enum struct layout_direction {
    deduced,
    ltr,
    rtl,
    ttb,
    btt
};

export enum struct linefeed {
    LF,   // ignores \r
    CRLF, // enable \r
};

export struct layout_result {
    math::frect aabb;
    glyph_borrow texture;
};

export struct glyph_layout {
    std::vector<layout_result> elems;
    math::vec2 extent;
};

// 辅助：简单的 UTF-8 解码，用于查找字符归属的字体
// constexpr std::pair<char32_t, int> utf8_decode(std::string_view str, size_t index) {
//     unsigned char c = static_cast<unsigned char>(str[index]);
//     if (c < 0x80) return {c, 1};
//     if ((c & 0xE0) == 0xC0) {
//         return {((c & 0x1F) << 6) | (str[index + 1] & 0x3F), 2};
//     }
//     if ((c & 0xF0) == 0xE0) {
//         return {((c & 0x0F) << 12) | ((str[index + 1] & 0x3F) << 6) | (str[index + 2] & 0x3F), 3};
//     }
//     if ((c & 0xF8) == 0xF0) {
//         return {((c & 0x07) << 18) | ((str[index + 1] & 0x3F) << 12) | ((str[index + 2] & 0x3F) << 6) | (str[index + 3] & 0x3F), 4};
//     }
//     return {0, 1}; // Invalid or logic error fallback
// }

font_ptr create_harfbuzz_font(const font_face_handle& face) {
    FT_Face ft_face = static_cast<FT_Face>(face);
    hb_font_t* font = hb_ft_font_create_referenced(ft_face);
    return font_ptr{font};
}

constexpr float normalize_hb_pos(hb_position_t pos) {
    return static_cast<float>(pos) / 64.0f;
}

struct layout_config {
    layout_direction direction;
    math::vec2 max_extent = math::vectors::constant2<float>::inf_positive_vec2;
    glyph_size_type font_size;
    linefeed line_feed_type;
    float tab_scale = 4.f;
};

// 定义一个文本片段，包含其对应的字体句柄
struct text_run {
    size_t start_index;
    size_t length;
    font_face_handle* face; // 指向 view 中的某个 handle
};

export glyph_layout layout_text(
    font_manager& manager,
    font_face_group_meta& font_group,
    std::string_view text,
    const layout_config& config
) {
    glyph_layout results{};
	if(text.empty())return results;
    math::vec2 min_bound{math::vectors::constant2<float>::inf_positive_vec2};
    math::vec2 max_bound{-math::vectors::constant2<float>::inf_positive_vec2};

    auto view = font_face_view{font_group.get_thread_local()};
    if (std::ranges::empty(view)) return results;

	results.elems.reserve(text.size() / 2);
    // 初始化主字体及大小
    auto& primary_face = view.face();
    const glyph_size_type snapped_size{
        get_snapped_size(config.font_size.x),
        get_snapped_size(config.font_size.y)
    };
    const math::vec2 scale_factor = config.font_size.as<float>() / snapped_size.as<float>();

    for(auto& face : view){
	    (void)face.set_size(snapped_size);
    }

    // --- 布局上下文初始化 ---
    math::vec2 pen{0, 0};
    const auto spacing = view.get_line_spacing_vec() * scale_factor;
    float line_spacing;
    float space_width;
    math::vec2 place_scaler = scale_factor;
    bool is_reversed = false;
    bool is_vertical = false;
    float math::vec2::* major_p;
    float math::vec2::* minor_p;

	//TODO support deduced
    hb_direction_t target_hb_dir = HB_DIRECTION_LTR;
    if (config.direction != layout_direction::deduced) {
        switch (config.direction) {
            case layout_direction::ltr: target_hb_dir = HB_DIRECTION_LTR; break;
            case layout_direction::rtl: target_hb_dir = HB_DIRECTION_RTL; break;
            case layout_direction::ttb: target_hb_dir = HB_DIRECTION_TTB; break;
            case layout_direction::btt: target_hb_dir = HB_DIRECTION_BTT; break;
            default: break;
        }
    }

    switch(target_hb_dir){
    case HB_DIRECTION_LTR : line_spacing = spacing.y;
	    break;
    case HB_DIRECTION_RTL : line_spacing = spacing.y;
	    place_scaler.flip_x();
	    is_reversed = true;
	    break;
    case HB_DIRECTION_TTB : line_spacing = spacing.x;
	    is_vertical = true;
	    break;
    case HB_DIRECTION_BTT : line_spacing = spacing.x;
	    place_scaler.flip_y();
	    is_reversed = true;
	    is_vertical = true;
	    break;
    default : std::unreachable();
    }

    FT_Load_Char(primary_face, FT_ULong{' '}, FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING);

    if (is_vertical) {
        major_p = &math::vec2::y; minor_p = &math::vec2::x;
    	space_width = line_spacing;
    } else {
        major_p = &math::vec2::x; minor_p = &math::vec2::y;
        space_width = normalize_hb_pos(primary_face->glyph->advance.x);
    }

    const auto advance_line = [&] {
        pen.*minor_p += line_spacing;
        pen.*major_p = 0;
    };

    // --- 定义 Run 处理器 (Lambda) ---
    // 返回 false 表示因超出范围需要停止整个布局
	const auto buffer = hb::make_buffer();

    const auto shape_run = [&](std::size_t start, std::size_t length, font_face_handle* face) -> bool {
    	hb_buffer_clear_contents(buffer.get());
        hb_buffer_add_utf8(
        	buffer.get(), text.data(),
        	static_cast<int>(text.size()), static_cast<unsigned int>(start), static_cast<int>(length));
        hb_buffer_set_direction(buffer.get(), target_hb_dir);
        hb_buffer_guess_segment_properties(buffer.get());

        auto hb_font = create_harfbuzz_font(*face);
        hb_ft_font_set_load_flags(hb_font.get(), FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING);
        hb_shape(hb_font.get(), buffer.get(), nullptr, 0);

        unsigned int len;
        hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer.get(), &len);
        hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buffer.get(), &len);

        for (unsigned int i = 0; i < len; ++i) {
            const glyph_index_t gid = infos[i].codepoint;

            // 控制字符处理
            switch (text[infos[i].cluster]) {
                case '\r':
                    if (config.line_feed_type == linefeed::CRLF) pen.*major_p = 0;
                    continue;
                case '\n':
                    if (config.line_feed_type == linefeed::CRLF) pen.*minor_p += line_spacing;
                    else advance_line();
                    continue;
                case '\t': {
                    float tab_step = config.tab_scale * space_width;
                    if (tab_step > std::numeric_limits<float>::epsilon()) {
                        pen.*major_p = (std::floor(pen.*major_p / tab_step) + 1) * tab_step;
                    }
                    continue;
                }
                default: break;
            }

            // 获取字形
            glyph_identity id{gid, snapped_size};
            glyph g = manager.get_glyph_exact(font_group, view, *face, id);

            float x_advance = normalize_hb_pos(pos[i].x_advance) * scale_factor.x;
            float y_advance = normalize_hb_pos(pos[i].y_advance) * scale_factor.y;
            float x_offset = normalize_hb_pos(pos[i].x_offset) * scale_factor.x;
            float y_offset = normalize_hb_pos(pos[i].y_offset) * scale_factor.y;

            // 边界检查
            if ((target_hb_dir == HB_DIRECTION_LTR && pen.x + x_advance > config.max_extent.x) ||
                (target_hb_dir == HB_DIRECTION_RTL && pen.x - x_advance < -config.max_extent.x) ||
                (target_hb_dir == HB_DIRECTION_TTB && pen.y - y_advance > config.max_extent.y) ||
                (target_hb_dir == HB_DIRECTION_BTT && pen.y + y_advance < -config.max_extent.y)) {
                advance_line();
            }

            // 计算位置
            math::vec2 draw_pos = pen;
            auto mov = [&] {
                if (target_hb_dir == HB_DIRECTION_LTR) { pen.x += x_advance; pen.y -= y_advance; }
                else if (target_hb_dir == HB_DIRECTION_RTL) { pen.x -= x_advance; pen.y -= y_advance; }
                else if (target_hb_dir == HB_DIRECTION_TTB) { pen.x += x_advance; pen.y -= y_advance; }
                else if (target_hb_dir == HB_DIRECTION_BTT) { pen.x += x_advance; pen.y += y_advance; }
            };

            if (is_reversed) mov();
            draw_pos.x += x_offset;
            draw_pos.y -= y_offset;

            math::frect aabb = g.metrics().place_to(draw_pos, scale_factor).expand(font_draw_expand * scale_factor);

            auto cur_max = max_bound;
            auto cur_min = min_bound;
            cur_max.max(aabb.vert_11());
            cur_min.min(aabb.vert_00());

            // 整体范围检查：如果超出限制，停止整个布局
            if (cur_max.*minor_p - cur_min.*minor_p > config.max_extent.*minor_p) {
                return false;
            }

            max_bound = cur_max;
            min_bound = cur_min;
            results.elems.push_back({aabb, std::move(g)});

            if (!is_reversed) mov();
        }
        return true;
    };

    // --- 主循环：边遍历边处理 ---
    {
        std::size_t current_idx = 0;
    	std::size_t run_start = 0;
        font_face_handle* current_face = nullptr;

        while (current_idx < text.size()) {
        	const auto len = encode::getUnicodeLength(text[current_idx]);
        	const auto codepoint = encode::utf_8_to_32(text.data() + current_idx, len);

            // 查找最佳字体
            auto [best_face, _] = view.find_glyph_of(codepoint);
            // find_glyph_of 保证返回有效指针 (找不到时返回 primary)

            if (current_face == nullptr) {
                current_face = best_face;
            }
            else if (best_face != current_face) {
                // 字体改变，立即处理上一个 Run
                if (!shape_run(run_start, current_idx - run_start, current_face)) {
                    goto finish_layout; // 停止布局
                }

                // 开始新 Run
                current_face = best_face;
                run_start = current_idx;
            }
            current_idx += len;
        }

        // 处理最后一段
        if (current_face != nullptr) {
            shape_run(run_start, text.size() - run_start, current_face);
        }
    }

finish_layout:
    if (results.elems.empty()) {
        results.extent = {0, 0};
    } else {
        auto extent = max_bound - min_bound;
        results.extent = extent;
        for (auto& elem : results.elems) {
            elem.aabb.move(-min_bound);
        }
    }

    return results;
}

} // namespace mo_yanxi::font::hb