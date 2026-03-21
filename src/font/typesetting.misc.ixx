module;

#include <hb.h>
#include <hb-ft.h>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.typesetting:misc;

import std;
import mo_yanxi.typesetting.util;

import mo_yanxi.math.vector2;
import mo_yanxi.math;
import mo_yanxi.math.rect_ortho;
import mo_yanxi.graphic.color;
import mo_yanxi.font;
import mo_yanxi.font.manager;
import mo_yanxi.hb.wrap;
import mo_yanxi.cond_exist;

import mo_yanxi.typesetting.rich_text;

namespace mo_yanxi::typesetting{
using typst_szt = unsigned;

export enum struct layout_direction : std::uint8_t{ ltr, rtl, ttb, btt, deduced };

export enum struct linefeed_type : std::uint8_t{ lf, crlf };

export enum struct line_alignment : std::uint8_t{ start, center, end, justify };


export struct glyph_elem{
	math::frect aabb;
	graphic::color color;
	font::glyph_borrow texture;
	float slant_factor_asc;
	float slant_factor_desc;
	float weight_offset;
};

export struct underline{
	math::vec2 start;
	math::vec2 end;
	float thickness;
	typst_szt start_gap_count;
	typst_szt end_gap_count;
	graphic::color color;
};

export struct subrange{
	typst_szt pos{};
	typst_szt size{};
};

export struct logical_cluster{
	typst_szt cluster_index;
	typst_szt cluster_span;
	math::frect logical_rect;
};

export struct line_align_result{
	math::vec2 start_pos;
	math::vec2 letter_spacing;
};

export struct line{
	subrange glyph_range;
	subrange underline_range;
	subrange cluster_range;

	layout_rect rect;
	math::vec2 start_pos;

	[[nodiscard]] constexpr line_align_result calculate_alignment(
		math::vec2 extent,
		line_alignment align,
		layout_direction dir) const noexcept{
		line_align_result result{start_pos, math::vec2{}};
		const bool is_vertical = (dir == layout_direction::ttb || dir == layout_direction::btt);
		const bool is_reversed = (dir == layout_direction::rtl || dir == layout_direction::btt);

		const float container_width = is_vertical ? extent.y : extent.x;
		const float abs_content = math::abs(rect.width);
		const float remaining = container_width - abs_content;

		float align_offset = 0.f;
		float spacing = 0.f;

		if(!math::isinf(container_width) && remaining > 0.001f){
			float factor = 0.f; // 0.f 代表起点，0.5f 代表居中，1.0f 代表终点

			switch(align){
			case line_alignment::start : factor = is_reversed ? 1.f : 0.f;
				break;
			case line_alignment::center : factor = 0.5f;
				break;
			case line_alignment::end : factor = is_reversed ? 0.f : 1.f;
				break;
			case line_alignment::justify : factor = is_reversed ? 1.f : 0.f;
				if(glyph_range.size > 1){
					spacing = remaining / static_cast<float>(glyph_range.size - 1);
				}
				break;
			}

			// 统一计算位移
			align_offset = is_reversed ? (container_width - remaining * (1.f - factor)) : (remaining * factor);
		} else if(is_reversed){
			align_offset = math::isinf(container_width) ? abs_content : container_width;
		}

		if(is_vertical){
			result.start_pos.y += align_offset;
			result.letter_spacing.y = (dir == layout_direction::ttb) ? -spacing : spacing;
		} else{
			result.start_pos.x += align_offset;
			result.letter_spacing.x = (dir == layout_direction::ltr) ? spacing : -spacing;
		}

		return result;
	}
};

export struct glyph_layout{
	std::vector<glyph_elem> elems;
	std::vector<underline> underlines;
	std::vector<logical_cluster> clusters;

	std::vector<line> lines;

	math::vec2 extent;
	layout_direction direction;
	bool is_exhausted;

	void clear() noexcept{
		elems.clear();
		underlines.clear();
		clusters.clear();
		lines.clear();
		extent = {};
		direction = {};
		is_exhausted = {};
	}

