module;

#include <hb.h>
#include <hb-ft.h>
#include <freetype/freetype.h>
#include <cassert>

export module mo_yanxi.hb.typesetting;

import std;
import mo_yanxi.font;
import mo_yanxi.font.manager;
import mo_yanxi.typesetting;
import mo_yanxi.typesetting.rich_text;
import mo_yanxi.hb.wrap;
import mo_yanxi.math;
import mo_yanxi.graphic.image_region;
import mo_yanxi.graphic.color;

import mo_yanxi.cache;

namespace mo_yanxi::font::hb{

export enum struct layout_direction{ deduced, ltr, rtl, ttb, btt };
export enum struct linefeed{ LF, CRLF };
export enum struct content_alignment{ start, center, end, justify };

export struct layout_result{
	math::frect aabb;
	graphic::color color;
	glyph_borrow texture;
};

export struct underline{
	math::vec2 start;
    math::vec2 end; // Changed from single start to start+end for easier processing
	float thickness;
	graphic::color color;
};

export struct glyph_layout{
	std::vector<layout_result> elems;
	std::vector<underline> underlines;
	math::vec2 extent;
};

font_ptr create_harfbuzz_font(const font_face_handle& face){
	hb_font_t* font = hb_ft_font_create_referenced(face);
	return font_ptr{font};
}

constexpr float normalize_hb_pos(hb_position_t pos){
	return static_cast<float>(pos) / 64.0f;
}

export struct layout_config{
	layout_direction direction;
	math::vec2 max_extent = math::vectors::constant2<float>::inf_positive_vec2;
	glyph_size_type font_size;
	linefeed line_feed_type;
	content_alignment align = content_alignment::start;
	float tab_scale = 4.f;
	float line_spacing_scale = 1.25f;
	float line_spacing_fixed_distance = 0.f;
	char32_t wrap_indicator_char = U'\u2925';
	constexpr bool has_wrap_indicator() const noexcept{ return wrap_indicator_char; }
};

struct layout_block{
	std::vector<layout_result> glyphs;
    std::vector<underline> underlines; // [Added] Store underlines in block

	math::vec2 cursor{};

	math::vec2 pos_min{math::vectors::constant2<float>::inf_positive_vec2};
	math::vec2 pos_max{-math::vectors::constant2<float>::inf_positive_vec2};

	float block_ascender = 0.f;
	float block_descender = 0.f;

	void clear(){
		glyphs.clear();
        underlines.clear(); // [Added]
		cursor = {};
		pos_min = math::vectors::constant2<float>::inf_positive_vec2;
		pos_max = -math::vectors::constant2<float>::inf_positive_vec2;
		block_ascender = 0.f;
		block_descender = 0.f;
	}

	void push_back(math::frect glyph_region, const layout_result& glyph){
		glyphs.push_back(glyph);
		pos_min.min(glyph_region.vert_00());
		pos_max.max(glyph_region.vert_11());
	}

    // [Added] Push back an underline segment
    void push_back_underline(const underline& ul) {
        underlines.push_back(ul);
        // Expand bounding box to include underline
        // Assuming horizontal underline for AABB calculation simplicity
        math::vec2 ul_min = math::min(ul.start, ul.end);
        math::vec2 ul_max = math::max(ul.start, ul.end);

        // Add thickness expansion (approximate)
        ul_min.y -= ul.thickness / 2.f;
        ul_max.y += ul.thickness / 2.f;

        pos_min.min(ul_min);
        pos_max.max(ul_max);
    }

	void push_front(math::frect glyph_region, const layout_result& glyph, math::vec2 glyph_advance){
		for(auto&& layout_result : glyphs){
			layout_result.aabb.move(glyph_advance);
		}
        // [Added] Shift existing underlines
        for(auto&& ul : underlines){
            ul.start += glyph_advance;
            ul.end += glyph_advance;
        }

		glyphs.insert(glyphs.begin(), glyph);
		pos_min += glyph_advance;
		pos_max += glyph_advance;
		pos_min.min(glyph_region.vert_00());
		pos_max.max(glyph_region.vert_11());
		cursor += glyph_advance;
	}

