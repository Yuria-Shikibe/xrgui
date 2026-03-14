module;

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.typesetting:result;

import std;
import mo_yanxi.typesetting.util;

import mo_yanxi.math.vector2;
import mo_yanxi.math;
import mo_yanxi.math.rect_ortho;
import mo_yanxi.graphic.color;
import mo_yanxi.font;
import mo_yanxi.font.manager;

namespace mo_yanxi::typesetting{
using typst_szt = unsigned;

export enum struct layout_direction{ ltr, rtl, ttb, btt, deduced };

export enum struct linefeed_type{ lf, crlf };

export enum struct line_alignment{ start, center, end, justify };


export struct glyph_elem{
	math::frect aabb;
	graphic::color color;
	font::glyph_borrow texture;
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
	layout_direction dir) const noexcept {

		line_align_result result{start_pos, math::vec2{}};
		const bool is_vertical = (dir == layout_direction::ttb || dir == layout_direction::btt);
		const bool is_reversed = (dir == layout_direction::rtl || dir == layout_direction::btt);

		const float container_width = is_vertical ? extent.y : extent.x;
		const float abs_content = math::abs(rect.width);
		const float remaining = container_width - abs_content;

		float align_offset = 0.f;
		float spacing = 0.f;

		if (!math::isinf(container_width) && remaining > 0.001f) {
			float factor = 0.f; // 0.f 代表起点，0.5f 代表居中，1.0f 代表终点

			switch (align) {
			case line_alignment::start:   factor = is_reversed ? 1.f : 0.f; break;
			case line_alignment::center:  factor = 0.5f; break;
			case line_alignment::end:     factor = is_reversed ? 0.f : 1.f; break;
			case line_alignment::justify:
				factor = is_reversed ? 1.f : 0.f;
				if (glyph_range.size > 1) {
					spacing = remaining / static_cast<float>(glyph_range.size - 1);
				}
				break;
			}

			// 统一计算位移
			align_offset = is_reversed ? (container_width - remaining * (1.f - factor)) : (remaining * factor);
		} else if (is_reversed) {
			align_offset = math::isinf(container_width) ? abs_content : container_width;
		}

		if (is_vertical) {
			result.start_pos.y += align_offset;
			result.letter_spacing.y = (dir == layout_direction::ttb) ? -spacing : spacing;
		} else {
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

	void clear() noexcept{
		elems.clear();
		underlines.clear();
		clusters.clear();
		lines.clear();
		extent = {};
		direction = {};
	}
};

}