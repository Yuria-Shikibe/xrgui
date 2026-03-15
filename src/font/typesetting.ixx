module;

#include <hb.h>
#include <hb-ft.h>
#include <freetype/freetype.h>

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.typesetting;

import std;
export import :result;

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


constexpr bool is_separator(char32_t c) noexcept {
	if (c < 0x80) {
		return c == U',' || c == U'.' ||
               c == U';' || c == U':' || c == U'!' || c == U'?';
	}

	// 2. Unicode 范围处理：无分支二分查找 (Branchless Binary Search)
	// 拆分结构体并填充至 16 个元素（2的幂次），以利于完全展开判断
	constexpr char32_t starts[16] = {
		0x0000, // 底部哨兵
		0x1100, 0x2026, 0x2E80, 0x3001, 0x3040, 0x3100, 0x31F0, 0x3400,
		0x4E00, 0xAC00, 0xFF01, 0xFF0C, 0xFF1A, 0xFF1F,
		0xFFFFFFFF // 顶部哨兵
	};

	constexpr char32_t ends[16] = {
		0x0000,
		0x11FF, 0x2026, 0x2FDF, 0x3002, 0x30FF, 0x31BF, 0x31FF, 0x4DBF,
		0x9FFF, 0xD7AF, 0xFF01, 0xFF0C, 0xFF1B, 0xFF1F,
		0xFFFFFFFF
	};

	// 将布尔值强转为整型，消除 if 分支，直接累加目标索引 (idx)
	std::uint32_t idx = 0;
	idx |= (static_cast<std::uint32_t>(c >= starts[idx | 8]) << 3);
	idx |= (static_cast<std::uint32_t>(c >= starts[idx | 4]) << 2);
	idx |= (static_cast<std::uint32_t>(c >= starts[idx | 2]) << 1);
	idx |=  static_cast<std::uint32_t>(c >= starts[idx | 1]);

	// 最终的 idx 指向了所有满足 starts[idx] <= c 的最大索引
	return c <= ends[idx];
}

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

	[[nodiscard]] math::vec2 get_default_font_size() const noexcept{
		return default_font_size.value_or(
			glyph_size::get_glyph_std_size_at(glyph_size::standard_size, get_screen_ppi()));
	}

	[[nodiscard]] math::vec2 get_screen_ppi() const noexcept{
		return screen_ppi.value_or(glyph_size::screen_ppi);
	}

	[[nodiscard]] constexpr bool has_wrap_indicator() const noexcept{ return wrap_indicator_char != 0; }
	constexpr bool operator==(const layout_config&) const noexcept = default;
};

font::hb::font_ptr create_harfbuzz_font(const font::font_face_handle& face){
	hb_font_t* font = hb_ft_font_create_referenced(face);
	return font::hb::font_ptr{font};
}

struct layout_block{
	std::vector<glyph_elem> glyphs;
	std::vector<underline> underlines;
	std::vector<logical_cluster> clusters;
	math::vec2 cursor{};
	math::vec2 pos_min{math::vectors::constant2<float>::inf_positive_vec2};
	math::vec2 pos_max{-math::vectors::constant2<float>::inf_positive_vec2};
	float block_ascender = 0.f;
	float block_descender = 0.f;

	FORCE_INLINE inline void clear(){
		glyphs.clear();
		underlines.clear();
		clusters.clear();
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

	FORCE_INLINE inline void push_cluster(const logical_cluster& cluster){
		clusters.push_back(cluster);
	}

	FORCE_INLINE inline void push_front(math::frect glyph_region, const glyph_elem& glyph, math::vec2 glyph_advance){
		for(auto& layout_result : glyphs) layout_result.aabb.move(glyph_advance);
		for(auto& ul : underlines){
			ul.start += glyph_advance;
			ul.end += glyph_advance;
		}
		for(auto& c : clusters) c.logical_rect.move(glyph_advance);

		glyphs.insert(glyphs.begin(), glyph);
		pos_min += glyph_advance;
		pos_max += glyph_advance;
		pos_min.min(glyph_region.vert_00());
		pos_max.max(glyph_region.vert_11());
		cursor += glyph_advance;
	}
};

export struct layout_context{
private:
	struct line_buffer_t{
		std::vector<glyph_elem> elems{};
		std::vector<underline> underlines{};
		std::vector<logical_cluster> clusters{};
		layout_rect line_bound{};
		math::vec2 pos_min{math::vectors::constant2<float>::inf_positive_vec2};
		math::vec2 pos_max{-math::vectors::constant2<float>::inf_positive_vec2};

		FORCE_INLINE inline void clear(){
			elems.clear();
			underlines.clear();
			clusters.clear();
			line_bound = {};
			pos_min = math::vectors::constant2<float>::inf_positive_vec2;
			pos_max = -math::vectors::constant2<float>::inf_positive_vec2;
		}

		FORCE_INLINE inline void append(layout_block& block, float math::vec2::* major_axis) {
			math::vec2 move_vec{};
			move_vec.*major_axis = line_bound.width;

			if (!block.glyphs.empty() || !block.underlines.empty()) {
				pos_min.min(block.pos_min + move_vec);
				pos_max.max(block.pos_max + move_vec);
			}

			elems.reserve(elems.size() + block.glyphs.size());
			for (auto& g : block.glyphs) {
				g.aabb.move(move_vec);
				elems.emplace_back(std::move(g)); // 避免多余的拷贝/移动构造
			}

			underlines.reserve(underlines.size() + block.underlines.size());
			for (auto& ul : block.underlines) {
				ul.start += move_vec;
				ul.end += move_vec;
				underlines.emplace_back(std::move(ul));
			}

			clusters.reserve(clusters.size() + block.clusters.size());
			for (auto& c : block.clusters) {
				c.logical_rect.move(move_vec);
				clusters.emplace_back(std::move(c));
			}

			line_bound.width += block.cursor.*major_axis;
			line_bound.ascender = std::max(line_bound.ascender, block.block_ascender);
			line_bound.descender = std::max(line_bound.descender, block.block_descender);
		}
	};