	struct hit_result{
		const line* source_line;
		const logical_cluster* source;
		typst_szt span_offset;
		bool is_hit;

		constexpr explicit operator bool() const noexcept{
			return source != nullptr;
		}
	};

	constexpr bool empty() const noexcept{ return lines.empty(); }

	[[nodiscard]] hit_result hit_test(math::vec2 pos, line_alignment line_align) const noexcept{
		if(line_align == line_alignment::justify || direction != layout_direction::ltr){
			return {};
		}
		if(lines.empty() || clusters.empty()) return {};

		const bool is_vertical = (direction == layout_direction::ttb || direction == layout_direction::btt);

		auto get_line_cross_center = [&](const line& l){
			auto align = l.calculate_alignment(extent, line_align, direction);
			if(is_vertical){
				return align.start_pos.x + (l.rect.descender - l.rect.ascender) / 2.0f;
			} else{
				return align.start_pos.y + (l.rect.descender - l.rect.ascender) / 2.0f;
			}
		};

		const auto line_it = std::ranges::lower_bound(lines, is_vertical ? pos.x : pos.y, {}, get_line_cross_center);

		typst_szt best_line_idx = 0;
		if(line_it == lines.end()){
			best_line_idx = lines.size() - 1;
		} else if(line_it == lines.begin()){
			best_line_idx = 0;
		} else{
			auto prev_it = std::ranges::prev(line_it);
			float dist_current = std::abs(get_line_cross_center(*line_it) - (is_vertical ? pos.x : pos.y));
			float dist_prev = std::abs(get_line_cross_center(*prev_it) - (is_vertical ? pos.x : pos.y));
			best_line_idx = (dist_prev < dist_current)
				                ? std::ranges::distance(lines.begin(), prev_it)
				                : std::ranges::distance(lines.begin(), line_it);
		}

		const auto& best_line = lines[best_line_idx];
		if(best_line.cluster_range.size == 0) return {};

		const auto align = best_line.calculate_alignment(extent, line_align, direction);
		const math::vec2 local_pos = pos - align.start_pos;

		const auto cluster_begin = clusters.begin() + best_line.cluster_range.pos;
		const auto cluster_end = cluster_begin + best_line.cluster_range.size;

		auto cluster_it = std::lower_bound(cluster_begin, cluster_end, local_pos,
			[&](const logical_cluster& c, const math::vec2& p){
				const math::vec2 center = c.logical_rect.get_center();
				return is_vertical ? (center.y < p.y) : (center.x < p.x);
			});

		auto best_cluster_it = cluster_begin;
		if(cluster_it == cluster_end){
			best_cluster_it = std::prev(cluster_end);
		} else if(cluster_it == cluster_begin){
			best_cluster_it = cluster_begin;
		} else{
			auto prev_it = std::prev(cluster_it);
			float dist_current = (local_pos - cluster_it->logical_rect.get_center()).length2();
			float dist_prev = (local_pos - prev_it->logical_rect.get_center()).length2();
			best_cluster_it = (dist_prev < dist_current) ? prev_it : cluster_it;
		}

		const math::vec2 center = best_cluster_it->logical_rect.get_center();
		bool is_hit = best_cluster_it->logical_rect.contains_loose(local_pos);

		// 计算基于 logical_rect 的相对落点比例 (0.0 到 1.0)
		float ratio = 0.0f;
		if(is_vertical){
			float h = best_cluster_it->logical_rect.vert_11().y - best_cluster_it->logical_rect.vert_00().y;
			if(h > 0.001f) ratio = (local_pos.y - best_cluster_it->logical_rect.vert_00().y) / h;
		} else{
			float w = best_cluster_it->logical_rect.vert_11().x - best_cluster_it->logical_rect.vert_00().x;
			if(w > 0.001f) ratio = (local_pos.x - best_cluster_it->logical_rect.vert_00().x) / w;
		}

		ratio = std::clamp(ratio, 0.0f, 1.0f);

		// 结合 cluster_span 利用四舍五入找到最近的等分边界
		typst_szt span_offset = static_cast<typst_szt>(std::round(ratio * best_cluster_it->cluster_span));

		return {&best_line, std::to_address(best_cluster_it), span_offset, is_hit};
	}
};


FORCE_INLINE CONST_FN constexpr bool is_separator(char32_t c) noexcept{
	if(c < 0x80){
		return c == U',' || c == U'.' || c == U';' || c == U':' || c == U'!' || c == U'?';
	}
	constexpr char32_t starts[16] = {
			0x0000, 0x1100, 0x2026, 0x2E80, 0x3001, 0x3040, 0x3100, 0x31F0, 0x3400,
			0x4E00, 0xAC00, 0xFF01, 0xFF0C, 0xFF1A, 0xFF1F, 0xFFFFFFFF
		};
	constexpr char32_t ends[16] = {
			0x0000, 0x11FF, 0x2026, 0x2FDF, 0x3002, 0x30FF, 0x31BF, 0x31FF, 0x4DBF,
			0x9FFF, 0xD7AF, 0xFF01, 0xFF0C, 0xFF1B, 0xFF1F, 0xFFFFFFFF
		};
	std::uint32_t idx = 0;
	idx |= (static_cast<std::uint32_t>(c >= starts[idx | 8]) << 3);
	idx |= (static_cast<std::uint32_t>(c >= starts[idx | 4]) << 2);
	idx |= (static_cast<std::uint32_t>(c >= starts[idx | 2]) << 1);
	idx |= static_cast<std::uint32_t>(c >= starts[idx | 1]);
	return c <= ends[idx];
}

font::hb::font_ptr create_harfbuzz_font(const font::font_face_handle& face){
	hb_font_t* font = hb_ft_font_create_referenced(face);
	return font::hb::font_ptr{font};
}

struct rich_text_state{
	rich_text_context rich_context{};
	// --- 富文本/状态跟踪器 ---
	tokenized_text_view::token_iterator token_hard_last{};
	tokenized_text_view::token_iterator token_soft_last{};
	typst_szt next_apply_pos = 0;