	math::frect get_local_draw_bound(){
		if(glyphs.empty() && underlines.empty()){ return {}; }
		return {tags::unchecked, tags::from_vertex, pos_min, pos_max};
	}
};

struct layout_context{
	struct line_buffer_t{
		std::vector<layout_result> elems;
        std::vector<underline> underlines; // [Added]
		float width = 0.f;
		float max_ascender = 0.f;
		float max_descender = 0.f;

		void clear(){
			elems.clear();
            underlines.clear(); // [Added]
			width = 0.f;
			max_ascender = 0.f;
			max_descender = 0.f;
		}

		void append(layout_block& block, float math::vec2::* major_axis){
			math::vec2 move_vec{};
			move_vec.*major_axis = width;

			elems.reserve(elems.size() + block.glyphs.size());
			for(auto& g : block.glyphs){
				g.aabb.move(move_vec);
				elems.push_back(std::move(g));
			}

            // [Added] Transfer underlines
            underlines.reserve(underlines.size() + block.underlines.size());
            for(auto& ul : block.underlines) {
                ul.start += move_vec;
                ul.end += move_vec;
                underlines.push_back(std::move(ul));
            }

			width += block.cursor.*major_axis;
			max_ascender = std::max(max_ascender, block.block_ascender);
			max_descender = std::max(max_descender, block.block_descender);
		}
	};

	struct indicator_cache{
		glyph_borrow texture;
		math::frect glyph_aabb;
		math::vec2 advance;
	};

private:
	font_manager& manager;
	font_face_view base_view;
	layout_config config;
	lru_cache<font_face_handle*, font_ptr, 4> hb_cache_;
	hb::buffer_ptr hb_buffer_;

	math::vec2 min_bound{math::vectors::constant2<float>::inf_positive_vec2};
	math::vec2 max_bound{-math::vectors::constant2<float>::inf_positive_vec2};

	float default_line_thickness = 0.f;
	float default_space_width = 0.f;

	float prev_line_descender_ = 0.f;
	float current_baseline_pos_ = 0.f;

	bool is_first_line_ = true;
	hb_direction_t target_hb_dir = HB_DIRECTION_LTR;

	float math::vec2::* major_p = &math::vec2::x;
	float math::vec2::* minor_p = &math::vec2::y;

	line_buffer_t line_buffer_;

	layout_block current_block{};
	indicator_cache cached_indicator_;
	type_setting::rich_text_context rich_text_context_;
	type_setting::tokenized_text::token_iterator token_hard_last_iterator;
	type_setting::tokenized_text::token_iterator token_soft_last_iterator;

	std::vector<hb_feature_t> feature_stack_;

public:
	layout_context(
		font_manager& m, font_face_view& v,
		const layout_config& c, const type_setting::tokenized_text& t
	) : manager(m), base_view(v), config(c)
	, token_hard_last_iterator(t.get_init_token())
	, token_soft_last_iterator(t.get_init_token())
	{
		hb_buffer_ = hb::make_buffer();

		if(config.direction != layout_direction::deduced){
			switch(config.direction){
			case layout_direction::ltr : target_hb_dir = HB_DIRECTION_LTR; break;
			case layout_direction::rtl : target_hb_dir = HB_DIRECTION_RTL; break;
			case layout_direction::ttb : target_hb_dir = HB_DIRECTION_TTB; break;
			case layout_direction::btt : target_hb_dir = HB_DIRECTION_BTT; break;
			default : std::unreachable();
			}
		}

		const auto snapped_base_size = get_snapped_size_vec(config.font_size);
		auto& primary_face = base_view.face();
		(void)primary_face.set_size(snapped_base_size);
		FT_Load_Char(primary_face, FT_ULong{' '}, FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING);
		math::vec2 base_scale_factor = config.font_size.as<float>() / snapped_base_size.as<float>();

		if(is_vertical_()){
			major_p = &math::vec2::y;
			minor_p = &math::vec2::x;
			default_line_thickness = normalize_len(primary_face->size->metrics.max_advance) * base_scale_factor.x;
			default_space_width = default_line_thickness;
		} else{
			major_p = &math::vec2::x;
			minor_p = &math::vec2::y;
			default_line_thickness = normalize_len(primary_face->size->metrics.height) * base_scale_factor.y;
			default_space_width = normalize_hb_pos(primary_face->glyph->advance.x) * base_scale_factor.x;
		}

		if(config.has_wrap_indicator()){
			auto [face, index] = base_view.find_glyph_of(config.wrap_indicator_char);
			if(index){
				glyph_identity id{index, snapped_base_size};
				glyph g = manager.get_glyph_exact(*face, id);
				const auto& m = g.metrics();
				const auto advance = m.advance * base_scale_factor;
				math::vec2 adv{};
				adv.*major_p = advance.*major_p * (is_vertical_() ? -1 : 1);
				math::frect local_aabb = m.place_to({}, base_scale_factor);
				cached_indicator_ = indicator_cache{std::move(g), local_aabb, adv};
			} else{
				config.wrap_indicator_char = 0;
			}
		}
	}

