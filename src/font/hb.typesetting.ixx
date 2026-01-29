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

import std;

namespace mo_yanxi::font::hb_layout {

    export enum class layout_direction {
        horizontal_ltr,
        horizontal_rtl,
        vertical_ttb,
        vertical_btt
    };

    export struct layout_constraint {
        math::vec2 max_size;
    };

    export struct layout_result {
        math::frect aabb;
        math::frect uv;
        glyph_borrow texture;
    };

    font_ptr create_harfbuzz_font(const font_face_handle& face) {
        // cast to FT_Face using implicit conversion from exclusive_handle<FT_Face>
        FT_Face ft_face = static_cast<FT_Face>(face);
        hb_font_t* font = hb_ft_font_create_referenced(ft_face);
        return font_ptr{font};
    }

    constexpr float normalize_hb_pos(hb_position_t pos) {
        return static_cast<float>(pos) / 64.0f;
    }

    export std::vector<layout_result> layout_text(
        font_manager& manager,
        font_face_group_meta& font_group,
        std::string_view text,
        layout_direction dir,
        layout_constraint constraint,
        glyph_size_type font_size
    ) {
        std::vector<layout_result> results;

        auto& view = font_group.get_thread_local();
        if (view.begin() == view.end()) return results;
        auto& face_handle = view.face();

        // 1. Calculate Snapped Size and Scale
        glyph_size_type snapped_size = {
            get_snapped_size(font_size.x),
            get_snapped_size(font_size.y)
        };

        math::vec2 scale_factor = font_size.as<float>() / snapped_size.as<float>();

        // 2. Set Face Size (for HarfBuzz / FT metrics)
        // We set it to snapped size so metrics from HB match metrics from font_manager
        face_handle.set_size(snapped_size);

        // 3. Setup HarfBuzz Buffer
        auto buffer = hb::make_buffer();
        hb_buffer_add_utf8(buffer.get(), text.data(), static_cast<int>(text.size()), 0, -1);

        hb_direction_t hb_dir = HB_DIRECTION_LTR;
        switch (dir) {
            case layout_direction::horizontal_ltr: hb_dir = HB_DIRECTION_LTR; break;
            case layout_direction::horizontal_rtl: hb_dir = HB_DIRECTION_RTL; break;
            case layout_direction::vertical_ttb: hb_dir = HB_DIRECTION_TTB; break;
            case layout_direction::vertical_btt: hb_dir = HB_DIRECTION_BTT; break;
        }
        hb_buffer_set_direction(buffer.get(), hb_dir);
        hb_buffer_guess_segment_properties(buffer.get());

        // 4. Create HarfBuzz Font
        auto font = create_harfbuzz_font(face_handle);
        hb_ft_font_set_load_flags(font.get(), FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING);

        // 5. Shape
        hb_shape(font.get(), buffer.get(), nullptr, 0);

        unsigned int len;
        hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer.get(), &len);
        hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buffer.get(), &len);

        // 6. Layout Loop
        math::vec2 pen{0, 0};

        // Adjust start position for RTL/BTT if needed (relative to max_size)
        // For simplicity, we start at 0,0 and users can align the result AABB later.
        // However, for wrapping, we need to respect the constraint width.
        // If RTL, we might start at max_width and move left.
        if (dir == layout_direction::horizontal_rtl) {
            pen.x = constraint.max_size.x;
        } else if (dir == layout_direction::vertical_btt) {
            pen.y = constraint.max_size.y;
        }

        // Line height calculation
        // get_line_spacing returns spacing in pixels for the given size
        float line_height = view.get_line_spacing(snapped_size) * scale_factor.y;

        for (unsigned int i = 0; i < len; ++i) {
            glyph_index_t gid = infos[i].codepoint;

            // Acquire glyph from manager
            glyph_identity id{gid, snapped_size};
            glyph g = manager.get_glyph_exact(font_group, view, face_handle, id);

            float x_advance = normalize_hb_pos(pos[i].x_advance) * scale_factor.x;
            float y_advance = normalize_hb_pos(pos[i].y_advance) * scale_factor.y; // Typically negative for TTB
            float x_offset = normalize_hb_pos(pos[i].x_offset) * scale_factor.x;
            float y_offset = normalize_hb_pos(pos[i].y_offset) * scale_factor.y; // Typically negative for TTB

            // Wrapping Logic
            bool wrapped = false;

            if (dir == layout_direction::horizontal_ltr) {
                if (pen.x + x_advance > constraint.max_size.x) {
                    pen.x = 0;
                    pen.y += line_height;
                    wrapped = true;
                }
            } else if (dir == layout_direction::horizontal_rtl) {
                // Moving left: pen.x decreases.
                // Check if we go below 0? Or wrap if we used too much width?
                // x_advance is positive.
                if (pen.x - x_advance < 0) {
                    pen.x = constraint.max_size.x;
                    pen.y += line_height;
                    wrapped = true;
                }
            } else if (dir == layout_direction::vertical_ttb) {
                // Moving down: pen.y increases.
                // y_advance is negative from HB (Y-Up). We flip it: -y_advance is positive distance.
                float dist = -y_advance;
                if (pen.y + dist > constraint.max_size.y) {
                    pen.y = 0;
                    pen.x += line_height;
                    wrapped = true;
                }
            } else if (dir == layout_direction::vertical_btt) {
                // Moving up: pen.y decreases.
                // y_advance is positive from HB (Y-Up). We flip it: -y_advance is negative distance (move up).
                float dist = y_advance; // Distance is positive
                if (pen.y - dist < 0) {
                    pen.y = constraint.max_size.y;
                    pen.x += line_height;
                    wrapped = true;
                }
            }

            // Calculate Draw Position
            // pen is the origin. Add offsets.
            // Note: HB offsets are usually (x, y) relative to origin.
            // For Y-Down screen:
            // y_offset positive means "Up" in HB. So "Down" (smaller Y) in Y-Down?
            // Or just subtract y_offset.
            // Let's assume standard flip Y convention: screen_y = -hb_y.

            math::vec2 draw_pos = pen;
            draw_pos.x += x_offset;
            draw_pos.y -= y_offset; // Flip Y offset

            // Calculate AABB
            // place_to expects pos and scale.
            // It uses horiBearing etc.
            // Assuming place_to generates Y-Down coords (top < bottom).
            math::frect aabb = g.metrics().place_to(draw_pos, scale_factor);

            math::frect uv = (*g).uv_rect;

            results.push_back({aabb, uv, std::move(g)});

            // Advance Pen
            if (dir == layout_direction::horizontal_ltr) {
                pen.x += x_advance;
                pen.y -= y_advance;
            } else if (dir == layout_direction::horizontal_rtl) {
                pen.x -= x_advance;
                pen.y -= y_advance;
            } else if (dir == layout_direction::vertical_ttb) {
                pen.x += x_advance;
                pen.y -= y_advance; // y_advance is negative, so this adds positive value
            } else if (dir == layout_direction::vertical_btt) {
                pen.x += x_advance;
                pen.y -= y_advance;
            }
        }

        return results;
    }
}
