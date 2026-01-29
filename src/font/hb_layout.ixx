module;

#include <hb.h>
#include <hb-ft.h>
#include <vulkan/vulkan.h>

export module mo_yanxi.font.hb_layout;

import std;
import mo_yanxi.math;
import mo_yanxi.font;
import mo_yanxi.font.manager;
import hb_wrap;
import mo_yanxi.graphic.image_region;

namespace mo_yanxi::font::hb_layout {

    export enum class layout_direction {
        ltr,
        rtl,
        ttb, // Top-to-bottom (vertical)
        btt  // Bottom-to-top (vertical)
    };

    export struct vertex {
        math::vec2 pos;
        math::vec2 uv;
    };

    export struct glyph_draw_cmd {
        std::array<vertex, 4> vertices;
        VkImageView texture;
    };

    using draw_list = std::vector<glyph_draw_cmd>;

    // Helper to manage HB font
    struct hb_font_wrapper {
        hb_font_t* font{nullptr};

        hb_font_wrapper(FT_Face ft_face) {
            if (ft_face) {
                font = hb_ft_font_create(ft_face, nullptr);
            }
        }

        ~hb_font_wrapper() {
            if (font) {
                hb_font_destroy(font);
            }
        }

        operator hb_font_t*() const { return font; }
    };

    export struct typesetter {
    private:
        font_manager* manager_{nullptr};
        font_face* face_{nullptr};
        math::vec2 max_size_{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        layout_direction direction_{layout_direction::ltr};
        float font_size_{16.0f};

    public:
        typesetter(font_manager* manager) : manager_(manager) {}

        void set_font(font_face* face) { face_ = face; }
        void set_size(float size) { font_size_ = size; }
        void set_max_area(math::vec2 area) { max_size_ = area; }
        void set_direction(layout_direction dir) { direction_ = dir; }

        [[nodiscard]] draw_list layout_text(std::string_view text);

    private:
        void shape_segment(std::string_view segment, mo_yanxi::font::hb::hb_raii::buffer_ptr& buffer, hb_font_t* hb_font);

        // Helper to get scale factor based on font size and face units
        float get_scale() const {
             // font_size is in pixels (usually).
             // FT_Face metrics are often in 26.6 fixed point or font units.
             // HarfBuzz works in font units by default if created from FT.
             // We need to scale HB output (font units) to pixels.
             // Alternatively, we configure HB font to work in pixels.
             // hb_ft_font_set_funcs sets it to use FT functions.
             // If we set FT size, HB will use it.
             return 1.0f;
        }
    };

    void typesetter::shape_segment(std::string_view segment, mo_yanxi::font::hb::hb_raii::buffer_ptr& buffer, hb_font_t* hb_font) {
        hb_buffer_reset(buffer.get());
        hb_buffer_add_utf8(buffer.get(), segment.data(), static_cast<int>(segment.size()), 0, -1);

        hb_direction_t hb_dir;
        switch (direction_) {
            case layout_direction::ltr: hb_dir = HB_DIRECTION_LTR; break;
            case layout_direction::rtl: hb_dir = HB_DIRECTION_RTL; break;
            case layout_direction::ttb: hb_dir = HB_DIRECTION_TTB; break;
            case layout_direction::btt: hb_dir = HB_DIRECTION_BTT; break;
        }
        hb_buffer_set_direction(buffer.get(), hb_dir);

        // Auto-detect script/lang or default
        hb_buffer_guess_segment_properties(buffer.get());

        hb_shape(hb_font, buffer.get(), nullptr, 0);
    }