	void reset(const tokenized_text_view& t) noexcept{
		rich_context.clear();
		token_hard_last = t.get_init_token();
		token_soft_last = t.get_init_token();
		next_apply_pos = 0;
	}
};

export struct layout_span{
	typst_szt elem_start{};
	typst_szt ul_start{};
	typst_szt cluster_start{};
};

struct block_data{
	layout_span block_span{};
	math::vec2 cursor{};
	math::vec2 block_pos_min{math::vectors::constant2<float>::inf_positive_vec2};
	math::vec2 block_pos_max{-math::vectors::constant2<float>::inf_positive_vec2};
	float block_ascender{};
	float block_descender{};

	FORCE_INLINE void max_bound_height(float ascender_, float descender_) noexcept{
		block_ascender = std::max(block_ascender, ascender_);
		block_descender = std::max(block_descender, descender_);
	}

	FORCE_INLINE inline void clear() & noexcept{
		*this = {};
	}
};

struct line_data{
	layout_span line_span{};
	layout_rect line_bound{};
	math::vec2 line_pos_min{math::vectors::constant2<float>::inf_positive_vec2};
	math::vec2 line_pos_max{-math::vectors::constant2<float>::inf_positive_vec2};

	FORCE_INLINE inline void clear() & noexcept{
		*this = {};
	}
};


struct layout_state_t{
	math::vec2 min_bound{math::vectors::constant2<float>::inf_positive_vec2};
	math::vec2 max_bound{-math::vectors::constant2<float>::inf_positive_vec2};
	math::vec2 default_font_size{};
	float default_line_thickness{};
	float default_ascender{};
	float default_descender{};
	float default_space_width{};

	//TODO using static dispatch?
	float math::vec2::* major_p = &math::vec2::x;
	float current_baseline_pos{};

	float math::vec2::* minor_p = &math::vec2::y;
	float prev_line_descender{};

