module;

#include <hb.h>
#include <hb-ft.h>
#include <freetype/freetype.h>
#include <cassert>

export module mo_yanxi.hb.typesetting;

import std;
import mo_yanxi.font;
import mo_yanxi.typesetting;
import mo_yanxi.typesetting.rich_text;
import mo_yanxi.math;
import mo_yanxi.font.manager;
import mo_yanxi.hb.wrap;
import mo_yanxi.graphic.image_region;
import mo_yanxi.cache;

namespace mo_yanxi::font::hb{

export enum struct layout_direction{ deduced, ltr, rtl, ttb, btt };
export enum struct linefeed{ LF, CRLF };
export enum struct content_alignment{ start, center, end, justify };

export struct layout_result{
	math::frect aabb;
	glyph_borrow texture;
};

export struct glyph_layout{
	std::vector<layout_result> elems;
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

    // Block 内部的绘图光标位置（相对于 pos_min/起始点）
    // 合并了原有的 pen 和 total_advance 功能
	math::vec2 cursor{};

	math::vec2 pos_min{math::vectors::constant2<float>::inf_positive_vec2};
	math::vec2 pos_max{-math::vectors::constant2<float>::inf_positive_vec2};

	// Block 内部的 Metrics
	float block_ascender = 0.f;
	float block_descender = 0.f;

	void clear(){
		glyphs.clear();
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

	void push_front(math::frect glyph_region, const layout_result& glyph, math::vec2 glyph_advance){
		for(auto&& layout_result : glyphs){
			layout_result.aabb.move(glyph_advance);
		}
		glyphs.insert(glyphs.begin(), glyph);
		pos_min += glyph_advance;
		pos_max += glyph_advance;
		pos_min.min(glyph_region.vert_00());
		pos_max.max(glyph_region.vert_11());
		cursor += glyph_advance;
	}

	math::frect get_local_draw_bound(){
		if(glyphs.empty()){ return {}; }
		return {tags::unchecked, tags::from_vertex, pos_min, pos_max};
	}
};

// --- 内部：排版上下文封装 ---
struct layout_context{
	// 行缓冲：存储当前行待提交的数据，用于在整行确定后统一计算对齐和位置
	struct line_buffer_t{
		std::vector<layout_result> elems;
		float width = 0.f; // 当前行在主轴上的累积宽度
		float max_ascender = 0.f;
		float max_descender = 0.f;

		void clear(){
			elems.clear();
			width = 0.f;
			max_ascender = 0.f;
			max_descender = 0.f;
		}