	[[nodiscard]] glyph_layout crop(const type_setting::tokenized_text& full_text) &&{
		if(config.direction == layout_direction::deduced){
			hb_buffer_add_utf32(hb_buffer_.get(), reinterpret_cast<const std::uint32_t*>(full_text.get_text().data()),
				static_cast<int>(full_text.get_text().size()), 0, -1);
			hb_buffer_guess_segment_properties(hb_buffer_.get());
			target_hb_dir = hb_buffer_get_direction(hb_buffer_.get());
			if(target_hb_dir == HB_DIRECTION_INVALID) target_hb_dir = HB_DIRECTION_LTR;
		}

		glyph_layout results{};

		results.elems.reserve(full_text.get_text().size());

		process_layout(full_text, results);
		finalize(results);

		return results;
	}

private:
	bool is_reversed_() const noexcept{ return target_hb_dir == HB_DIRECTION_RTL || target_hb_dir == HB_DIRECTION_BTT; }
	bool is_vertical_() const noexcept{ return target_hb_dir == HB_DIRECTION_TTB || target_hb_dir == HB_DIRECTION_BTT; }

	[[nodiscard]] glyph_size_type get_current_snapped_size() const noexcept{
		auto req_size = rich_text_context_.get_size(config.font_size.as<float>());
		return get_snapped_size_vec(req_size);
	}

	static glyph_size_type get_snapped_size_vec(glyph_size_type v) noexcept{
		return {get_snapped_size(v.x), get_snapped_size(v.y)};
	}

	static glyph_size_type get_snapped_size_vec(math::vec2 v) noexcept{
		return get_snapped_size_vec(v.as<glyph_size_type::value_type>());
	}

	void advance_line(glyph_layout& results) noexcept{
		const bool is_empty_line = line_buffer_.elems.empty();
		const float current_asc = is_empty_line ? get_scaled_default_line_thickness() / 2.f : line_buffer_.max_ascender;
		const float current_desc = is_empty_line ? get_scaled_default_line_thickness() / 2.f : line_buffer_.max_descender;

		if(is_first_line_){
			current_baseline_pos_ = current_asc;
			is_first_line_ = false;
		} else{
			float metrics_sum = prev_line_descender_ + current_asc;
			float step = metrics_sum * config.line_spacing_scale + config.line_spacing_fixed_distance;
			current_baseline_pos_ += step;
		}

		float align_offset = 0.f;
		const float content_width = line_buffer_.width;

		if(!config.max_extent.is_Inf()){
			const float container_width = config.max_extent.*major_p;
			float remaining = container_width - content_width;
			if(remaining > 0.001f){
				switch(config.align){
				case content_alignment::center : align_offset = remaining / 2.0f;
					break;
				case content_alignment::end : align_offset = remaining;
					break;
				case content_alignment::justify :
					break;
				default : break;
				}
			}
		}

		math::vec2 alignment_vec{};
		alignment_vec.*major_p = align_offset;
		math::vec2 baseline_vec{};
		baseline_vec.*minor_p = current_baseline_pos_;

		for(auto& elem : line_buffer_.elems){
			elem.aabb.move(alignment_vec + baseline_vec);
			results.elems.push_back(std::move(elem));
			min_bound.min(elem.aabb.vert_00());
			max_bound.max(elem.aabb.vert_11());
		}

		// [Added] Process underlines from line buffer to global results
		for(auto& ul : line_buffer_.underlines){
			ul.start += alignment_vec + baseline_vec;
			ul.end += alignment_vec + baseline_vec;

			// [Fix] Update bounds to include underlines
			math::vec2 ul_min = math::min(ul.start, ul.end);
			math::vec2 ul_max = math::max(ul.start, ul.end);
			// Expand by half thickness to account for stroke width
			ul_min.y -= ul.thickness / 2.f;
			ul_max.y += ul.thickness / 2.f;

			min_bound.min(ul_min);
			max_bound.max(ul_max);

			results.underlines.push_back(std::move(ul));
		}

		prev_line_descender_ = current_desc;
		line_buffer_.clear();
	}