	hb_direction_t target_hb_dir = HB_DIRECTION_INVALID;

	bool is_first_line = true;
	bool is_vertical_mode = false;

	void reset(){
		min_bound = math::vectors::constant2<float>::inf_positive_vec2;
		max_bound = -math::vectors::constant2<float>::inf_positive_vec2;
		default_font_size = {};
		prev_line_descender = {};
		current_baseline_pos = {};
		is_first_line = true;
	}
};

export struct layout_config{
	layout_direction direction;
	math::vec2 max_extent = math::vectors::constant2<float>::inf_positive_vec2;
	math::optional_vec2<float> default_font_size{math::nullopt_vec2<float>};
	linefeed_type line_feed_type;
	float throughout_scale = 1.f;
	float tab_scale = 4.f;
	float line_spacing_scale = 1.5f;
	float line_spacing_fixed_distance = 0.f;
	char32_t wrap_indicator_char = U'\u2925';
	math::optional_vec2<float> screen_ppi{math::nullopt_vec2<float>};
	rich_text_fallback_style rich_text_fallback_style;

	[[nodiscard]] math::vec2 get_default_font_size() const noexcept{
		return default_font_size.value_or(
			glyph_size::get_glyph_std_size_at(glyph_size::standard_size, get_screen_ppi()));
	}

	[[nodiscard]] math::vec2 get_screen_ppi() const noexcept{
		return screen_ppi.value_or(glyph_size::screen_ppi);
	}

	[[nodiscard]] constexpr bool has_wrap_indicator() const noexcept{ return wrap_indicator_char != 0; }
	constexpr bool operator==(const layout_config&) const noexcept = default;


	bool set_max_extent(math::vec2 ext){
		if(max_extent == ext) return false;
		max_extent = ext;
		return true;
	}
};


struct layout_buffer : block_data, line_data{
	template <bool HasClusters>
	constexpr bool empty(const glyph_layout& results) const noexcept{
		bool is_empty = block_span.elem_start == results.elems.size() && block_span.ul_start == results.underlines.
			size();
		if constexpr(HasClusters){
			is_empty = is_empty && (block_span.cluster_start == results.clusters.size());
		}
		return is_empty;
	}

	FORCE_INLINE inline void clear() & noexcept{
		block_data::clear();
		line_data::clear();
	}

	template <bool HasClusters>
	FORCE_INLINE inline void block_sync_start(const glyph_layout& results){
		block_span.elem_start = results.elems.size();
		block_span.ul_start = results.underlines.size();
		if constexpr(HasClusters) block_span.cluster_start = results.clusters.size();
	}

	FORCE_INLINE inline void push_back(glyph_layout& results, math::frect glyph_region, const glyph_elem& glyph){
		results.elems.push_back(glyph);
		block_pos_min.min(glyph_region.vert_00());
		block_pos_max.max(glyph_region.vert_11());
	}

	template <bool HasClusters>
	FORCE_INLINE inline void push_front(glyph_layout& results, math::frect glyph_region, const glyph_elem& glyph,
		math::vec2 glyph_advance){
		// 原位移动当前 block 已经写入的元素
		for(std::size_t i = block_span.elem_start; i < results.elems.size(); ++i){
			results.elems[i].aabb.move(glyph_advance);
		}
		for(std::size_t i = block_span.ul_start; i < results.underlines.size(); ++i){
			results.underlines[i].start += glyph_advance;
			results.underlines[i].end += glyph_advance;
		}
		if constexpr(HasClusters){
			for(std::size_t i = block_span.cluster_start; i < results.clusters.size(); ++i){
				results.clusters[i].logical_rect.move(glyph_advance);
			}
		}

		// 由于 span.elem_start 必定对应当前 block 的起点（且排版总是追加），
		// 这里的 insert 只会发生微量的元素平移（通常只有几个字符的开销）
		results.elems.insert(results.elems.begin() + block_span.elem_start, glyph);
		block_pos_min += glyph_advance;
		block_pos_max += glyph_advance;
		block_pos_min.min(glyph_region.vert_00());
		block_pos_max.max(glyph_region.vert_11());
		cursor += glyph_advance;
	}