		void append(layout_block& block, float math::vec2::* major_axis){
			// 将 block 的相对位置修正为相对于行首的位置
			math::vec2 move_vec{};
			move_vec.*major_axis = width;

			elems.reserve(elems.size() + block.glyphs.size());
			for(auto& g : block.glyphs){
				g.aabb.move(move_vec); // 此时 aabb 是相对于 (行首, 0) 的
				elems.push_back(std::move(g));
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
	const type_setting::tokenized_text* full_text;
	lru_cache<font_face_handle*, font_ptr, 4> hb_cache_;
	hb::buffer_ptr hb_buffer_;

	glyph_layout results;

	// 全局包围盒记录
	math::vec2 min_bound{math::vectors::constant2<float>::inf_positive_vec2};
	math::vec2 max_bound{-math::vectors::constant2<float>::inf_positive_vec2};

	// 动态行距相关状态
	float default_line_thickness = 0.f;
	float default_space_width = 0.f;

	// 记录上一行的下延，用于计算与当前行上延的距离
	float prev_line_descender_ = 0.f;
	// 当前行的基线在副轴（minor axis）上的绝对坐标
	float current_baseline_pos_ = 0.f;

	bool is_first_line_ = true;

	hb_direction_t target_hb_dir = HB_DIRECTION_LTR;

	float math::vec2::* major_p = &math::vec2::x;
	float math::vec2::* minor_p = &math::vec2::y;

	line_buffer_t line_buffer_;

	layout_block current_block{};
	indicator_cache cached_indicator_;

	type_setting::rich_text_context rich_text_context_;
	type_setting::tokenized_text::token_iterator last_iterator;

public:
	layout_context(
		font_manager& m, font_face_view& v,
		const layout_config& c, const type_setting::tokenized_text& t
	) : manager(m), base_view(v), config(c), full_text(&t), last_iterator(t.get_init_token()){
		hb_buffer_ = hb::make_buffer();
		results.elems.reserve(full_text->get_text().size());

		// 1. 确定书写方向
		if(config.direction != layout_direction::deduced){
			switch(config.direction){
			case layout_direction::ltr : target_hb_dir = HB_DIRECTION_LTR; break;
			case layout_direction::rtl : target_hb_dir = HB_DIRECTION_RTL; break;
			case layout_direction::ttb : target_hb_dir = HB_DIRECTION_TTB; break;
			case layout_direction::btt : target_hb_dir = HB_DIRECTION_BTT; break;
			default : break;
			}
		} else{
			hb_buffer_add_utf32(hb_buffer_.get(), reinterpret_cast<const std::uint32_t*>(full_text->get_text().data()),
				static_cast<int>(full_text->get_text().size()), 0, -1);
			hb_buffer_guess_segment_properties(hb_buffer_.get());
			target_hb_dir = hb_buffer_get_direction(hb_buffer_.get());
			if(target_hb_dir == HB_DIRECTION_INVALID) target_hb_dir = HB_DIRECTION_LTR;
		}

		// 2. 初始化度量
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

		// 初始化 Wrap Indicator
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

	// 提交当前行到全局结果，计算基线位置并应用对齐
	void advance_line() noexcept{
		// 如果行是空的，处理空行高度
		bool is_empty_line = line_buffer_.elems.empty();
		float current_asc = is_empty_line ? default_line_thickness / 2.f : line_buffer_.max_ascender;
		float current_desc = is_empty_line ? default_line_thickness / 2.f : line_buffer_.max_descender;

		// 1. 计算基线位置 (Minor Axis)
		if(is_first_line_){
			// 第一行：通常让 Ascender 顶住容器顶部
			// 假设 Y 向下，则 Baseline 设为 current_asc 使得顶部对齐 0
			current_baseline_pos_ = current_asc;
			is_first_line_ = false;
		} else{
			// 后续行：Gap = (PrevDesc + CurrAsc) * Scale + Fixed
			float metrics_sum = prev_line_descender_ + current_asc;
			float step = metrics_sum * config.line_spacing_scale + config.line_spacing_fixed_distance;
			current_baseline_pos_ += step;
		}

		// 2. 计算对齐偏移 (Major Axis)
		float align_offset = 0.f;
		const float content_width = line_buffer_.width;
		// 只有当设置了 max_extent 时，对齐才有意义；否则视为无限宽，默认 start
		if(!config.max_extent.is_Inf()){
			const float container_width = config.max_extent.*major_p;
			float remaining = container_width - content_width;
			if(remaining > 0.001f){
				switch(config.align){
				case content_alignment::center : align_offset = remaining / 2.0f;
					break;
				case content_alignment::end : align_offset = remaining;
					break;
				// Justify 暂未实现完整逻辑，此处降级为左对齐
				case content_alignment::justify :
					break;
				default : break;
				}
			}
		}

		// 3. 将 Buffer 中的元素应用最终位置并刷入 Global Results
		math::vec2 alignment_vec{};
		alignment_vec.*major_p = align_offset;

		math::vec2 baseline_vec{};
		baseline_vec.*minor_p = current_baseline_pos_;

		for(auto& elem : line_buffer_.elems){
			// 原始 elem.aabb 是相对于 (行首, 0) 的，现在应用 (align_offset, current_baseline_pos_)
			elem.aabb.move(alignment_vec + baseline_vec);

			results.elems.push_back(std::move(elem));

			// 更新全局包围盒
			min_bound.min(elem.aabb.vert_00());
			max_bound.max(elem.aabb.vert_11());
		}

		// 4. 更新状态以备下一行
		prev_line_descender_ = current_desc;
		line_buffer_.clear();
	}

	bool flush_block(){
		if(current_block.glyphs.empty()) return true;

		// 临时将 Block 加入 Buffer 计算预期宽度，判断是否溢出
		const float current_line_width = line_buffer_.width;
		const float block_width = current_block.cursor.*major_p;
		const float max_width = config.max_extent.*major_p;

		// 检查主轴溢出 (如果定义了最大宽度)
		bool overflow = false;
		if (!config.max_extent.is_Inf()) {
			// 简单的溢出判断：如果当前行非空 且 (当前宽 + Block宽 > 最大宽)
			if (current_line_width > 0.001f && (current_line_width + block_width > max_width)) {
				overflow = true;
			}
		}

		if (overflow) {
			// 1. 提交当前行 (Buffer 中的内容)
			advance_line();

			// 2. 处理 Wrap Indicator
			if(config.has_wrap_indicator()){
				const auto& ind = cached_indicator_;
				// 将 Indicator 加入到 Block 的头部
				current_block.push_front(
					ind.glyph_aabb,
					{ind.glyph_aabb.copy().expand(font_draw_expand), cached_indicator_.texture},
					ind.advance);
			}

			// 3. 将 Block 加入新的行 Buffer (现在是空的)
			line_buffer_.append(current_block, major_p);
		} else {
			// 未溢出，直接追加到 Buffer
			line_buffer_.append(current_block, major_p);
		}

		// 关键：Block 处理完毕后，必须重置 Block 状态（包括 cursor）以防影响后续排版
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

	bool process_text_run(std::size_t start, std::size_t length, font_face_handle* face){
		hb_buffer_clear_contents(hb_buffer_.get());
		hb_buffer_add_utf32(hb_buffer_.get(), reinterpret_cast<const std::uint32_t*>(full_text->get_text().data()),
			static_cast<int>(full_text->get_text().size()), static_cast<unsigned int>(start), static_cast<int>(length));
		hb_buffer_set_direction(hb_buffer_.get(), target_hb_dir);
		hb_buffer_guess_segment_properties(hb_buffer_.get());

		const auto req_size_vec = rich_text_context_.get_size(config.font_size.as<float>()).round<
			glyph_size_type::value_type>();
		const auto snapped_size = get_snapped_size_vec(req_size_vec);
		const auto rich_offset = rich_text_context_.get_offset();
		face->set_size(snapped_size);
		math::vec2 run_scale_factor = req_size_vec.as<float>() / snapped_size.as<float>();

		auto* raw_hb_font = get_hb_font_(face);
		hb_shape(raw_hb_font, hb_buffer_.get(), nullptr, 0);
		unsigned int len;
		hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(hb_buffer_.get(), &len);
		hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(hb_buffer_.get(), &len);

		for(unsigned int i = 0; i < len; ++i){
			const glyph_index_t gid = infos[i].codepoint;
			const auto ch = full_text->get_text()[infos[i].cluster];

			if(bool is_delimiter = (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')){
				if(!flush_block()) return false;
			}

			// 获取 Metrics
			glyph_identity id{gid, snapped_size};
			const auto& glyph_metrics_ref = [&]() -> const glyph_metrics&{
				return manager.get_glyph_exact(*face, id).metrics();
			}();

			float asc, desc;
			if(is_vertical_()){
				float width = glyph_metrics_ref.size.x * run_scale_factor.x;
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
				if(config.line_feed_type == linefeed::CRLF){ /* ... */ }
				continue;
			case '\n' :
				if(!flush_block()) return false;
				advance_line();
				continue;
			case '\t' :{
				float tab_step = config.tab_scale * default_space_width;
				if(tab_step > std::numeric_limits<float>::epsilon()){
					// Tab 基于 cursor (即 Block 内的绝对位置) 计算
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

			// 字形放置
			glyph g = manager.get_glyph_exact(*face, id);
			const auto advance = math::vec2{
				normalize_hb_pos(pos[i].x_advance), normalize_hb_pos(pos[i].y_advance)
			} * run_scale_factor;
			const float x_offset = normalize_hb_pos(pos[i].x_offset) * run_scale_factor.x;
			const float y_offset = normalize_hb_pos(pos[i].y_offset) * run_scale_factor.y;

			math::vec2 glyph_local_draw_pos{x_offset, -y_offset};
			glyph_local_draw_pos += rich_offset;

			// RTL 处理：先移动光标，再绘制
			if(is_reversed_()) {
				current_block.cursor = move_pen(current_block.cursor, advance);
			}

			// 计算 AABB：直接使用 cursor
			const math::frect actual_aabb = g.metrics().place_to(
				current_block.cursor + glyph_local_draw_pos, run_scale_factor);
			const math::frect draw_aabb = actual_aabb.copy().expand(font_draw_expand * run_scale_factor);

			current_block.push_back(actual_aabb, {draw_aabb, std::move(g)});

			// LTR 处理：绘制后移动光标
			if(!is_reversed_()){
				current_block.cursor = move_pen(current_block.cursor, advance);
			}
		}
		return true;
	}

	void process_layout(){
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

		while(current_idx < full_text->get_text().size()){
			auto tokens = full_text->get_token_group(current_idx, last_iterator);
			if(!tokens.empty()){
				if(current_face && current_idx > run_start) process_text_run(run_start, current_idx - run_start,
					current_face);
				rich_text_context_.update(manager, config.font_size.as<float>(), tokens);
				last_iterator = tokens.end();
				run_start = current_idx;
				current_face = nullptr;

				// 属性变化可能导致 metrics 变更，强制 flush
				flush_block();
			}

			const auto codepoint = full_text->get_text()[current_idx];
			font_face_handle* best_face = resolve_face(current_face, codepoint);
			if(current_face == nullptr){
				current_face = best_face;
			} else if(best_face != current_face){
				process_text_run(run_start, current_idx - run_start, current_face);
				current_face = best_face;
				run_start = current_idx;
			}
			current_idx++;
		}
		if(current_face != nullptr) process_text_run(run_start, full_text->get_text().size() - run_start, current_face);

		flush_block();
		advance_line(); // 确保最后一行被提交
	}

	// Finalize 只需计算最终包围盒，所有位置偏移已在 advance_line 中应用
	void finalize(){
		if(results.elems.empty()){
			results.extent = {0, 0};
			return;
		}

		// 归一化位置 (将 min_bound 移到 0,0)
		auto extent = max_bound - min_bound;
		results.extent = extent;

		for(auto& elem : results.elems){
			elem.aabb.move(-min_bound);
		}
	}

	[[nodiscard]] glyph_layout&& crop() &&{ return std::move(results); }
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
	ctx.process_layout();
	ctx.finalize();
	return std::move(ctx).crop();
}
}