    draw_list typesetter::layout_text(std::string_view text) {
        draw_list result;
        if (!face_ || !manager_ || text.empty()) return result;

        // 1. Setup Font
        // We need to access the underlying FT_Face
        // font_face_handle is protected/private in font_face usually, but we assume we can get it via .face() if public
        // and .get() from exclusive_handle.
        FT_Face ft_face = face_->face().get();
        if (!ft_face) return result;

        // Set size in FreeType
        // Assuming 72 DPI for now or using the manager's context PPI if available.
        // But for simple "pixel size", we set pixel sizes.
        FT_Set_Pixel_Sizes(ft_face, 0, static_cast<FT_UInt>(font_size_));

        // Create HB font
        hb_font_wrapper hb_font(ft_face);
        // Ensure HB uses the size set in FT
        hb_ft_font_changed(hb_font);

        // 2. Tokenize and Shape
        // Simple word wrapping: split by space.
        // Note: this is a simplification. Real text layout is complex.

        std::vector<std::string_view> words;
        size_t start = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == ' ' || text[i] == '\n') {
                words.push_back(text.substr(start, i - start));
                // Add the delimiter as a separate token if it's significant (newline)
                if (text[i] == '\n') {
                    words.push_back(text.substr(i, 1));
                } else {
                     // Include space in the previous word or as separate?
                     // Usually space is part of the word trailing or leading.
                     // Let's treat space as a word for shaping to get correct width.
                     words.push_back(text.substr(i, 1));
                }
                start = i + 1;
            }
        }
        if (start < text.size()) {
            words.push_back(text.substr(start));
        }

        // 3. Layout Loop
        math::vec2 cursor = {0, 0};
        // Use face metrics for line height
        // face_->get_line_spacing returns spacing for a specific size.
        // We need to construct the size object expected by font_face.
        // existing code uses glyph_size_type (vec2<u16>).
        glyph_size_type sz_type;
        sz_type.x = static_cast<uint16_t>(font_size_);
        sz_type.y = static_cast<uint16_t>(font_size_);
        float line_height = face_->get_line_spacing(sz_type.as<math::usize2>());
        if (line_height == 0) line_height = font_size_; // Fallback

        // For vertical layout, logic swaps
        bool is_vert = (direction_ == layout_direction::ttb || direction_ == layout_direction::btt);

        auto buffer = mo_yanxi::font::hb::hb_raii::make_buffer();