	template <bool HasClusters>
	FORCE_INLINE inline void push_front_visual(glyph_layout& results, math::frect glyph_region, glyph_elem&& glyph,
		math::vec2 glyph_advance){
		// 原位移动当前 block 已经写入的元素
		for(std::size_t i = block_span.elem_start; i < results.elems.size(); ++i){
			results.elems[i].aabb.move(glyph_advance);
		}
		for(std::size_t i = block_span.ul_start; i < results.underlines.size(); ++i){
			results.underlines[i].start += glyph_advance;
			results.underlines[i].end += glyph_advance;
		}
		if constexpr(HasClusters){
			for(std::size_t i = block_span.cluster_start; i < results.clusters.size(); ++i){
				results.clusters[i].logical_rect.move(glyph_advance);
			}
		}

		results.elems.push_back(std::move(glyph));
		block_pos_min += glyph_advance;
		block_pos_max += glyph_advance;
		block_pos_min.min(glyph_region.vert_00());
		block_pos_max.max(glyph_region.vert_11());
		cursor += glyph_advance;
	}

	template <bool HasClusters>
	FORCE_INLINE inline void append(glyph_layout& results, float math::vec2::* major_axis){
		math::vec2 move_vec{};
		move_vec.*major_axis = line_bound.width;

		if(bool has_elems = (results.elems.size() > block_span.elem_start) || (results.underlines.size() > block_span.
			ul_start)){
			line_pos_min.min(block_pos_min + move_vec);
			line_pos_max.max(block_pos_max + move_vec);
		}

		// 修改已经存在于 results 中，但属于此 block 的数据的相对偏移
		for(std::size_t i = block_span.elem_start; i < results.elems.size(); ++i){
			results.elems[i].aabb.move(move_vec);
		}
		for(std::size_t i = block_span.ul_start; i < results.underlines.size(); ++i){
			results.underlines[i].start += move_vec;
			results.underlines[i].end += move_vec;
		}
		if constexpr(HasClusters){
			for(std::size_t i = block_span.cluster_start; i < results.clusters.size(); ++i){
				results.clusters[i].logical_rect.move(move_vec);
			}
		}

		line_bound.width += cursor.*major_axis;
		line_bound.ascender = std::max(line_bound.ascender, block_ascender);
		line_bound.descender = std::max(line_bound.descender, block_descender);
	}

	template <math::bool2 IsInf>
	FORCE_INLINE bool check_block_fit(
		const layout_state_t& state_,
		const layout_config& config_,
		const glyph_layout& results,
		const float scale) const noexcept{
		const float predicted_width = line_bound.width + cursor.*state_.major_p;
		if constexpr(!IsInf.x){
			if(predicted_width > config_.max_extent.*state_.major_p + 0.001f){
				return false;
			}
		}

		const float min_asc = state_.default_ascender * scale;
		const float min_desc = state_.default_descender * scale;

		const float asc = math::max(line_bound.ascender, block_ascender, min_asc);
		const float desc = math::max(line_bound.descender, block_descender, min_desc);
		float estimated_baseline = state_.current_baseline_pos;
		if(!state_.is_first_line && (line_span.elem_start == results.elems.size())){
			estimated_baseline += (state_.prev_line_descender + asc) * config_.line_spacing_scale + config_.
				line_spacing_fixed_distance;
		}

		if constexpr(!IsInf.y){
			const float predicted_bottom = estimated_baseline + desc;
			const float predicted_top = estimated_baseline - asc;
			if((math::max(state_.max_bound.*state_.minor_p, predicted_bottom) -
					math::min(state_.min_bound.*state_.minor_p, predicted_top)) > config_.max_extent.*state_.minor_p +
				0.001f){
				return false;
			}
		}
		return true;
	}
};


namespace concepts{
template <typename T>
concept ClusterPolicy = requires(){
	{ T::enabled } -> std::convertible_to<bool>;
	// policy.push(block, cluster);
	// policy.reserve(block, size);
};

template <typename T, typename StateType>
concept RichTextPolicy = requires(){
	{ T::enabled } -> std::convertible_to<bool>;
	// policy.sync(state, config, manager, text, target);
	// { policy.get_color(state, config) } -> std::convertible_to<graphic::color>;
	// { policy.is_underline_enabled(state, config) } -> std::convertible_to<bool>;
	// { policy.get_rich_offset(state, config) } -> std::convertible_to<math::vec2>;
	// { policy.get_size(state, config) } -> std::convertible_to<math::vec2>;
};
}

namespace policies{
export struct ignore_clusters{
	static constexpr bool enabled = false;