	bool flush_block(glyph_layout& results){
		if(current_block.glyphs.empty() && current_block.underlines.empty()) return true;

		const float current_line_width = line_buffer_.width;
		const float block_width = current_block.cursor.*major_p;
		const float max_width = config.max_extent.*major_p;

		bool overflow = false;
		if (!config.max_extent.is_Inf()) {
			if (current_line_width > 0.001f && (current_line_width + block_width > max_width)) {
				overflow = true;
			}
		}

		if (overflow) {
			advance_line(results);

			if(config.has_wrap_indicator()){
				const auto& ind = cached_indicator_;
				const float scale = get_current_relative_scale();
				auto scaled_aabb = ind.glyph_aabb.copy();
				math::vec2 p_min = scaled_aabb.vert_00() * scale;
				math::vec2 p_max = scaled_aabb.vert_11() * scale;
				scaled_aabb = {tags::unchecked, tags::from_vertex, p_min, p_max};
				math::vec2 scaled_advance = ind.advance * scale;
				current_block.push_front(
					scaled_aabb,
					{scaled_aabb.copy().expand(font_draw_expand), graphic::colors::white * .68f, cached_indicator_.texture},
					scaled_advance);
			}

			line_buffer_.append(current_block, major_p);
		} else {
			line_buffer_.append(current_block, major_p);
		}

		current_block.clear();
		return true;
	}

	hb_font_t* get_hb_font_(font_face_handle* face) noexcept{
		if(auto ptr = hb_cache_.get(face)) return ptr->get();
		auto new_hb_font = create_harfbuzz_font(*face);
		hb_ft_font_set_load_flags(new_hb_font.get(), FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING);
		const auto rst = new_hb_font.get();
		hb_cache_.put(face, std::move(new_hb_font));
		return rst;
	}

	math::vec2 move_pen(math::vec2 pen, math::vec2 advance) const noexcept{
		if(target_hb_dir == HB_DIRECTION_LTR){
			pen.x += advance.x;
			pen.y -= advance.y;
		} else if(target_hb_dir == HB_DIRECTION_RTL){
			pen.x -= advance.x;
			pen.y -= advance.y;
		} else if(target_hb_dir == HB_DIRECTION_TTB){
			pen.x += advance.x;
			pen.y -= advance.y;
		} else if(target_hb_dir == HB_DIRECTION_BTT){
			pen.x += advance.x;
			pen.y += advance.y;
		}
		return pen;
	}