	struct indicator_cache{
		font::glyph_borrow texture;
		math::frect glyph_aabb;
		math::vec2 advance;
	};

	struct run_metrics{
		float run_font_asc = 0.f;
		float run_font_desc = 0.f;
		float ul_position = 0.f;
		float ul_thickness = 0.f;
		math::vec2 run_scale_factor{};
		math::vec2 req_size_vec{};
		font::glyph_size_type snapped_size{};
	};

	struct ul_start_info{
		math::vec2 pos;
		typst_szt gap_count;
	};

	struct text_run_context{
		std::optional<ul_start_info> active_ul_start;
		std::optional<logical_cluster> current_logic_cluster;
		typst_szt next_apply_pos;
		math::vec2 rich_offset;
		graphic::color prev_color;
	};

	struct layout_state{
		math::vec2 min_bound{math::vectors::constant2<float>::inf_positive_vec2};
		math::vec2 max_bound{-math::vectors::constant2<float>::inf_positive_vec2};
		math::vec2 default_font_size{};
		float default_line_thickness{};
		float default_ascender{};  // 新增：默认升部
		float default_descender{}; // 新增：默认降部

		float default_space_width{};
		float prev_line_descender{};
		float current_baseline_pos{};
		bool is_first_line = true;
		hb_direction_t target_hb_dir = HB_DIRECTION_INVALID;
		float math::vec2::* major_p = &math::vec2::x;
		float math::vec2::* minor_p = &math::vec2::y;

		line_buffer_t line_buffer{};
		layout_block current_block{};
		rich_text_context rich_context;
		tokenized_text::token_iterator token_hard_last;
		tokenized_text::token_iterator token_soft_last;

		void reset(const tokenized_text& t){
			min_bound = math::vectors::constant2<float>::inf_positive_vec2;
			max_bound = -math::vectors::constant2<float>::inf_positive_vec2;
			default_font_size = {};
			prev_line_descender = {};
			current_baseline_pos = {};
			is_first_line = true;
			line_buffer.clear();
			current_block.clear();
			rich_context.clear();
			token_hard_last = t.get_init_token();
			token_soft_last = t.get_init_token();
		}
	};

	font::font_manager* manager_{font::default_font_manager};
	lru_cache<font::font_face_handle*, font::hb::font_ptr, 4> hb_cache_;
	font::hb::buffer_ptr hb_buffer_;
	std::vector<hb_feature_t> feature_stack_;
	layout_config config_{};
	layout_state state_{};
	indicator_cache cached_indicator_{};

public:
	layout_context() = default;

	explicit layout_context(std::in_place_t){
		hb_buffer_ = font::hb::make_buffer();
	}

	explicit layout_context(const layout_config& c, font::font_manager* m = font::default_font_manager)
		: manager_(m), config_(c){
		assert(manager_ != nullptr);
		hb_buffer_ = font::hb::make_buffer();
	}

#pragma region API

	[[nodiscard]] glyph_layout layout(const tokenized_text& full_text, font::font_face_view default_font_face = {}){
		glyph_layout results{};
		this->layout(results, full_text, default_font_face);
		return results;
	}

	void layout(glyph_layout& layout_ref, const tokenized_text& full_text, font::font_face_view default_font_face = {}){
		this->initialize_state(full_text, default_font_face);

		layout_ref.elems.clear();
		layout_ref.underlines.clear();
		layout_ref.clusters.clear();
		layout_ref.lines.clear();
		layout_ref.extent = {};
		layout_ref.direction = this->get_actual_direction();

        // --- 预留优化开始 ---
        const std::size_t raw_text_size = full_text.get_text().size();
		layout_ref.elems.reserve(raw_text_size);
        layout_ref.clusters.reserve(raw_text_size);
        layout_ref.lines.reserve((raw_text_size / 20) + 1); // 粗略预估行数以避免多次分配
        // --- 预留优化结束 ---

		if(this->process_layout(full_text, layout_ref)){
		}
		this->finalize(layout_ref);
	}

	bool set_max_extent(math::vec2 ext){
		if(config_.max_extent == ext) return false;
		config_.max_extent = ext;
		return true;
	}

	void set_config(const layout_config& c){ config_ = c; }
	[[nodiscard]] const layout_config& get_config() const noexcept{ return config_; }

#pragma endregion

private:
#pragma region Initialization_And_Finalization