	// 签名同步修改，但不做任何操作
	static constexpr void push(glyph_layout& results, const logical_cluster& cluster) noexcept{
	}

	static constexpr void reserve(glyph_layout& results, std::size_t size) noexcept{
	}
};

export
struct store_clusters{
	static constexpr bool enabled = true;

	// 直接操作 results 中的 clusters 数组
	static constexpr void push(glyph_layout& results, const logical_cluster& cluster){
		results.clusters.push_back(cluster);
	}

	static constexpr void reserve(glyph_layout& results, std::size_t size){
		results.clusters.reserve(results.clusters.size() + size);
	}
};

export struct plain_text_only{
	static constexpr bool enabled = false;

	FORCE_INLINE static constexpr void sync(const auto&, const auto&, const layout_state_t& state,
		const layout_config& config, font::font_manager& manager, const tokenized_text_view& full_text,
		std::size_t target_cluster) noexcept{
	}

	FORCE_INLINE static graphic::color get_color(const auto&, const layout_config& config) noexcept{
		return config.rich_text_fallback_style.color;
	}

	FORCE_INLINE static bool is_underline_enabled(const auto&, const layout_config& config) noexcept{
		return config.rich_text_fallback_style.enables_underline;
	}

	FORCE_INLINE static math::vec2 get_rich_offset(const auto&, const layout_config& config) noexcept{
		return config.rich_text_fallback_style.offset;
	}

	FORCE_INLINE static math::vec2 get_size(const auto&, const layout_state_t& state) noexcept{
		return state.default_font_size;
	}
};

export
struct rich_text_enabled{
	static constexpr bool enabled = true;

	FORCE_INLINE static void sync(
		rich_text_state& rich_context, const layout_buffer& buffer, layout_state_t& state, const layout_config& config,
		font::font_manager& manager, const tokenized_text_view& full_text, std::size_t target_cluster){
		for(typst_szt p = rich_context.next_apply_pos; p <= target_cluster; ++p){
			auto tokens = full_text.get_token_group(p, rich_context.token_soft_last);
			if(!tokens.empty()){
				rich_context.rich_context.update(
					manager,
					{state.default_font_size, buffer.block_ascender, buffer.block_descender, state.is_vertical_mode},
					config.rich_text_fallback_style,
					tokens, context_update_mode::soft_only
				);
				rich_context.token_soft_last = tokens.end();
			}
		}
		rich_context.next_apply_pos = target_cluster + 1;
	}

	FORCE_INLINE static graphic::color get_color(const rich_text_state& rich_context,
		const layout_config& config) noexcept{
		return rich_context.rich_context.get_color(config.rich_text_fallback_style);
	}

	FORCE_INLINE static bool is_underline_enabled(const rich_text_state& rich_context,
		const layout_config& config) noexcept{
		return rich_context.rich_context.is_underline_enabled(config.rich_text_fallback_style);
	}

	FORCE_INLINE static math::vec2 get_rich_offset(const rich_text_state& rich_context,
		const layout_config& config) noexcept{
		return rich_context.rich_context.get_offset(config.rich_text_fallback_style);
	}

	FORCE_INLINE static math::vec2 get_size(const rich_text_state& rich_context, const layout_state_t& state) noexcept{
		return rich_context.rich_context.get_size(state.default_font_size);
	}
};
}
}
