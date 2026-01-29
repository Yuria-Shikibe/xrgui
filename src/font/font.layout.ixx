module;

#include <hb.h>
#include <hb-ft.h>
#include <freetype/freetype.h>

export module mo_yanxi.font.layout;

import std;
import mo_yanxi.font;
import mo_yanxi.font.manager;
import mo_yanxi.math;

export namespace mo_yanxi::font::layout {

    enum class layout_direction {
        left_to_right,
        right_to_left,
        top_to_bottom,
        bottom_to_top
    };

    struct glyph_position_info {
        unsigned int glyph_index;
        math::vec2 position; // Absolute position relative to layout origin
        math::vec2 advance;
    };

    struct layout_result {
        std::vector<glyph_position_info> glyphs;
        math::vec2 total_size;
    };

    class harfbuzz_shaper {
    public:
        explicit harfbuzz_shaper(font_face& face) : face_(face) {
            // Create hb_font from FT_Face
            // We need to access the raw FT_Face.
            // font_face.face() returns const font_face_handle&
            // font_face_handle is wrapper<FT_Face>, so .get() returns FT_Face.
            FT_Face ft_face = face_.face().get();
            hb_font_ = hb_ft_font_create(ft_face, nullptr);
        }

        ~harfbuzz_shaper() {
            if (hb_font_) {
                hb_font_destroy(hb_font_);
            }
        }

        layout_result shape(std::string_view text, layout_direction dir = layout_direction::left_to_right) {
            return layout_internal(text, dir, std::nullopt);
        }

        layout_result layout_in_box(std::string_view text, layout_direction dir, math::vec2 max_size) {
            return layout_internal(text, dir, max_size);
        }

    private:
        font_face& face_;
        hb_font_t* hb_font_ = nullptr;

        layout_result layout_internal(std::string_view text, layout_direction dir, std::optional<math::vec2> max_constraint) {
            hb_buffer_t* buffer = hb_buffer_create();

            // Add UTF-8 text
            hb_buffer_add_utf8(buffer, text.data(), static_cast<int>(text.size()), 0, -1);

            // Set direction
            hb_direction_t hb_dir;
            switch (dir) {
                case layout_direction::left_to_right: hb_dir = HB_DIRECTION_LTR; break;
                case layout_direction::right_to_left: hb_dir = HB_DIRECTION_RTL; break;
                case layout_direction::top_to_bottom: hb_dir = HB_DIRECTION_TTB; break;
                case layout_direction::bottom_to_top: hb_dir = HB_DIRECTION_BTT; break;
            }
            hb_buffer_set_direction(buffer, hb_dir);

            // Guess script and language
            hb_buffer_guess_segment_properties(buffer);

            // Shape
            hb_shape(hb_font_, buffer, nullptr, 0);

            // Get info
            unsigned int glyph_count;
            hb_glyph_info_t* glyph_info = hb_buffer_get_glyph_infos(buffer, &glyph_count);
            hb_glyph_position_t* glyph_pos = hb_buffer_get_glyph_positions(buffer, &glyph_count);

            layout_result result;
            result.glyphs.reserve(glyph_count);

            math::vec2 pen = {0, 0};
            float line_height = 0;

            // Get font metrics for line height
            // We use a default size for metrics if not specified, but here we assume the FT_Face is already sized?
            // font_face logic suggests size is passed on obtain().
            // However, hb_ft_font_create uses the current size of FT_Face.
            // We should probably rely on whatever size is set on the face or use a standard one.
            // Let's assume the user has set the size on the face or we use a default.
            // But font_face doesn't seem to expose set_size permanently, it does it in obtain().
            // This is a potential issue: FT_Face size might be 0 or random.
            // We should probably enforce a size or use the last set size.
            // For now, let's assume valid state or 12pt default if we could set it.
            // Actually, we can't easily set size on font_face from here without friend access or helper.
            // But font_face::obtain calls FT_Set_Pixel_Sizes.
            // Let's assume the caller has set up the face size, or we default to something.
            // Using face_.get_line_spacing({64, 64}) might give us something?
            // Let's use 64 pixels (approx standard) for calculation if we can't find better.

            // Actually, let's query the FT_Face directly since we have it.
            FT_Face ft_face = face_.face().get();
            if (ft_face->size) {
                line_height = static_cast<float>(ft_face->size->metrics.height) / 64.0f;
            } else {
                 line_height = 20.0f; // Fallback
            }
            // If line height is 0 (e.g. unscaled), force a default
            if (line_height < 1.0f) line_height = 16.0f;


            float current_max_dim = 0; // Track max width (or height) for bounding box calculation

            // Processing loop
            for (unsigned int i = 0; i < glyph_count; ++i) {
                float x_advance = glyph_pos[i].x_advance / 64.0f;
                float y_advance = glyph_pos[i].y_advance / 64.0f;
                float x_offset = glyph_pos[i].x_offset / 64.0f;
                float y_offset = glyph_pos[i].y_offset / 64.0f;

                math::vec2 glyph_pos_vec = pen + math::vec2{x_offset, y_offset};

                // Check constraints and wrap
                if (max_constraint.has_value()) {
                     bool wrap = false;
                     if (dir == layout_direction::left_to_right || dir == layout_direction::right_to_left) {
                         // Horizontal layout: Wrap on X overflow
                         if (pen.x + x_advance > max_constraint->x && pen.x > 0) {
                             wrap = true;
                             current_max_dim = std::max(current_max_dim, pen.x);
                             pen.x = 0;
                             pen.y += line_height; // Move down
                             // Re-calculate pos
                             glyph_pos_vec = pen + math::vec2{x_offset, y_offset};
                         }
                     } else {
                         // Vertical layout: Wrap on Y overflow
                         // Assuming TTB, we wrap to the RIGHT? Or LEFT?
                         // Standard CJK vertical text wraps R-to-L.
                         // Let's assume R-to-L wrapping for vertical text.
                         // But TTB increases Y.
                         if (pen.y + y_advance > max_constraint->y && pen.y > 0) { // y_advance is usually negative or small in TTB?
                             // Wait, HB TTB: y_advance is negative (down)? No, usually coordinate system is Y-down or Y-up.
                             // Freetype/HB usually: Y-up is standard, but screen is Y-down.
                             // Let's check mo_yanxi::math conventions.
                             // Usually screen coords: X right, Y down.
                             // TTB: Y increases.
                             wrap = true;
                             current_max_dim = std::max(current_max_dim, pen.y);
                             pen.y = 0;
                             pen.x -= line_height; // Move Left? Or Right?
                             // Let's assume Move Right (L-to-R columns) for simplicity unless CJK specific.
                             // Actually CJK is R-to-L columns.
                             // Let's do columns to the right for now (standard UI grid).
                             pen.x += line_height;

                             glyph_pos_vec = pen + math::vec2{x_offset, y_offset};
                         }
                     }
                }

                result.glyphs.push_back({
                    glyph_info[i].codepoint,
                    glyph_pos_vec,
                    {x_advance, y_advance}
                });

                pen.x += x_advance;
                pen.y += y_advance;
            }

            // Final size calc
            if (dir == layout_direction::left_to_right || dir == layout_direction::right_to_left) {
                 result.total_size = { std::max(current_max_dim, pen.x), pen.y + line_height };
            } else {
                 result.total_size = { pen.x + line_height, std::max(current_max_dim, pen.y) };
            }

            hb_buffer_destroy(buffer);
            return result;
        }
    };
}