	bool process_text_run(
		const type_setting::tokenized_text& full_text, glyph_layout& results,
		std::size_t start, std::size_t length, font_face_handle* face
		){
		hb_buffer_clear_contents(hb_buffer_.get());
		hb_buffer_add_utf32(hb_buffer_.get(), reinterpret_cast<const std::uint32_t*>(full_text.get_text().data()),
			static_cast<int>(full_text.get_text().size()), static_cast<unsigned int>(start), static_cast<int>(length));
		hb_buffer_set_direction(hb_buffer_.get(), target_hb_dir);
		// hb_buffer_set_language(hb_buffer_.get(), hb_language_from_string("en", -1));

		hb_buffer_guess_segment_properties(hb_buffer_.get());


		const auto req_size_vec = rich_text_context_.get_size(config.font_size.as<float>());
		const auto snapped_size = get_snapped_size_vec(req_size_vec);
		auto rich_offset = rich_text_context_.get_offset();
		(void)face->set_size(snapped_size);
		math::vec2 run_scale_factor = req_size_vec / snapped_size.as<float>();

		auto* raw_hb_font = get_hb_font_(face);


		hb_shape(raw_hb_font, hb_buffer_.get(), feature_stack_.data(), feature_stack_.size());
		unsigned int len;
		hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(hb_buffer_.get(), &len);
		hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(hb_buffer_.get(), &len);

		long long last_applied_pos = static_cast<long long>(start) - 1;

        // [Added] Fetch underline metrics from FT_Face
        // Note: FT_Face underline_position is often negative for below baseline.
        // We normalize and scale it.
        // Thickness and position are in font units if we access raw face, but FT_Face handle has scaled metrics if set_size is used?
        // Actually, FT_FaceRec (handle->handle) has underline_position and underline_thickness.
        // These are usually in font units, we need to scale them.
        // Since we are using FreeType, let's rely on the fact that `run_scale_factor` * `normalize_len`
        // converts font units to our logical pixel space.

        auto ft_face = face->get(); // Access raw FT_Face
		float ul_position;
		float ul_thickness;

		// FT_Face metrics are usually in Font Units (integers), not 26.6 fixed point.
		// We need to scale them: (Value / EM) * Size
		if (ft_face->units_per_EM != 0) {
			float em_scale = req_size_vec.y / static_cast<float>(ft_face->units_per_EM);
			ul_position = static_cast<float>(ft_face->underline_position) * em_scale;
			ul_thickness = static_cast<float>(ft_face->underline_thickness) * em_scale;
		} else {
			// Fallback for non-scalable fonts or missing EM info
			ul_position = 0.0f;
			ul_thickness = 0.0f;
		}

		// Apply defaults if metrics are missing or invalid
		if (ul_thickness <= 0.0f) {
			ul_thickness = std::max(1.0f, req_size_vec.y / 14.0f);
		} else {
			// Ensure strictly positive and minimal visibility
			ul_thickness = std::max(ul_thickness, 1.0f);
		}

		if (ul_position == 0.0f) {
			// Default position: roughly 10-15% of height below baseline
			// Note: FT underline_position is usually negative (downwards in Cartesian, but handled as offset)
			ul_position = -req_size_vec.y / 10.0f;
		}

        // Helper to commit an underline segment
        auto commit_underline = [&](math::vec2 start_pos, math::vec2 end_pos, graphic::color color) {

             math::vec2 offset_vec{};
             offset_vec.y = -ul_position;

             underline ul;
             ul.start = start_pos + offset_vec;
             ul.end = end_pos + offset_vec;
             ul.thickness = ul_thickness;
             ul.color = color;
             current_block.push_back_underline(ul);
        };

        std::optional<math::vec2> active_ul_start;

		for(unsigned int i = 0; i < len; ++i){
			const glyph_index_t gid = infos[i].codepoint;
			const auto current_cluster = infos[i].cluster;

            // [Modified] Check token updates (soft attributes including underline toggle)
			if (static_cast<long long>(current_cluster) > last_applied_pos && current_cluster < start + length) {
				// Check state before update
				bool was_ul = rich_text_context_.is_underline_enabled();
				graphic::color prev_color = rich_text_context_.get_color();

				for (auto p = last_applied_pos + 1; p <= current_cluster; ++p) {
					auto tokens = full_text.get_token_group(static_cast<std::size_t>(p), token_soft_last_iterator);
					// [Fix 1] Always update context, even if tokens is empty, to reset styles
					if (!tokens.empty()) {
						rich_text_context_.update(manager, config.font_size.as<float>(), tokens, type_setting::context_update_mode::soft_only);
						token_soft_last_iterator = tokens.end();
					}
				}
				rich_offset = rich_text_context_.get_offset();

				last_applied_pos = current_cluster;

                // Check state after update
                bool is_ul = rich_text_context_.is_underline_enabled();

                // Handle toggle OFF
                if (was_ul && !is_ul && active_ul_start.has_value()) {
                     commit_underline(*active_ul_start, current_block.cursor, prev_color);
                     active_ul_start.reset();
                }
                // Handle toggle ON (will be handled by "if is_ul && !active" check below)
			}

            // [Added] Start underline if enabled and not started
            if (rich_text_context_.is_underline_enabled() && !active_ul_start.has_value()) {
                active_ul_start = current_block.cursor;
            }

			const auto ch = full_text.get_text()[infos[i].cluster];
			if(bool is_delimiter = (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')){
				// If we are breaking the block (flush), we must close the underline segment
				if (active_ul_start.has_value()) {
					commit_underline(*active_ul_start, current_block.cursor, rich_text_context_.get_color());
					active_ul_start.reset();
				}

				if(!flush_block(results)) return false;

				// [Fix 2] Do NOT restart underline immediately if it is a newline
				// If underline is still enabled, restart it at the new cursor
				if (ch != '\n' && ch != '\r' && rich_text_context_.is_underline_enabled()) {
					active_ul_start = current_block.cursor;
				}
			}

			glyph_identity id{gid, snapped_size};
			const auto& glyph_metrics_ref = [&]() -> const glyph_metrics&{
				return manager.get_glyph_exact(*face, id).metrics();
			}();

			float asc, desc;
			if(is_vertical_()){
				float width = glyph_metrics_ref.advance.x * run_scale_factor.x;
				asc = width / 2.f;
				desc = width / 2.f;
			} else{
				asc = glyph_metrics_ref.ascender() * run_scale_factor.y;
				desc = glyph_metrics_ref.descender() * run_scale_factor.y;
			}

			if(ch != '\n' && ch != '\r'){
				current_block.block_ascender = std::max(current_block.block_ascender, asc);
				current_block.block_descender = std::max(current_block.block_descender, desc);
			}

			switch(ch){
			case '\r' :
				if(config.line_feed_type == linefeed::CRLF){ }
				continue;
			case '\n' :
				// [Fix 3] Simplified newline handling
				// flush_block was already called in is_delimiter check above
				// Just advance line and ensure underline is reset
				advance_line(results);
				active_ul_start.reset();
				continue;
			case '\t' :{
				float tab_step = config.tab_scale * get_scaled_default_space_width();
				if(tab_step > std::numeric_limits<float>::epsilon()){
					float current_pos = current_block.cursor.*major_p;
					float next_tab = (std::floor(current_pos / tab_step) + 1) * tab_step;
					current_block.cursor.*major_p = next_tab;
				}
				continue;
			}
			case ' ' :{
				const auto sp = math::vec2{
					normalize_hb_pos(pos[i].x_advance), normalize_hb_pos(pos[i].y_advance)
				} * run_scale_factor;
				current_block.cursor = move_pen(current_block.cursor, sp);
			}
				continue;
			default : break;
			}

			glyph g = manager.get_glyph_exact(*face, id);
			const auto advance = math::vec2{
				normalize_hb_pos(pos[i].x_advance), normalize_hb_pos(pos[i].y_advance)
			} * run_scale_factor;
			const float x_offset = normalize_hb_pos(pos[i].x_offset) * run_scale_factor.x;
			const float y_offset = normalize_hb_pos(pos[i].y_offset) * run_scale_factor.y;

			math::vec2 glyph_local_draw_pos{x_offset, -y_offset};
			glyph_local_draw_pos += rich_offset;
			if(is_reversed_()) {
				current_block.cursor = move_pen(current_block.cursor, advance);
			}

			const math::frect actual_aabb = g.metrics().place_to(
				current_block.cursor + glyph_local_draw_pos, run_scale_factor);
			const math::frect draw_aabb = actual_aabb.copy().expand(font_draw_expand * run_scale_factor);

			current_block.push_back(actual_aabb, {draw_aabb, rich_text_context_.get_color(), std::move(g)});

			if(!is_reversed_()){
				current_block.cursor = move_pen(current_block.cursor, advance);
			}
		}

        // [Added] Handle pending underline at end of Run
        if (active_ul_start.has_value()) {
            commit_underline(*active_ul_start, current_block.cursor, rich_text_context_.get_color());
        }

		if (last_applied_pos < static_cast<long long>(start + length - 1)) {
			for (auto p = last_applied_pos + 1; p < start + length; ++p) {
				auto tokens = full_text.get_token_group(static_cast<std::size_t>(p), full_text.get_init_token());
				if (!tokens.empty()) {
					rich_text_context_.update(manager, config.font_size.as<float>(), tokens, type_setting::context_update_mode::soft_only);
					token_soft_last_iterator = tokens.end();
				}
			}
		}

		return true;
	}

	void process_layout(const type_setting::tokenized_text& full_text, glyph_layout& results){
		std::size_t current_idx = 0;
		std::size_t run_start = 0;
		font_face_handle* current_face = nullptr;
		auto resolve_face = [&](font_face_handle* current, char32_t codepoint) -> font_face_handle*{
			if(current && current->index_of(codepoint)) return current;
			const auto* family = &rich_text_context_.get_font(manager.get_default_family());
			auto face_view = manager.use_family(family);
			auto [best_face, _] = face_view.find_glyph_of(codepoint);
			return best_face;
		};
		while(current_idx < full_text.get_text().size()){
			auto tokens = full_text.get_token_group(current_idx, token_hard_last_iterator);
			token_hard_last_iterator = tokens.end();

			const bool need_another_run = type_setting::check_token_group_need_another_run(tokens);
			if(need_another_run){
				if(current_face && current_idx > run_start){
					process_text_run(full_text, results, run_start, current_idx - run_start, current_face);
				}
				rich_text_context_.update(
					manager, config.font_size.as<float>(),
					tokens, type_setting::context_update_mode::hard_only,
					[&](hb_feature_t f){
						f.start = static_cast<unsigned int>(current_idx);
						f.end = HB_FEATURE_GLOBAL_END;
						feature_stack_.push_back(f);
					}, [&](unsigned to_close){
						for(auto it = feature_stack_.rbegin(); it != feature_stack_.rend() && to_close > 0; ++it){
							if(it->end == HB_FEATURE_GLOBAL_END){
								it->end = static_cast<unsigned int>(current_idx);
								to_close--;
							}
						}
					}
				);


				run_start = current_idx;
				current_face = nullptr;

				flush_block(results);
			}else{
			}

			const auto codepoint = full_text.get_text()[current_idx];
			font_face_handle* best_face = resolve_face(current_face, codepoint);

			if(current_face == nullptr){
				current_face = best_face;
			} else if(best_face != current_face){
				process_text_run(full_text, results, run_start, current_idx - run_start, current_face);
				current_face = best_face;
				run_start = current_idx;
			}
			current_idx++;
		}
		if(current_face != nullptr) process_text_run(full_text, results, run_start, full_text.get_text().size() - run_start, current_face);

		flush_block(results);
		advance_line(results);
	}

	void finalize(glyph_layout& results){
		if(results.elems.empty()){
			results.extent = {0, 0};
			return;
		}

		auto extent = max_bound - min_bound;
		results.extent = extent;

		for(auto& elem : results.elems){
			elem.aabb.move(-min_bound);
		}
        // [Added] Adjust final underline positions
        for(auto& ul : results.underlines){
            ul.start -= min_bound;
            ul.end -= min_bound;
        }
	}

	[[nodiscard]] float get_current_relative_scale() const noexcept{
		const auto current_size = rich_text_context_.get_size(config.font_size.as<float>());
		return current_size.y / static_cast<float>(config.font_size.y);
	}

	[[nodiscard]] float get_scaled_default_line_thickness() const noexcept{
		return default_line_thickness * get_current_relative_scale();
	}

	[[nodiscard]] float get_scaled_default_space_width() const noexcept{
		return default_space_width * get_current_relative_scale();
	}
};

export glyph_layout layout_text(
	font_manager& manager,
	const font_family& font_group,
	const type_setting::tokenized_text& text,
	const layout_config& config
){
	glyph_layout empty_result{};
	if(text.get_text().empty()) return empty_result;
	auto view = font_face_view{manager.use_family(&font_group)};
	if(std::ranges::empty(view)) return empty_result;
	layout_context ctx(manager, view, config, text);
	return std::move(ctx).crop(text);
}
}