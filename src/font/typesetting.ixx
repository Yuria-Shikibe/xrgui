module;

#include <hb.h>
#include <hb-ft.h>
#include <freetype/freetype.h>

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.typesetting;

import std;
import mo_yanxi.font;
import mo_yanxi.font.manager;
import mo_yanxi.typesetting.util;
import mo_yanxi.typesetting.rich_text;
import mo_yanxi.hb.wrap;
import mo_yanxi.math;
export import mo_yanxi.graphic.image_region;
export import mo_yanxi.graphic.color;
export import mo_yanxi.math.vector2;

import mo_yanxi.cache;

namespace mo_yanxi::typesetting{
export enum struct layout_direction{ ltr, rtl, ttb, btt, deduced };

export enum struct linefeed{ LF, CRLF };

export enum struct content_alignment{ start, center, end, justify };

export struct glyph_elem{
	math::frect aabb;
	graphic::color color;
	font::glyph_borrow texture;
};

export struct underline{
	math::vec2 start;
	math::vec2 end;
	float thickness;
	graphic::color color;
};

export struct glyph_layout{
	std::vector<glyph_elem> elems;
	std::vector<underline> underlines;
	math::vec2 extent;
};

font::hb::font_ptr create_harfbuzz_font(const font::font_face_handle& face){
	hb_font_t* font = hb_ft_font_create_referenced(face);
	return font::hb::font_ptr{font};
}


constexpr bool IsSeparator(char32_t c) noexcept {
	//TODO use detailed check when ICU is valid

	switch (c) {
		// --- 常见 ASCII 标点 (保持原样) ---
	case U',':
	case U'.':
	case U';':
	case U':':
	case U'!':
	case U'?':

		// --- 常见 中文/全角 标点 (使用 Unicode 转义) ---

		// 句读符号
	case U'\u3002': // '。' Ideographic Full Stop
	case U'\uFF0C': // '，' Fullwidth Comma
	case U'\u3001': // '、' Ideographic Comma (顿号)

		// 分隔/语气符号
	case U'\uFF1B': // '；' Fullwidth Semicolon
	case U'\uFF1A': // '：' Fullwidth Colon
	case U'\uFF01': // '！' Fullwidth Exclamation Mark
	case U'\uFF1F': // '？' Fullwidth Question Mark

		// 特殊符号
	case U'\u2026': // '…' Horizontal Ellipsis (省略号)

		return true;

	default:
		return false;
	}
}

export struct layout_config{
	layout_direction direction;
	math::vec2 max_extent = math::vectors::constant2<float>::inf_positive_vec2;

	math::optional_vec2<float> default_font_size{math::nullopt_vec2<float>};
	linefeed line_feed_type;
	content_alignment align = content_alignment::start;
	float tab_scale = 4.f;
	float line_spacing_scale = 1.5f;
	float line_spacing_fixed_distance = 0.f;
	char32_t wrap_indicator_char = U'\u2925';

	math::optional_vec2<float> screen_ppi{math::nullopt_vec2<float>};

	math::vec2 get_default_font_size() const noexcept{
		return default_font_size.value_or(glyph_size::get_glyph_std_size_at(glyph_size::standard_size, get_screen_ppi()));
	}

	math::vec2 get_screen_ppi() const noexcept{
		return screen_ppi.value_or(glyph_size::screen_ppi);
	}

	constexpr bool has_wrap_indicator() const noexcept{ return wrap_indicator_char; }

	constexpr bool operator==(const layout_config&) const noexcept = default;
};

struct layout_block{
	std::vector<glyph_elem> glyphs;
	std::vector<underline> underlines;

	math::vec2 cursor{};

	math::vec2 pos_min{math::vectors::constant2<float>::inf_positive_vec2};
	math::vec2 pos_max{-math::vectors::constant2<float>::inf_positive_vec2};

	float block_ascender = 0.f;
	float block_descender = 0.f;

	FORCE_INLINE inline void clear(){
		glyphs.clear();
		underlines.clear();
		cursor = {};
		pos_min = math::vectors::constant2<float>::inf_positive_vec2;
		pos_max = -math::vectors::constant2<float>::inf_positive_vec2;
		block_ascender = 0.f;
		block_descender = 0.f;
	}

	FORCE_INLINE inline void push_back(math::frect glyph_region, const glyph_elem& glyph){
		glyphs.push_back(glyph);
		pos_min.min(glyph_region.vert_00());
		pos_max.max(glyph_region.vert_11());
	}