	void initialize_state(const tokenized_text& full_text, font::font_face_view base_view){
		base_view = base_view ? base_view : manager_->use_family(manager_->get_default_family());
		state_.reset(full_text);
		feature_stack_.clear();
        feature_stack_.reserve(8); // --- 预留优化：预分配常用 OpenType feature 的空间 ---

		if(config_.direction != layout_direction::deduced){
			switch(config_.direction){
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

		if(this->is_vertical()){
			state_.major_p = &math::vec2::y;
			state_.minor_p = &math::vec2::x;
		} else{
			state_.major_p = &math::vec2::x;
			state_.minor_p = &math::vec2::y;
		}

		state_.default_font_size = config_.get_default_font_size();
		const auto snapped_base_size = layout_context::get_snapped_size_vec(state_.default_font_size);
		auto& primary_face = base_view.face();

		(void)primary_face.set_size(snapped_base_size);
		FT_Load_Char(primary_face, FT_ULong{' '}, FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING);

		const math::vec2 base_scale_factor = state_.default_font_size / snapped_base_size.as<float>();
		if(this->is_vertical()){
			state_.default_ascender = state_.default_font_size.x / 2.f;
			state_.default_descender = state_.default_font_size.x / 2.f;
			state_.default_line_thickness = font::normalize_len(primary_face->size->metrics.max_advance) * base_scale_factor.x;
			state_.default_space_width = state_.default_line_thickness;
		} else{
			// 提取字体的全局度量
			const float raw_asc = font::normalize_len(primary_face->size->metrics.ascender) * base_scale_factor.y;
			const float raw_desc = std::abs(font::normalize_len(primary_face->size->metrics.descender) * base_scale_factor.y);
			const float raw_height = raw_asc + raw_desc;

			if(raw_height > 0.001f){
				state_.default_ascender = raw_asc;
				state_.default_descender = raw_desc;
			} else{
				// 防御性后备：按标准比例 8:2 分配
				state_.default_ascender = state_.default_font_size.y * 0.8f;
				state_.default_descender = state_.default_font_size.y * 0.2f;
			}

			state_.default_line_thickness = font::normalize_len(primary_face->size->metrics.height) * base_scale_factor.y;
			state_.default_space_width = font::normalize_len(primary_face->glyph->advance.x) * base_scale_factor.x;
		}

		if(config_.has_wrap_indicator()){
			auto [face, index] = base_view.find_glyph_of(config_.wrap_indicator_char);
			if(index){
				font::glyph_identity id{index, snapped_base_size};
				font::glyph g = manager_->get_glyph_exact(*face, id);
				const auto& m = g.metrics();
				const auto advance = m.advance * base_scale_factor;
				math::vec2 adv{};
				// 提取有效步进值：借用水平宽度或自身垂直跨度
				float fallback_adv = (std::abs(advance.*state_.major_p) > 0.001f)
										 ? std::abs(advance.*state_.major_p)
										 : std::abs(advance.*state_.minor_p);

				// 严格映射四个排版方向的真实物理坐标位移
				if (state_.target_hb_dir == HB_DIRECTION_LTR) {
					adv.x = fallback_adv;
				} else if (state_.target_hb_dir == HB_DIRECTION_RTL) {
					adv.x = -fallback_adv;
				} else if (state_.target_hb_dir == HB_DIRECTION_TTB) {
					adv.y = fallback_adv;
				} else if (state_.target_hb_dir == HB_DIRECTION_BTT) {
					adv.y = -fallback_adv;
				}

				math::frect local_aabb = m.place_to({}, base_scale_factor);
				// ==========================================
				// --- 新增：将换行指示符居中对齐到它分配的网格空间中 ---
				// ==========================================
				if (state_.target_hb_dir == HB_DIRECTION_RTL) {
					// RTL: 水平向左排版，分配的格子在 [-adv, 0]，将其移入其中
					local_aabb.move({-fallback_adv, 0.f});
				} else if (state_.target_hb_dir == HB_DIRECTION_TTB) {
					// TTB: 水平居中 (将原本在[0, w]的字形移到[-w/2, w/2])
					float cx = -(local_aabb.vert_11().x + local_aabb.vert_00().x) / 2.f;
					// 垂直居中 (将字形移到 [0, adv.y] 的中心)
					float cy = fallback_adv / 2.f - (local_aabb.vert_11().y + local_aabb.vert_00().y) / 2.f;
					local_aabb.move({cx, cy});
				} else if (state_.target_hb_dir == HB_DIRECTION_BTT) {
					float cx = -(local_aabb.vert_11().x + local_aabb.vert_00().x) / 2.f;
					// BTT: 向上排版，格子在 [-adv, 0] 之间
					float cy = -fallback_adv / 2.f - (local_aabb.vert_11().y + local_aabb.vert_00().y) / 2.f;
					local_aabb.move({cx, cy});
				}

				cached_indicator_ = indicator_cache{std::move(g), local_aabb, adv};
			} else{
				const_cast<layout_config&>(config_).wrap_indicator_char = 0;
			}
		}
	}

	FORCE_INLINE inline void finalize(glyph_layout& results){
		if(results.lines.empty()){
			results.extent = {0, 0};
			return;
		}
		const auto extent = state_.max_bound - state_.min_bound;
		results.extent = extent + math::vec2{1, 1};
		for(auto& line : results.lines){
			line.start_pos -= state_.min_bound;
		}
	}

#pragma endregion

#pragma region Line_And_Block_Building

	bool advance_line(glyph_layout& results) noexcept{
		const float scale = this->get_current_relative_scale();
		const float min_asc = state_.default_ascender * scale;
		const float min_desc = state_.default_descender * scale;

		const float current_asc = std::max(min_asc, state_.line_buffer.line_bound.ascender);
		const float current_desc = std::max(min_desc, state_.line_buffer.line_bound.descender);
		float next_baseline = state_.current_baseline_pos;
		if(state_.is_first_line){
			next_baseline = current_asc;
		} else{
			const float metrics_sum = state_.prev_line_descender + current_asc;
			next_baseline += metrics_sum * config_.line_spacing_scale + config_.line_spacing_fixed_distance;
		}

		if(!config_.max_extent.is_any_inf()){
			const float container_width = config_.max_extent.*state_.major_p;
			if(state_.line_buffer.line_bound.width > container_width + 0.001f) return false;
		}

		math::vec2 offset_vec{};
		offset_vec.*state_.major_p = {};
		offset_vec.*state_.minor_p = next_baseline;

		if(!state_.line_buffer.elems.empty()){
			const float visual_min_y = state_.line_buffer.pos_min.*state_.minor_p + offset_vec.*state_.minor_p;
			const float visual_max_y = state_.line_buffer.pos_max.*state_.minor_p + offset_vec.*state_.minor_p;
			const float logical_min_y = offset_vec.*state_.minor_p - current_asc;
			const float logical_max_y = offset_vec.*state_.minor_p + current_desc;

			const float line_min_y = std::min(visual_min_y, logical_min_y);
			const float line_max_y = std::max(visual_max_y, logical_max_y);
			const float global_min_y = std::min(state_.min_bound.*state_.minor_p, line_min_y);
			const float global_max_y = std::max(state_.max_bound.*state_.minor_p, line_max_y);
			if(!std::isinf(config_.max_extent.*state_.minor_p) && (global_max_y - global_min_y) > config_.max_extent.*state_.minor_p + 0.001f){
				return false;
			}
		} else if(!config_.max_extent.is_any_inf()){
			if(next_baseline + current_desc > config_.max_extent.*state_.minor_p) return false;
		}

		state_.current_baseline_pos = next_baseline;
		state_.is_first_line = false;

		line new_line;
		new_line.start_pos = offset_vec;
		new_line.rect = state_.line_buffer.line_bound;
		new_line.glyph_range = mo_yanxi::typesetting::subrange(results.elems.size(), state_.line_buffer.elems.size());
		new_line.underline_range = mo_yanxi::typesetting::subrange(results.underlines.size(), state_.line_buffer.underlines.size());
		new_line.cluster_range = mo_yanxi::typesetting::subrange(results.clusters.size(), state_.line_buffer.clusters.size());

		for(auto& elem : state_.line_buffer.elems){
			state_.min_bound.min(elem.aabb.vert_00() + offset_vec);
			state_.max_bound.max(elem.aabb.vert_11() + offset_vec);
			results.elems.push_back(std::move(elem));
		}

		for(auto& ul : state_.line_buffer.underlines){
			math::vec2 ul_min = math::min(ul.start, ul.end) + offset_vec;
			math::vec2 ul_max = math::max(ul.start, ul.end) + offset_vec;
			ul_min.y -= ul.thickness / 2.f;
			ul_max.y += ul.thickness / 2.f;

			state_.min_bound.min(ul_min);
			state_.max_bound.max(ul_max);
			results.underlines.push_back(std::move(ul));
		}

		math::vec2 line_logical_min = offset_vec;
		line_logical_min.*state_.minor_p = offset_vec.*state_.minor_p - current_asc;
		line_logical_min.*state_.major_p = offset_vec.*state_.major_p;

		math::vec2 line_logical_max = offset_vec;
		line_logical_max.*state_.minor_p = offset_vec.*state_.minor_p + current_desc;
		line_logical_max.*state_.major_p = offset_vec.*state_.major_p + state_.line_buffer.line_bound.width;

		state_.min_bound.min(line_logical_min);
		state_.max_bound.max(line_logical_max);

		for(auto& c : state_.line_buffer.clusters){
			results.clusters.push_back(std::move(c));
		}

		results.lines.push_back(std::move(new_line));
		state_.prev_line_descender = current_desc;
		state_.line_buffer.clear();

		return true;
	}

	bool flush_block(glyph_layout& results){
		if(state_.current_block.glyphs.empty() && state_.current_block.underlines.empty() && state_.current_block.clusters.empty()) return true;

		auto check_block_fit = [&](const line_buffer_t& buffer, const layout_block& blk) -> bool{
			const float predicted_width = buffer.line_bound.width + blk.cursor.*state_.major_p;
			if(!std::isinf(config_.max_extent.*state_.major_p) && predicted_width > config_.max_extent.*state_.major_p + 0.001f){
				return false;
			}

			const float scale = this->get_current_relative_scale();
			const float min_asc = state_.default_ascender * scale;
			const float min_desc = state_.default_descender * scale;

			float asc = std::max({buffer.line_bound.ascender, blk.block_ascender, min_asc});
			float desc = std::max({buffer.line_bound.descender, blk.block_descender, min_desc});
			float estimated_baseline = state_.current_baseline_pos;
			if(!state_.is_first_line && buffer.elems.empty()){
				estimated_baseline += (state_.prev_line_descender + asc) * config_.line_spacing_scale + config_.line_spacing_fixed_distance;
			}

			if(!std::isinf(config_.max_extent.*state_.minor_p)){
				const float predicted_bottom = estimated_baseline + desc;
				const float predicted_top = estimated_baseline - asc;
				if((std::max(state_.max_bound.*state_.minor_p, predicted_bottom) -
						std::min(state_.min_bound.*state_.minor_p, predicted_top)) > config_.max_extent.*state_.minor_p + 0.001f){
					return false;
				}
			}
			return true;
		};

		if(check_block_fit(state_.line_buffer, state_.current_block)){
			state_.line_buffer.append(state_.current_block, state_.major_p);
			state_.current_block.clear();
			return true;
		}

		if(!this->advance_line(results)) return false;

		if(config_.has_wrap_indicator()){
			const float scale = this->get_current_relative_scale();
			auto scaled_aabb = cached_indicator_.glyph_aabb.copy();
			scaled_aabb = { tags::unchecked, tags::from_vertex, scaled_aabb.vert_00() * scale, scaled_aabb.vert_11() * scale };
			state_.current_block.push_front(
				scaled_aabb,
				{ scaled_aabb.copy().expand(font::font_draw_expand), graphic::colors::white * .68f, cached_indicator_.texture },
				cached_indicator_.advance * scale
			);
		}

		if(check_block_fit(state_.line_buffer, state_.current_block)){
			state_.line_buffer.append(state_.current_block, state_.major_p);
		} else{
			return false;
		}

		state_.current_block.clear();
		return true;
	}

#pragma endregion

#pragma region Shaping_And_Metrics

	run_metrics calculate_metrics(const font::font_face_handle& face) const{
		run_metrics m;
		m.req_size_vec = (state_.rich_context.get_size(state_.default_font_size) * config_.throughout_scale).max({1, 1});
		m.snapped_size = layout_context::get_snapped_size_vec(m.req_size_vec);
		m.run_scale_factor = m.req_size_vec / m.snapped_size.as<float>();

		auto ft_face = face.get();
		(void)face.set_size(m.snapped_size);

		if(this->is_vertical()){
			m.run_font_asc = m.req_size_vec.x / 2.f;
			m.run_font_desc = m.req_size_vec.x / 2.f;
		} else{
			const float raw_asc = font::normalize_len(ft_face->size->metrics.ascender) * m.run_scale_factor.y;
			const float raw_desc = std::abs(font::normalize_len(ft_face->size->metrics.descender) * m.run_scale_factor.y);
			const float raw_height = raw_asc + raw_desc;

			if(raw_height > 0.001f){
				const float em_scale = m.req_size_vec.y / raw_height;
				m.run_font_asc = raw_asc * em_scale;
				m.run_font_desc = raw_desc * em_scale;
			} else{
				m.run_font_asc = m.req_size_vec.y * 0.8f;
				m.run_font_desc = m.req_size_vec.y * 0.2f;
			}
		}

		if(ft_face->units_per_EM != 0){
			const float em_scale = m.req_size_vec.y / static_cast<float>(ft_face->units_per_EM);
			m.ul_position = this->is_vertical()
				                ? static_cast<float>(ft_face->descender) * em_scale
				                : static_cast<float>(ft_face->underline_position) * em_scale;
			m.ul_thickness = static_cast<float>(ft_face->underline_thickness) * em_scale;
		}

		if(m.ul_thickness <= 0.0f) m.ul_thickness = std::max(1.0f, m.req_size_vec.y / 14.0f);
		else m.ul_thickness = std::max(m.ul_thickness, 1.0f);
		if(m.ul_position == 0.0f) m.ul_position = -m.req_size_vec.y / 10.0f;

		return m;
	}

#pragma endregion

#pragma region Glyph_Processing_Pipeline

	void sync_rich_text(const tokenized_text& full_text, typst_szt target_cluster, text_run_context& ctx){
		for(typst_szt p = ctx.next_apply_pos; p <= target_cluster; ++p){
			auto tokens = full_text.get_token_group(p, state_.token_soft_last);
			if(!tokens.empty()){
				state_.rich_context.update(
					*manager_,
					{
						state_.default_font_size, state_.current_block.block_ascender,
						state_.current_block.block_descender, this->is_vertical()
					},
					tokens, context_update_mode::soft_only
				);
				state_.token_soft_last = tokens.end();
			}
		}
		ctx.rich_offset = state_.rich_context.get_offset();
		ctx.next_apply_pos = target_cluster + 1;
	}

	typst_szt get_current_gap_index(bool is_delimiter) const noexcept{
		const auto size = state_.line_buffer.elems.size() + state_.current_block.glyphs.size();
		return (is_delimiter && size > 0) ? size - 1 : size;
	}

	void submit_underline(const ul_start_info& start_info, const text_run_context& ctx, const run_metrics& metrics,
		bool is_delimiter){
		math::vec2 offset_vec{};
		offset_vec.*state_.minor_p = -metrics.ul_position;
		underline ul;
		ul.start = start_info.pos + offset_vec;
		ul.end = state_.current_block.cursor + offset_vec;
		ul.thickness = metrics.ul_thickness;
		ul.color = ctx.prev_color;
		ul.start_gap_count = start_info.gap_count;
		ul.end_gap_count = std::max(ul.start_gap_count, this->get_current_gap_index(is_delimiter));

		state_.current_block.push_back_underline(ul);
	}

	void submit_cluster(text_run_context& ctx){
		if(ctx.current_logic_cluster){
			state_.current_block.push_cluster(*ctx.current_logic_cluster);
			ctx.current_logic_cluster.reset();
		}
	}

	bool process_text_run(const tokenized_text& full_text, glyph_layout& results, typst_szt start, typst_szt length,
		font::font_face_handle& face){
		hb_buffer_clear_contents(hb_buffer_.get());
		hb_buffer_add_utf32(hb_buffer_.get(), reinterpret_cast<const std::uint32_t*>(full_text.get_text().data()),
			static_cast<int>(full_text.get_text().size()), static_cast<unsigned int>(start), static_cast<int>(length));
		hb_buffer_set_direction(hb_buffer_.get(), state_.target_hb_dir);
		hb_buffer_guess_segment_properties(hb_buffer_.get());
		const run_metrics metrics = this->calculate_metrics(face);
		hb_shape(this->get_hb_font(&face), hb_buffer_.get(), feature_stack_.data(), feature_stack_.size());

		unsigned int len;
		hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(hb_buffer_.get(), &len);
		hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(hb_buffer_.get(), &len);

		text_run_context ctx;
		ctx.next_apply_pos = start;

        // --- 预留优化开始 ---
        // 基于当前 run 产生的确切字形数量提前扩容，杜绝循环内部 realloc
        state_.current_block.glyphs.reserve(state_.current_block.glyphs.size() + len);
        state_.current_block.clusters.reserve(state_.current_block.clusters.size() + len);
        // --- 预留优化结束 ---

		for(unsigned int i = 0; i < len; ++i){
			const font::glyph_index_t gid = infos[i].codepoint;
			const auto current_cluster = infos[i].cluster;
			const auto ch = full_text.get_text()[current_cluster];
			const bool is_delimiter = (ch == U' ' || ch == U'\t' || ch == U'\r' || ch == U'\n' || mo_yanxi::typesetting::is_separator(ch));
			ctx.prev_color = state_.rich_context.get_color();
			const bool was_ul = state_.rich_context.is_underline_enabled();

			this->sync_rich_text(full_text, current_cluster, ctx);

			const bool is_ul = state_.rich_context.is_underline_enabled();
			if(was_ul && !is_ul && ctx.active_ul_start){
				this->submit_underline(*ctx.active_ul_start, ctx, metrics, is_delimiter);
				ctx.active_ul_start.reset();
			}

			if(is_ul && !ctx.active_ul_start){
				ctx.active_ul_start = ul_start_info{state_.current_block.cursor, this->get_current_gap_index(is_delimiter)};
			}

			if(is_delimiter){
				if(ctx.active_ul_start){
					this->submit_underline(*ctx.active_ul_start, ctx, metrics, true);
					ctx.active_ul_start.reset();
				}
				this->submit_cluster(ctx);

				if(!this->flush_block(results)) return false;

				if(ch != U'\n' && ch != U'\r' && state_.rich_context.is_underline_enabled()){
					ctx.active_ul_start = ul_start_info{state_.current_block.cursor, this->get_current_gap_index(true)};
				}
			}

			const font::glyph loaded_glyph = manager_->get_glyph_exact(face, {gid, metrics.snapped_size});
			const float asc = this->is_vertical()
				                  ? (loaded_glyph.metrics().advance.x * metrics.run_scale_factor.x / 2.f)
				                  : (loaded_glyph.metrics().ascender() * metrics.run_scale_factor.y);
			const float desc = this->is_vertical()
				                   ? asc
				                   : (loaded_glyph.metrics().descender() * metrics.run_scale_factor.y);
			const math::vec2 run_advance{
					font::normalize_len(pos[i].x_advance) * metrics.run_scale_factor.x,
					font::normalize_len(pos[i].y_advance) * metrics.run_scale_factor.y
				};

			if(ch == U'\r'){
				this->submit_cluster(ctx);
				state_.current_block.block_ascender = std::max(state_.current_block.block_ascender, metrics.run_font_asc);
				state_.current_block.block_descender = std::max(state_.current_block.block_descender, metrics.run_font_desc);
				const math::vec2 logical_base = state_.current_block.cursor + ctx.rich_offset;
				const math::frect r_rect = this->is_vertical()
					? math::frect{ tags::from_extent, {logical_base.x - metrics.run_font_asc, logical_base.y}, {metrics.run_font_asc + metrics.run_font_desc, -1.f} }
					: math::frect{ tags::from_extent, {logical_base.x, logical_base.y - metrics.run_font_asc}, {1.f, metrics.run_font_asc + metrics.run_font_desc} };
				state_.current_block.push_cluster(logical_cluster{current_cluster, 1, r_rect});

				if(config_.line_feed_type == linefeed_type::crlf){
					state_.line_buffer.line_bound.width = {};
					ctx.active_ul_start.reset();
				}
				continue;
			} else if(ch == U'\n'){
				this->submit_cluster(ctx);
				state_.current_block.block_ascender = std::max(state_.current_block.block_ascender, metrics.run_font_asc);
				state_.current_block.block_descender = std::max(state_.current_block.block_descender, metrics.run_font_desc);
				const math::vec2 logical_base = state_.current_block.cursor + ctx.rich_offset;
				const math::frect n_rect = this->is_vertical()
					? math::frect{ tags::from_extent, {logical_base.x - metrics.run_font_asc, logical_base.y}, {metrics.run_font_asc + metrics.run_font_desc, -1.f} }
					: math::frect{ tags::from_extent, {logical_base.x, logical_base.y - metrics.run_font_asc}, {1.f, metrics.run_font_asc + metrics.run_font_desc} };
				state_.current_block.push_cluster(logical_cluster{current_cluster, 1, n_rect});

				if(!this->flush_block(results) || !this->advance_line(results)) return false;
				state_.current_block.cursor = {};
				if(config_.line_feed_type == linefeed_type::crlf) state_.current_block.cursor.*state_.minor_p = 0;
				ctx.active_ul_start.reset();
				continue;
			} else if(ch == U'\t'){
				this->submit_cluster(ctx);
				state_.current_block.block_ascender = std::max(state_.current_block.block_ascender, metrics.run_font_asc);
				state_.current_block.block_descender = std::max(state_.current_block.block_descender, metrics.run_font_desc);

				math::vec2 old_cursor = state_.current_block.cursor;
				const float tab_step = config_.tab_scale * this->get_scaled_default_space_width();
				if(tab_step > std::numeric_limits<float>::epsilon()){
					const float current_pos = state_.current_block.cursor.*state_.major_p;
					state_.current_block.cursor.*state_.major_p = (std::floor(current_pos / tab_step) + 1) * tab_step;
				}

				math::vec2 step_advance{};
				step_advance.*state_.major_p = std::abs(state_.current_block.cursor.*state_.major_p - old_cursor.*state_.major_p);
				const math::vec2 logical_base = old_cursor + ctx.rich_offset;
				const math::frect tab_rect = this->is_vertical()
					? math::frect{ tags::from_extent, {logical_base.x - metrics.run_font_asc, logical_base.y}, {metrics.run_font_asc + metrics.run_font_desc, -step_advance.y} }
					: math::frect{ tags::from_extent, {logical_base.x, logical_base.y - metrics.run_font_asc}, {step_advance.x, metrics.run_font_asc + metrics.run_font_desc} };
				state_.current_block.push_cluster(logical_cluster{current_cluster, 1, tab_rect});
				continue;
			} else if(ch == U' '){
				this->submit_cluster(ctx);
				state_.current_block.block_ascender = std::max(state_.current_block.block_ascender, metrics.run_font_asc);
				state_.current_block.block_descender = std::max(state_.current_block.block_descender, metrics.run_font_desc);
				const math::vec2 logical_base = state_.current_block.cursor + ctx.rich_offset;
				const math::frect space_rect = this->is_vertical()
					? math::frect{ tags::from_extent, {logical_base.x - metrics.run_font_asc, logical_base.y}, {metrics.run_font_asc + metrics.run_font_desc, -run_advance.y} }
					: math::frect{ tags::from_extent, {logical_base.x, logical_base.y - metrics.run_font_asc}, {run_advance.x, metrics.run_font_asc + metrics.run_font_desc} };
				state_.current_block.push_cluster(logical_cluster{current_cluster, 1, space_rect});
				state_.current_block.cursor = this->move_pen(state_.current_block.cursor, run_advance);
				continue;
			}

			// ==========================================
			// --- 新增：智能 Word-Wrap 与强制 Break-All ---
			// ==========================================
			const float current_word_w = std::abs(state_.current_block.cursor.*state_.major_p);
			const float line_w = std::abs(state_.line_buffer.line_bound.width);
			const float next_w = std::abs(run_advance.*state_.major_p);
			const float max_w = config_.max_extent.*state_.major_p;
			const float indicator_w = config_.has_wrap_indicator() ? std::abs(cached_indicator_.advance.*state_.major_p * this->get_current_relative_scale()) : 0.f;

            if (!std::isinf(max_w) && (line_w + current_word_w + next_w > max_w + 0.001f)) {
				// 判定 1：当前积攒的单词是否足够短，可以直接完整推移到下一行（标准 Word-Wrap）
				if (!state_.line_buffer.elems.empty() && (current_word_w + next_w + indicator_w <= max_w + 0.001f)) {
					if (!this->advance_line(results)) return false;
					if (config_.has_wrap_indicator()) {
						const float scale = this->get_current_relative_scale();
						auto scaled_aabb = cached_indicator_.glyph_aabb.copy();
						scaled_aabb = { tags::unchecked, tags::from_vertex, scaled_aabb.vert_00() * scale, scaled_aabb.vert_11() * scale };

						const math::vec2 shift_adv = cached_indicator_.advance * scale;
						state_.current_block.push_front(
							scaled_aabb,
							{ scaled_aabb.copy().expand(font::font_draw_expand), graphic::colors::white * .68f, cached_indicator_.texture },
							shift_adv
						);

						// --- 新增修复：同步平移挂起状态 ---
						if (ctx.current_logic_cluster) {
							ctx.current_logic_cluster->logical_rect.move(shift_adv);
						}
						if (ctx.active_ul_start) {
							ctx.active_ul_start->pos += shift_adv;
						}
						// --------------------------------
					}
				} else {
					// 判定 2：单词本身长于整行最大宽度，或者当前已经在行首，必须强制截断 (Break-All)
					if (!state_.current_block.glyphs.empty()) {
						if (ctx.active_ul_start) {
							this->submit_underline(*ctx.active_ul_start, ctx, metrics, false);
							ctx.active_ul_start.reset();
						}
						this->submit_cluster(ctx);

						state_.line_buffer.append(state_.current_block, state_.major_p);
						state_.current_block.clear();
						if (!this->advance_line(results)) return false;

						if (config_.has_wrap_indicator()) {
							const float scale = this->get_current_relative_scale();
							auto scaled_aabb = cached_indicator_.glyph_aabb.copy();
							scaled_aabb = { tags::unchecked, tags::from_vertex, scaled_aabb.vert_00() * scale, scaled_aabb.vert_11() * scale };
							state_.current_block.push_front(
								scaled_aabb,
								{ scaled_aabb.copy().expand(font::font_draw_expand), graphic::colors::white * .68f, cached_indicator_.texture },
								cached_indicator_.advance * scale
							);
						}

						if (state_.rich_context.is_underline_enabled()) {
							ctx.active_ul_start = ul_start_info{state_.current_block.cursor, this->get_current_gap_index(false)};
						}
					}
				}
			}
			// ==========================================
			// --- 断块逻辑结束 ---
			// ==========================================

			if(ch != U'\n' && ch != U'\r'){
				state_.current_block.block_ascender = std::max(state_.current_block.block_ascender, std::max(asc, metrics.run_font_asc));
				state_.current_block.block_descender = std::max(state_.current_block.block_descender, std::max(desc, metrics.run_font_desc));
			}

			if(this->is_reversed()) state_.current_block.cursor = this->move_pen(state_.current_block.cursor, run_advance);

			const math::vec2 logical_base = state_.current_block.cursor + ctx.rich_offset;
			const math::frect advance_rect = this->is_vertical()
				                                 ? math::frect{
					                                 tags::from_extent,
					                                 {logical_base.x - metrics.run_font_asc, logical_base.y},
					                                 {metrics.run_font_asc + metrics.run_font_desc, -run_advance.y}
				                                 }
				                                 : math::frect{
					                                 tags::from_extent,
					                                 {logical_base.x, logical_base.y - metrics.run_font_asc},
					                                 {run_advance.x, metrics.run_font_asc + metrics.run_font_desc}
				                                 };
			const auto next_cluster = (i + 1 < len) ? infos[i + 1].cluster : (start + length);
			const auto span = next_cluster > current_cluster ? next_cluster - current_cluster : 1;

			if(ctx.current_logic_cluster && ctx.current_logic_cluster->cluster_index == current_cluster){
				ctx.current_logic_cluster->logical_rect.expand_by(advance_rect);
				ctx.current_logic_cluster->cluster_span = std::max(ctx.current_logic_cluster->cluster_span, span);
			} else{
				this->submit_cluster(ctx);
				ctx.current_logic_cluster = logical_cluster{current_cluster, span, advance_rect};
			}

			const math::vec2 glyph_local_draw_pos = {
					font::normalize_len(pos[i].x_offset) * metrics.run_scale_factor.x + ctx.rich_offset.x,
					-font::normalize_len(pos[i].y_offset) * metrics.run_scale_factor.y + ctx.rich_offset.y
				};

			const math::vec2 visual_base_pos = state_.current_block.cursor + glyph_local_draw_pos;
			const math::frect actual_aabb = loaded_glyph.metrics().place_to(visual_base_pos, metrics.run_scale_factor);
			const math::frect draw_aabb = actual_aabb.copy().expand(font::font_draw_expand * metrics.run_scale_factor);

			state_.current_block.push_back(actual_aabb, {
					draw_aabb, state_.rich_context.get_color(), std::move(loaded_glyph)
				});
			if(!this->is_reversed()) state_.current_block.cursor = this->move_pen(state_.current_block.cursor, run_advance);
		}

		this->submit_cluster(ctx);

		if(ctx.active_ul_start){
			this->submit_underline(*ctx.active_ul_start, ctx, metrics, true);
		}

		if(ctx.next_apply_pos < start + length){
			for(auto p = ctx.next_apply_pos; p < start + length; ++p){
				auto tokens = full_text.get_token_group(p, full_text.get_init_token());
				if(!tokens.empty()){
					state_.rich_context.update(*manager_,
						{
							state_.default_font_size, state_.current_block.block_ascender,
							state_.current_block.block_descender, this->is_vertical()
						},
						tokens, typesetting::context_update_mode::soft_only);
					state_.token_soft_last = tokens.end();
				}
			}
		}

		return true;
	}

	bool process_layout(const tokenized_text& full_text, glyph_layout& results){
		typst_szt current_idx = 0;
		typst_szt run_start = 0;
		font::font_face_handle* current_face = nullptr;

		auto resolve_face = [&](font::font_face_handle* current, char32_t codepoint) -> font::font_face_handle*{
			if(current && current->index_of(codepoint)) return current;
			const auto* family = &state_.rich_context.get_font(manager_->get_default_family());
			auto face_view = manager_->use_family(family);
			auto [best_face, _] = face_view.find_glyph_of(codepoint);
			return best_face;
		};

		while(current_idx < full_text.get_text().size()){
			auto tokens = full_text.get_token_group(current_idx, state_.token_hard_last);
			state_.token_hard_last = tokens.end();
			if(check_token_group_need_another_run(tokens)){
				if(current_face && current_idx > run_start){
					if(!this->process_text_run(full_text, results, run_start, current_idx - run_start, *current_face)) return false;
				}

				state_.rich_context.update(
					*manager_,
					{
						state_.default_font_size, state_.current_block.block_ascender,
						state_.current_block.block_descender, this->is_vertical()
					},
					tokens, context_update_mode::hard_only,
					[&](hb_feature_t f){
						f.start = static_cast<unsigned int>(current_idx);
						f.end = HB_FEATURE_GLOBAL_END;
						feature_stack_.push_back(f);
					},
					[&](unsigned to_close){
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

				if(!this->flush_block(results)) return false;
			}

			const auto codepoint = full_text.get_text()[current_idx];
			font::font_face_handle* best_face = resolve_face(current_face, codepoint);
			if(!current_face){
				current_face = best_face;
			} else if(best_face != current_face){
				if(!this->process_text_run(full_text, results, run_start, current_idx - run_start, *current_face)) return false;
				current_face = best_face;
				run_start = current_idx;
			}
			current_idx++;
		}

		if(current_face){
			if(!this->process_text_run(full_text, results, run_start, full_text.get_text().size() - run_start, *current_face)) return false;
		}

		return this->flush_block(results) && this->advance_line(results);
	}

#pragma endregion

#pragma region Trivial_Helpers

	[[nodiscard]] FORCE_INLINE inline layout_direction get_actual_direction() const noexcept{
		if(config_.direction != layout_direction::deduced) return config_.direction;
		switch(state_.target_hb_dir){
		case HB_DIRECTION_LTR : return layout_direction::ltr;
		case HB_DIRECTION_RTL : return layout_direction::rtl;
		case HB_DIRECTION_TTB : return layout_direction::ttb;
		case HB_DIRECTION_BTT : return layout_direction::btt;
		default : return layout_direction::ltr;
		}
	}

	[[nodiscard]] FORCE_INLINE inline bool is_reversed() const noexcept{
		return state_.target_hb_dir == HB_DIRECTION_RTL || state_.target_hb_dir == HB_DIRECTION_BTT;
	}

	[[nodiscard]] FORCE_INLINE inline bool is_vertical() const noexcept{
		return state_.target_hb_dir == HB_DIRECTION_TTB || state_.target_hb_dir == HB_DIRECTION_BTT;
	}

	[[nodiscard]] FORCE_INLINE inline font::glyph_size_type get_current_snapped_size() const noexcept{
		return layout_context::get_snapped_size_vec(state_.rich_context.get_size(state_.default_font_size));
	}

	[[nodiscard]] FORCE_INLINE static font::glyph_size_type get_snapped_size_vec(font::glyph_size_type v) noexcept{
		return {font::get_snapped_size(v.x), font::get_snapped_size(v.y)};
	}

	[[nodiscard]] FORCE_INLINE static font::glyph_size_type get_snapped_size_vec(math::vec2 v) noexcept{
		return layout_context::get_snapped_size_vec(v.as<font::glyph_size_type::value_type>());
	}

	[[nodiscard]] FORCE_INLINE inline hb_font_t* get_hb_font(font::font_face_handle* face) noexcept{
		if(const auto ptr = hb_cache_.get(face)) return ptr->get();
		auto new_hb_font = create_harfbuzz_font(*face);
		hb_ft_font_set_load_flags(new_hb_font.get(), FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING);
		const auto rst = new_hb_font.get();
		hb_cache_.put(face, std::move(new_hb_font));
		return rst;
	}

	[[nodiscard]] FORCE_INLINE inline math::vec2 move_pen(math::vec2 pen, math::vec2 advance) const noexcept{
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
		return state_.rich_context.get_size(state_.default_font_size).y / state_.default_font_size.y;
	}

	[[nodiscard]] FORCE_INLINE inline float get_scaled_default_line_thickness() const noexcept{
		return state_.default_line_thickness * this->get_current_relative_scale();
	}

	[[nodiscard]] FORCE_INLINE inline float get_scaled_default_space_width() const noexcept{
		return state_.default_space_width * this->get_current_relative_scale();
	}

#pragma endregion
};
}