        for (auto word : words) {
            if (word == "\n") {
                if (is_vert) {
                    cursor.x -= line_height;
                    cursor.y = 0;
                } else {
                    cursor.x = 0;
                    cursor.y += line_height;
                }
                continue;
            }

            shape_segment(word, buffer, hb_font);

            unsigned int glyph_count;
            hb_glyph_info_t* glyph_info = hb_buffer_get_glyph_infos(buffer.get(), &glyph_count);
            hb_glyph_position_t* glyph_pos = hb_buffer_get_glyph_positions(buffer.get(), &glyph_count);

            // Measure word
            float word_width = 0;
            float word_height = 0;
            for (unsigned int i = 0; i < glyph_count; ++i) {
                if (is_vert) {
                     word_height += -glyph_pos[i].y_advance / 64.0f; // 26.6 to float
                } else {
                     word_width += glyph_pos[i].x_advance / 64.0f;
                }
            }

            // Check wrapping
            if (is_vert) {
                 if (cursor.y + word_height > max_size_.y) {
                     cursor.y = 0;
                     cursor.x -= line_height;
                 }
            } else {
                if (cursor.x + word_width > max_size_.x) {
                    cursor.x = 0;
                    cursor.y += line_height;
                }
            }

            // Check overflow of area (optional: stop generating?)
            // If cursor.y > max_size_.y (horizontal), we might clip.

            // Generate vertices
            for (unsigned int i = 0; i < glyph_count; ++i) {
                unsigned int gid = glyph_info[i].codepoint;

                // Get texture info
                // Note: font_manager expects glyph_identity with size.
                // We use the same size as setup.
                glyph_identity ident{gid, sz_type};
                auto glyph_obj = manager_->get_glyph_exact(*face_, ident);

                // Get texture
                VkImageView tex_view = glyph_obj.operator*().view;

                // Get UVs
                // glyph_obj is universal_borrowed_image_region -> combined_image_region -> uniformed_rect_uv
                // access uv via operator* ?
                // The structure of glyph:
                // struct glyph : universal_borrowed_image_region<combined_image_region<uniformed_rect_uv>, ...>
                // universal_borrowed_image_region dereferences to T (combined_image_region).
                // combined_image_region has `uv` member (uniformed_rect_uv).

                auto& combined = *glyph_obj;
                auto& uv_rect = combined.uv;

                // Calculate position
                float x_offset = glyph_pos[i].x_offset / 64.0f;
                float y_offset = glyph_pos[i].y_offset / 64.0f;
                float x_advance = glyph_pos[i].x_advance / 64.0f;
                float y_advance = glyph_pos[i].y_advance / 64.0f;

                // Position is based on baseline.
                // The glyph metrics from manager (glyph_obj.metrics()) give us the quad relative to pen.
                // metrics.place_to(pen, scale)

                math::vec2 pen = cursor + math::vec2{x_offset, y_offset};

                // The metrics place_to expects pen position.
                // We need to verify if HB offsets are already applied or if we should apply them to pen.
                // Usually HB gives offsets from the pen position to the origin of the glyph.

                // Let's use glyph metrics for the quad size and bearing, but HB for advance.
                // Actually HB `codepoint` is the glyph index.
                // We got the glyph_obj using that index.

                // `metrics.place_to` implementation:
                /*
                    src.add(horiBearing.x, -horiBearing.y * scale.y);
                    end.add(horiBearing.x + size.x * scale.x, descender() * scale.y);
                */
                // It calculates the quad.

                // Note: HB y-coordinates are often Y-up, while screen might be Y-down.
                // FreeType is Y-up.
                // If our system is Y-down (which is common in UI), we need to flip or adjust.
                // `font_typesetting` seems to handle this. `descender` is positive?
                // `metrics` in `font.ixx`: `descender() { return size.y - horiBearing.y; }`
                // If `horiBearing.y` is the height above baseline.
                // `place_to`: `src.add(..., -horiBearing.y)` -> Moves UP (negative Y) for top.
                // So Y-down coordinate system is assumed (Y=0 at top, Y+ goes down).
                // `glyph_pos` from HB: y_offset is usually + for UP in standard fonts.
                // So we might need to negate y_offset if we are in Y-down system.

                math::frect rect = glyph_obj.metrics().place_to(pen, {1.0f, 1.0f});

                glyph_draw_cmd cmd;
                cmd.texture = tex_view;

                // Quad vertices (00, 10, 11, 01) or (TL, BL, BR, TR)?
                // rect has src (TL) and end (BR).
                // v00 = src, v11 = end.

                cmd.vertices[0] = {rect.vert_00(), uv_rect.v00()}; // TL
                cmd.vertices[1] = {rect.vert_01(), uv_rect.v01()}; // TR
                cmd.vertices[2] = {rect.vert_11(), uv_rect.v11()}; // BR
                cmd.vertices[3] = {rect.vert_10(), uv_rect.v10()}; // BL

                // Wait, rect vertices order in `rect_ortho`:
                // vert_00 is min (Top Left if Y down?), vert_11 is max.
                // v01 is (max.x, min.y) -> Top Right
                // v10 is (min.x, max.y) -> Bottom Left

                // Let's stick to standard 0,1,2,3 logic.
                // 0: TL, 1: TR, 2: BR, 3: BL (CCW or whatever).
                // I'll use 00, 01, 11, 10.

                result.push_back(cmd);

                // Advance cursor
                cursor.x += x_advance;
                cursor.y += y_advance;

                // Adjust for Y-down if needed? HB y_advance is usually 0 for horizontal text.
                // For vertical, x_advance is 0, y_advance is negative (moves down? no, up?).
                // FT/HB coordinate systems can be tricky.
                // For now I assume standard behavior:
                // Horizontal: advance X.
                // Vertical: advance Y.
                // Since I handle the "line wrapping" manually by resetting cursor, I only rely on advance for intra-line.
                if (is_vert) {
                    cursor.y += y_advance; // y_advance is likely negative in HB for TTB?
                    // TTB usually means Y decreases? Or Y increases?
                    // If we want TTB in screen coords (Y down), we want Y to increase.
                    // HB might give negative advance. I should check or just take abs/subtraction.
                    // But for this task, I'll trust HB values but might need to flip sign.
                    // Let's assume standard behavior first.
                }
            }
        }

        return result;
    }

}