	FORCE_INLINE inline void push_back_underline(const underline& ul){
		underlines.push_back(ul);
		math::vec2 ul_min = math::min(ul.start, ul.end);
		math::vec2 ul_max = math::max(ul.start, ul.end);
		const math::vec2 diff = ul.end - ul.start;
		if(std::abs(diff.x) > std::abs(diff.y)){
			ul_min.y -= ul.thickness / 2.f;
			ul_max.y += ul.thickness / 2.f;
		} else{
			ul_min.x -= ul.thickness / 2.f;
			ul_max.x += ul.thickness / 2.f;
		}

		pos_min.min(ul_min);
		pos_max.max(ul_max);
	}

	FORCE_INLINE inline void push_front(math::frect glyph_region, const glyph_elem& glyph, math::vec2 glyph_advance){
		for(auto&& layout_result : glyphs){
			layout_result.aabb.move(glyph_advance);
		}
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

	FORCE_INLINE inline math::frect get_local_draw_bound(){
		if(glyphs.empty() && underlines.empty()){ return {}; }
		return {tags::unchecked, tags::from_vertex, pos_min, pos_max};
	}
};

export
struct layout_context{
private:
	struct line_buffer_t{
		std::vector<glyph_elem> elems;
		std::vector<underline> underlines;
		float width = 0.f;
		float max_ascender = 0.f;
		float max_descender = 0.f;

		// [Added] 追踪当前行的局部包围盒 (相对于行起始点)
		math::vec2 pos_min{math::vectors::constant2<float>::inf_positive_vec2};
		math::vec2 pos_max{-math::vectors::constant2<float>::inf_positive_vec2};

		FORCE_INLINE inline void clear(){
			elems.clear();
			underlines.clear();
			width = 0.f;
			max_ascender = 0.f;
			max_descender = 0.f;
			// [Added] 重置包围盒
			pos_min = math::vectors::constant2<float>::inf_positive_vec2;
			pos_max = -math::vectors::constant2<float>::inf_positive_vec2;
		}

		FORCE_INLINE inline void append(layout_block& block, float math::vec2::* major_axis){
			math::vec2 move_vec{};
			move_vec.*major_axis = width;
			elems.reserve(elems.size() + block.glyphs.size());

			// [Modified] 在添加元素时同时更新行缓存的包围盒
			if (!block.glyphs.empty() || !block.underlines.empty()) {
				// 计算 block 移动后的包围盒
				math::vec2 block_min = block.pos_min + move_vec;
				math::vec2 block_max = block.pos_max + move_vec;

				pos_min.min(block_min);
				pos_max.max(block_max);
			}

			for(auto& g : block.glyphs){
				g.aabb.move(move_vec);
				elems.push_back(std::move(g));
			}

			underlines.reserve(underlines.size() + block.underlines.size());
			for(auto& ul : block.underlines){
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
		font::glyph_borrow texture;
		math::frect glyph_aabb;
		math::vec2 advance;
	};

	struct layout_state{
		math::vec2 min_bound{math::vectors::constant2<float>::inf_positive_vec2};
		math::vec2 max_bound{-math::vectors::constant2<float>::inf_positive_vec2};
		math::vec2 default_font_size{};

		float default_line_thickness = 0.f;
		float default_space_width = 0.f;
		float prev_line_descender_ = 0.f;
		float current_baseline_pos_ = 0.f;
		bool is_first_line_ = true;

		hb_direction_t target_hb_dir = HB_DIRECTION_INVALID;
		float math::vec2::* major_p = &math::vec2::x;
		float math::vec2::* minor_p = &math::vec2::y;

		line_buffer_t line_buffer_{};
		layout_block current_block{};
		rich_text_context rich_text_context_;
		tokenized_text::token_iterator token_hard_last_iterator;
		tokenized_text::token_iterator token_soft_last_iterator;

		void reset(const tokenized_text& t){
			min_bound = math::vectors::constant2<float>::inf_positive_vec2;
			max_bound = -math::vectors::constant2<float>::inf_positive_vec2;
			default_font_size = {};

			prev_line_descender_ = 0.f;
			current_baseline_pos_ = 0.f;
			is_first_line_ = true;

			line_buffer_.clear();
			current_block.clear();

			rich_text_context_.clear();
			token_hard_last_iterator = t.get_init_token();
			token_soft_last_iterator = t.get_init_token();
		}
	};

	font::font_manager* manager_{font::default_font_manager};
	lru_cache<font::font_face_handle*, font::hb::font_ptr, 4> hb_cache_;
	font::hb::buffer_ptr hb_buffer_;
	std::vector<hb_feature_t> feature_stack_;

	layout_config config;

	layout_state state_;
	indicator_cache cached_indicator_{};

public:
	layout_context() = default;

	explicit layout_context(std::in_place_t){
		hb_buffer_ = font::hb::make_buffer();
	}

	explicit layout_context(
		const layout_config& c, font::font_manager* m = font::default_font_manager
	) : manager_(m), config(c){
		assert(manager_ != nullptr);
		hb_buffer_ = font::hb::make_buffer();
	}

	[[nodiscard]] glyph_layout layout(const tokenized_text& full_text, font::font_face_view base_view = {}){
		initialize_state(full_text, base_view);
		glyph_layout results{};
		results.elems.reserve(full_text.get_text().size());

		if(process_layout(full_text, results)){

		}
		finalize(results);

		return results;
	}

	void layout(glyph_layout& layout, const tokenized_text& full_text, font::font_face_view base_view = {}){
		initialize_state(full_text, base_view);

		layout.elems.clear();
		layout.underlines.clear();
		layout.extent = {};

		layout.elems.reserve(full_text.get_text().size());

		if(process_layout(full_text, layout)){

		}
		finalize(layout);
	}

	bool set_max_extent(math::vec2 ext){
		if(config.max_extent == ext){
			return false;
		}
		config.max_extent = ext;
		return true;
	}

	void set_config(layout_config& c){
		config = c;
		//state update?
	}

	[[nodiscard]] const layout_config& get_config() const noexcept{
		return config;
	}

private:
	FORCE_INLINE inline bool is_extent_exceeded(math::vec2 candidate_min, math::vec2 candidate_max) const noexcept {
		if (config.max_extent.is_both_inf()) return false;

		math::vec2 size = candidate_max - candidate_min;

		// 检查主轴和副轴是否超出
		// 注意：如果有无限轴，则该轴不检查
		if (!std::isinf(config.max_extent.x) && size.x > config.max_extent.x + 0.001f) return true;
		if (!std::isinf(config.max_extent.y) && size.y > config.max_extent.y + 0.001f) return true;

		return false;
	}

	void initialize_state(
		const typesetting::tokenized_text& full_text, font::font_face_view base_view){
		base_view = base_view ? base_view : manager_->use_family(manager_->get_default_family());
		state_.reset(full_text);
		feature_stack_.clear();

		if(config.direction != layout_direction::deduced){
			switch(config.direction){
			case layout_direction::ltr : state_.target_hb_dir = HB_DIRECTION_LTR;
				break;
			case layout_direction::rtl : state_.target_hb_dir = HB_DIRECTION_RTL;
				break;
			case layout_direction::ttb : state_.target_hb_dir = HB_DIRECTION_TTB;
				break;
			case layout_direction::btt : state_.target_hb_dir = HB_DIRECTION_BTT;
				break;
			default : std::unreachable();
			}
		} else{
			hb_buffer_clear_contents(hb_buffer_.get());
			hb_buffer_add_utf32(hb_buffer_.get(), reinterpret_cast<const std::uint32_t*>(full_text.get_text().data()),
				static_cast<int>(full_text.get_text().size()), 0, -1);
			hb_buffer_guess_segment_properties(hb_buffer_.get());
			state_.target_hb_dir = hb_buffer_get_direction(hb_buffer_.get());
			if(state_.target_hb_dir == HB_DIRECTION_INVALID) state_.target_hb_dir = HB_DIRECTION_LTR;
		}

		if(is_vertical_()){
			state_.major_p = &math::vec2::y;
			state_.minor_p = &math::vec2::x;
		} else{
			state_.major_p = &math::vec2::x;
			state_.minor_p = &math::vec2::y;
		}

		state_.default_font_size = config.get_default_font_size();

		const auto snapped_base_size = get_snapped_size_vec(state_.default_font_size);
		auto& primary_face = base_view.face();
		(void)primary_face.set_size(snapped_base_size);
		FT_Load_Char(primary_face, FT_ULong{' '}, FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING);
		math::vec2 base_scale_factor = state_.default_font_size / snapped_base_size.as<float>();

		if(is_vertical_()){
			state_.default_line_thickness = font::normalize_len(primary_face->size->metrics.max_advance) * base_scale_factor.
				x;
			state_.default_space_width = state_.default_line_thickness;
		} else{
			state_.default_line_thickness = font::normalize_len(primary_face->size->metrics.height) * base_scale_factor.y;
			state_.default_space_width = font::normalize_len(primary_face->glyph->advance.x) * base_scale_factor.x;
		}

		if(config.has_wrap_indicator()){
			auto [face, index] = base_view.find_glyph_of(config.wrap_indicator_char);
			if(index){
				font::glyph_identity id{index, snapped_base_size};
				font::glyph g = manager_->get_glyph_exact(*face, id);
				const auto& m = g.metrics();
				const auto advance = m.advance * base_scale_factor;
				math::vec2 adv{};
				adv.*state_.major_p = advance.*state_.major_p * (is_vertical_() ? -1 : 1);
				math::frect local_aabb = m.place_to({}, base_scale_factor);
				cached_indicator_ = indicator_cache{std::move(g), local_aabb, adv};
			} else{
				const_cast<layout_config&>(config).wrap_indicator_char = 0;
			}
		}
	}


	// [Changed] Return bool: true if success, false if boundary exceeded
	bool advance_line(glyph_layout& results) noexcept{
		const bool is_empty_line = state_.line_buffer_.elems.empty();
		const float current_asc = is_empty_line
									  ? get_scaled_default_line_thickness() / 2.f
									  : state_.line_buffer_.max_ascender;
		const float current_desc = is_empty_line
									   ? get_scaled_default_line_thickness() / 2.f
									   : state_.line_buffer_.max_descender;

		// 1. 预测下一行的基线位置
		float next_baseline = state_.current_baseline_pos_;
		if(state_.is_first_line_){
			next_baseline = current_asc;
		} else{
			const float metrics_sum = state_.prev_line_descender_ + current_asc;
			const float step = metrics_sum * config.line_spacing_scale + config.line_spacing_fixed_distance;
			next_baseline += step;
		}

		// 2. [Fix] 主轴(宽度)检查：使用排版宽度(Advance)，忽略像素溢出
		if (!config.max_extent.is_any_inf()) {
			const float container_width = config.max_extent.*state_.major_p;
			const float content_width = state_.line_buffer_.width;

			// 如果仅仅是排版宽度超出（不含容差），则不允许提交
			// 注意：这里允许极小的浮点误差
			if (content_width > container_width + 0.001f) {
				return false;
			}
		}

		// 3. 计算对齐偏移 (保持原有逻辑)
		float align_offset = 0.f;
		const float content_width = state_.line_buffer_.width;

		if(!config.max_extent.is_any_inf()){
			const float container_width = config.max_extent.*state_.major_p;
			const float abs_content = std::abs(content_width);
			float remaining = container_width - abs_content;

			if(remaining > 0.001f){
				if (is_reversed_()) {
					switch(config.align){
						case content_alignment::start: align_offset = container_width; break;
						case content_alignment::center: align_offset = container_width - (remaining / 2.0f); break;
						case content_alignment::end: align_offset = abs_content; break;
						default: align_offset = container_width; break;
					}
				} else {
					switch(config.align){
						case content_alignment::center: align_offset = remaining / 2.0f; break;
						case content_alignment::end: align_offset = remaining; break;
						default: break;
					}
				}
			} else if (is_reversed_()) {
				align_offset = container_width;
			}
		} else {
			if (is_reversed_()) align_offset = std::abs(content_width);
		}

		math::vec2 offset_vec{};
		offset_vec.*state_.major_p = align_offset;
		offset_vec.*state_.minor_p = next_baseline;

		// 4. [Fix] 副轴(高度)检查：仍然基于 AABB，防止垂直溢出
		// 我们需要计算这一行放置后的垂直覆盖范围
		if (!state_.line_buffer_.elems.empty()) {
			// 计算当前行的垂直边界 (相对于 offset_vec 之前的局部坐标)
			// 注意：pos_min/max 包含了 X 和 Y。我们只关心 minor_p (Y轴)

			float line_min_y = state_.line_buffer_.pos_min.*state_.minor_p + offset_vec.*state_.minor_p;
			float line_max_y = state_.line_buffer_.pos_max.*state_.minor_p + offset_vec.*state_.minor_p;

			// 结合全局边界
			float global_min_y = std::min(state_.min_bound.*state_.minor_p, line_min_y);
			float global_max_y = std::max(state_.max_bound.*state_.minor_p, line_max_y);

			float total_height = global_max_y - global_min_y;
			float limit_height = config.max_extent.*state_.minor_p;

			if (!std::isinf(limit_height) && total_height > limit_height + 0.001f) {
				return false;
			}
		} else {
			// 空行检查
			if (!config.max_extent.is_any_inf()) {
				float max_v = config.max_extent.*state_.minor_p;
				if (next_baseline + current_desc > max_v) return false;
			}
		}

		// 5. 确认提交
		state_.current_baseline_pos_ = next_baseline;
		state_.is_first_line_ = false;

		for(auto& elem : state_.line_buffer_.elems){
			elem.aabb.move(offset_vec);
			results.elems.push_back(std::move(elem));
			state_.min_bound.min(elem.aabb.vert_00());
			state_.max_bound.max(elem.aabb.vert_11());
		}

		for(auto& ul : state_.line_buffer_.underlines){
			ul.start += offset_vec;
			ul.end += offset_vec;

			math::vec2 ul_min = math::min(ul.start, ul.end);
			math::vec2 ul_max = math::max(ul.start, ul.end);
			ul_min.y -= ul.thickness / 2.f;
			ul_max.y += ul.thickness / 2.f;

			state_.min_bound.min(ul_min);
			state_.max_bound.max(ul_max);

			results.underlines.push_back(std::move(ul));
		}

		state_.prev_line_descender_ = current_desc;
		state_.line_buffer_.clear();
		return true;
	}

	bool flush_block(glyph_layout& results){
		if(state_.current_block.glyphs.empty() && state_.current_block.underlines.empty()) return true;

		// 辅助 Lambda：预测合并 block 后的尺寸是否合规
		auto check_block_fit = [&](const line_buffer_t& buffer, const layout_block& blk) -> bool {
			// --- [Fix] 主轴检查：只检查 Advance Width (排版宽度) ---
			// 预测行宽 = 当前行宽 + Block步进
			float predicted_width = buffer.width + blk.cursor.*state_.major_p;
			float limit_width = config.max_extent.*state_.major_p;

			// 如果不是无限宽，且预测宽度 > 限制宽度，则放不下
			if (!std::isinf(limit_width) && predicted_width > limit_width + 0.001f) {
				return false;
			}

			// --- [Fix] 副轴检查：检查 AABB Height (垂直溢出) ---
			// 获取行高信息
			float asc = std::max(buffer.max_ascender, blk.block_ascender);
			float desc = std::max(buffer.max_descender, blk.block_descender);
			if (buffer.elems.empty()) {
				if (asc == 0 && desc == 0) {
					asc = get_scaled_default_line_thickness() / 2.f;
					desc = asc;
				}
			}

			// 估算 Baseline Y
			float estimated_baseline = state_.current_baseline_pos_;
			if (!state_.is_first_line_ && buffer.elems.empty()) {
				const float metrics_sum = state_.prev_line_descender_ + asc;
				const float step = metrics_sum * config.line_spacing_scale + config.line_spacing_fixed_distance;
				estimated_baseline += step;
			}

			float limit_height = config.max_extent.*state_.minor_p;
			if (!std::isinf(limit_height)) {
				// 预测的最底端
				float predicted_bottom = estimated_baseline + desc;
				// 如果已有的内容更靠下，取最大值
				float global_max_y = std::max(state_.max_bound.*state_.minor_p, predicted_bottom);

				// 预测的最顶端 (如果是第一行)
				float predicted_top = estimated_baseline - asc;
				float global_min_y = std::min(state_.min_bound.*state_.minor_p, predicted_top);

				// 检查总高度是否超出
				if (global_max_y - global_min_y > limit_height + 0.001f) return false;
			}

			return true;
		};

		// --- 步骤 1: 尝试放入当前行 ---
		if (check_block_fit(state_.line_buffer_, state_.current_block)) {
			state_.line_buffer_.append(state_.current_block, state_.major_p);
			state_.current_block.clear();
			return true;
		}

		// --- 步骤 2: 放入当前行失败，必须换行 ---

		// 2.1 先提交当前行
		if(!advance_line(results)) return false;

		// 2.2 添加换行提示符 (Wrap Indicator)
		if(config.has_wrap_indicator()){
			const auto& ind = cached_indicator_;
			const float scale = get_current_relative_scale();
			auto scaled_aabb = ind.glyph_aabb.copy();
			math::vec2 p_min = scaled_aabb.vert_00() * scale;
			math::vec2 p_max = scaled_aabb.vert_11() * scale;
			scaled_aabb = {tags::unchecked, tags::from_vertex, p_min, p_max};
			math::vec2 scaled_advance = ind.advance * scale;

			state_.current_block.push_front(
				scaled_aabb,
				{
					scaled_aabb.copy().expand(font::font_draw_expand), graphic::colors::white * .68f,
					cached_indicator_.texture
				},
				scaled_advance);
		}

		// --- 步骤 3: 尝试放入新行 (此时 line_buffer 已被 clear) ---
		if (check_block_fit(state_.line_buffer_, state_.current_block)) {
			state_.line_buffer_.append(state_.current_block, state_.major_p);
		} else {
			// 如果新行还是放不下 (例如单个 Block 比 MaxExtent 还宽)，直接失败
			return false;
		}

		state_.current_block.clear();
		return true;
	}

	bool process_text_run(
		const typesetting::tokenized_text& full_text, glyph_layout& results,
		std::size_t start, std::size_t length, font::font_face_handle* face
	){
		hb_buffer_clear_contents(hb_buffer_.get());
		hb_buffer_add_utf32(hb_buffer_.get(), reinterpret_cast<const std::uint32_t*>(full_text.get_text().data()),
			static_cast<int>(full_text.get_text().size()), static_cast<unsigned int>(start), static_cast<int>(length));
		hb_buffer_set_direction(hb_buffer_.get(), state_.target_hb_dir);

		hb_buffer_guess_segment_properties(hb_buffer_.get());
		const auto req_size_vec = state_.rich_text_context_.get_size(state_.default_font_size);
		const auto snapped_size = get_snapped_size_vec(req_size_vec);
		const math::vec2 run_scale_factor = req_size_vec / snapped_size.as<float>();

		auto rich_offset = state_.rich_text_context_.get_offset();
		(void)face->set_size(snapped_size);

		auto* raw_hb_font = get_hb_font_(face);


		hb_shape(raw_hb_font, hb_buffer_.get(), feature_stack_.data(), feature_stack_.size());
		unsigned int len;
		hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(hb_buffer_.get(), &len);
		hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(hb_buffer_.get(), &len);

		long long last_applied_pos = static_cast<long long>(start) - 1;
		auto ft_face = face->get();
		float ul_position;
		float ul_thickness;
		if(ft_face->units_per_EM != 0){
			float em_scale = req_size_vec.y / static_cast<float>(ft_face->units_per_EM);
			if(is_vertical_()){
				ul_position = static_cast<float>(ft_face->descender) * em_scale;
			} else{
				ul_position = static_cast<float>(ft_face->underline_position) * em_scale;
			}

			ul_thickness = static_cast<float>(ft_face->underline_thickness) * em_scale;
		} else{
			ul_position = 0.0f;
			ul_thickness = 0.0f;
		}

		if(ul_thickness <= 0.0f){
			ul_thickness = std::max(1.0f, req_size_vec.y / 14.0f);
		} else{
			ul_thickness = std::max(ul_thickness, 1.0f);
		}

		if(ul_position == 0.0f){
			ul_position = -req_size_vec.y / 10.0f;
		}

		auto commit_underline = [&](math::vec2 start_pos, math::vec2 end_pos, graphic::color color){
			math::vec2 offset_vec{};
			offset_vec.*state_.minor_p = -ul_position;

			underline ul;
			ul.start = start_pos + offset_vec;
			ul.end = end_pos + offset_vec;
			ul.thickness = ul_thickness;
			ul.color = color;
			state_.current_block.push_back_underline(ul);
		};

		std::optional<math::vec2> active_ul_start;
		for(unsigned int i = 0; i < len; ++i){
			const font::glyph_index_t gid = infos[i].codepoint;
			const auto current_cluster = infos[i].cluster;

			bool was_ul = state_.rich_text_context_.is_underline_enabled();
			graphic::color prev_color = state_.rich_text_context_.get_color();
			for(auto p = last_applied_pos + 1; p <= current_cluster; ++p){
				auto tokens = full_text.get_token_group(static_cast<std::size_t>(p),
					state_.token_soft_last_iterator);
				if(!tokens.empty()){
					state_.rich_text_context_.update(
						*manager_,
						typesetting::update_param{
							state_.default_font_size, state_.current_block.block_ascender,
							state_.current_block.block_descender, is_vertical_()
						},
						tokens, typesetting::context_update_mode::soft_only);
					state_.token_soft_last_iterator = tokens.end();
				}
			}
			rich_offset = state_.rich_text_context_.get_offset();

			last_applied_pos = current_cluster;

			bool is_ul = state_.rich_text_context_.is_underline_enabled();
			if(was_ul && !is_ul && active_ul_start.has_value()){
				commit_underline(*active_ul_start, state_.current_block.cursor, prev_color);
				active_ul_start.reset();
			}

			if(state_.rich_text_context_.is_underline_enabled() && !active_ul_start.has_value()){
				active_ul_start = state_.current_block.cursor;
			}

			const auto ch = full_text.get_text()[infos[i].cluster];
			if(bool is_delimiter = (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || IsSeparator(ch))){
				if(active_ul_start.has_value()){
					commit_underline(*active_ul_start, state_.current_block.cursor,
						state_.rich_text_context_.get_color());
					active_ul_start.reset();
				}

				if(!flush_block(results)) return false;

				if(ch != '\n' && ch != '\r' && state_.rich_text_context_.is_underline_enabled()){
					active_ul_start = state_.current_block.cursor;
				}
			}

			font::glyph loaded_glyph = manager_->get_glyph_exact(*face, {gid, snapped_size});

			float asc, desc;
			if(is_vertical_()){
				float width = loaded_glyph.metrics().advance.x * run_scale_factor.x;
				asc = width / 2.f;
				desc = width / 2.f;
			} else{
				asc = loaded_glyph.metrics().ascender() * run_scale_factor.y;
				desc = loaded_glyph.metrics().descender() * run_scale_factor.y;
			}

			if(ch != '\n' && ch != '\r'){
				state_.current_block.block_ascender = std::max(state_.current_block.block_ascender, asc);
				state_.current_block.block_descender = std::max(state_.current_block.block_descender, desc);
			}

			switch(ch){
			case '\r' : if(config.line_feed_type == linefeed::CRLF){
					state_.line_buffer_.width = 0.f;
					active_ul_start.reset();
				}
				continue;
			case '\n' :{
				if(!flush_block(results)) return false;

				if(!advance_line(results)) return false;

				state_.current_block.cursor = {};

				if(config.line_feed_type == linefeed::CRLF){
					state_.current_block.cursor.*state_.minor_p = 0;
				}else{
					state_.current_block.cursor = {};
				}

				active_ul_start.reset();
				continue;
			}
			case '\t' :{
				float tab_step = config.tab_scale * get_scaled_default_space_width();
				if(tab_step > std::numeric_limits<float>::epsilon()){
					float current_pos = state_.current_block.cursor.*state_.major_p;
					float next_tab = (std::floor(current_pos / tab_step) + 1) * tab_step;
					state_.current_block.cursor.*state_.major_p = next_tab;
				}
				continue;
			}
			case ' ' :{
				const auto sp = math::vec2{
						font::normalize_len(pos[i].x_advance), font::normalize_len(pos[i].y_advance)
					} * run_scale_factor;
				state_.current_block.cursor = move_pen(state_.current_block.cursor, sp);
			}
				continue;
			default : break;
			}

			const auto advance = math::vec2{
					font::normalize_len(pos[i].x_advance), font::normalize_len(pos[i].y_advance)
				} * run_scale_factor;
			const float x_offset = font::normalize_len(pos[i].x_offset) * run_scale_factor.x;
			const float y_offset = font::normalize_len(pos[i].y_offset) * run_scale_factor.y;

			math::vec2 glyph_local_draw_pos{x_offset, -y_offset};
			glyph_local_draw_pos += rich_offset;
			if(is_reversed_()){
				state_.current_block.cursor = move_pen(state_.current_block.cursor, advance);
			}

			const math::frect actual_aabb = loaded_glyph.metrics().place_to(
				state_.current_block.cursor + glyph_local_draw_pos, run_scale_factor);
			const math::frect draw_aabb = actual_aabb.copy().expand(font::font_draw_expand * run_scale_factor);

			state_.current_block.push_back(actual_aabb,
				{draw_aabb, state_.rich_text_context_.get_color(), std::move(loaded_glyph)});
			if(!is_reversed_()){
				state_.current_block.cursor = move_pen(state_.current_block.cursor, advance);
			}
		}

		if(active_ul_start.has_value()){
			commit_underline(*active_ul_start, state_.current_block.cursor, state_.rich_text_context_.get_color());
		}

		if(last_applied_pos < static_cast<long long>(start + length - 1)){
			for(auto p = last_applied_pos + 1; p < start + length; ++p){
				auto tokens = full_text.get_token_group(static_cast<std::size_t>(p), full_text.get_init_token());
				if(!tokens.empty()){
					state_.rich_text_context_.update(*manager_,
						{
							state_.default_font_size, state_.current_block.block_ascender,
							state_.current_block.block_descender, is_vertical_()
						}, tokens,
						typesetting::context_update_mode::soft_only);
					state_.token_soft_last_iterator = tokens.end();
				}
			}
		}

		return true;
	}

	bool process_layout(const typesetting::tokenized_text& full_text, glyph_layout& results){
		std::size_t current_idx = 0;
		std::size_t run_start = 0;
		font::font_face_handle* current_face = nullptr;
		auto resolve_face = [&](font::font_face_handle* current, char32_t codepoint) -> font::font_face_handle*{
			if(current && current->index_of(codepoint)) return current;
			const auto* family = &state_.rich_text_context_.get_font(manager_->get_default_family());
			auto face_view = manager_->use_family(family);
			auto [best_face, _] = face_view.find_glyph_of(codepoint);
			return best_face;
		};
		while(current_idx < full_text.get_text().size()){
			auto tokens = full_text.get_token_group(current_idx, state_.token_hard_last_iterator);
			state_.token_hard_last_iterator = tokens.end();


			const bool need_another_run = typesetting::check_token_group_need_another_run(tokens);
			if(need_another_run){
				if(current_face && current_idx > run_start){
					if(!process_text_run(full_text, results, run_start, current_idx - run_start, current_face)) return
						false;
				}

				state_.rich_text_context_.update(
					*manager_, {
						state_.default_font_size, state_.current_block.block_ascender,
						state_.current_block.block_descender, is_vertical_()
					},
					tokens, typesetting::context_update_mode::hard_only,
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

				if(!flush_block(results)) return false;
			}

			const auto codepoint = full_text.get_text()[current_idx];
			font::font_face_handle* best_face = resolve_face(current_face, codepoint);
			if(current_face == nullptr){
				current_face = best_face;
			} else if(best_face != current_face){
				if(!process_text_run(full_text, results, run_start, current_idx - run_start, current_face)) return
					false;
				current_face = best_face;
				run_start = current_idx;
			}
			current_idx++;
		}

		if(current_face != nullptr)
			if(!process_text_run(full_text, results, run_start,
				full_text.get_text().size() - run_start, current_face))
				return false;

		if(!flush_block(results)) return false;
		if(!advance_line(results)) return false;

		return true;
	}

	FORCE_INLINE inline void finalize(glyph_layout& results){
		if(results.elems.empty()){
			results.extent = {0, 0};
			return;
		}

		auto extent = state_.max_bound - state_.min_bound;
		results.extent = extent;
		for(auto& elem : results.elems){
			elem.aabb.move(-state_.min_bound);
		}
		for(auto& ul : results.underlines){
			ul.start -= state_.min_bound;
			ul.end -= state_.min_bound;
		}
	}

#pragma region TRIVIAL_OP

	FORCE_INLINE inline bool is_reversed_() const noexcept{
		return state_.target_hb_dir == HB_DIRECTION_RTL || state_.target_hb_dir == HB_DIRECTION_BTT;
	}

	FORCE_INLINE inline bool is_vertical_() const noexcept{
		return state_.target_hb_dir == HB_DIRECTION_TTB || state_.target_hb_dir == HB_DIRECTION_BTT;
	}

	[[nodiscard]] FORCE_INLINE inline font::glyph_size_type get_current_snapped_size() const noexcept{
		auto req_size = state_.rich_text_context_.get_size(state_.default_font_size);
		return get_snapped_size_vec(req_size);
	}

	FORCE_INLINE inline static font::glyph_size_type get_snapped_size_vec(font::glyph_size_type v) noexcept{
		return {font::get_snapped_size(v.x), font::get_snapped_size(v.y)};
	}

	FORCE_INLINE inline static font::glyph_size_type get_snapped_size_vec(math::vec2 v) noexcept{
		return get_snapped_size_vec(v.as<font::glyph_size_type::value_type>());
	}

	FORCE_INLINE inline hb_font_t* get_hb_font_(font::font_face_handle* face) noexcept{
		if(auto ptr = hb_cache_.get(face)) return ptr->get();
		auto new_hb_font = create_harfbuzz_font(*face);
		hb_ft_font_set_load_flags(new_hb_font.get(), FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING);
		const auto rst = new_hb_font.get();
		hb_cache_.put(face, std::move(new_hb_font));
		return rst;
	}

	FORCE_INLINE inline math::vec2 move_pen(math::vec2 pen, math::vec2 advance) const noexcept{
		if(state_.target_hb_dir == HB_DIRECTION_LTR){
			pen.x += advance.x;
			pen.y -= advance.y;
		} else if(state_.target_hb_dir == HB_DIRECTION_RTL){
			pen.x -= advance.x;
			pen.y -= advance.y;
		} else if(state_.target_hb_dir == HB_DIRECTION_TTB){
			pen.x += advance.x;
			pen.y -= advance.y;
		} else if(state_.target_hb_dir == HB_DIRECTION_BTT){
			pen.x += advance.x;
			pen.y += advance.y;
		}
		return pen;
	}

	[[nodiscard]] FORCE_INLINE inline float get_current_relative_scale() const noexcept{
		const auto current_size = state_.rich_text_context_.get_size(state_.default_font_size);
		return current_size.y / state_.default_font_size.y;
	}

	[[nodiscard]] FORCE_INLINE inline float get_scaled_default_line_thickness() const noexcept{
		return state_.default_line_thickness * get_current_relative_scale();
	}

	[[nodiscard]] FORCE_INLINE inline float get_scaled_default_space_width() const noexcept{
		return state_.default_space_width * get_current_relative_scale();
	}

#pragma endregion
};

}
