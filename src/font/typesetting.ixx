module;

#include <hb.h>
#include <hb-ft.h>
#include <freetype/freetype.h>
#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.typesetting;

import std;
export import :misc;

export import mo_yanxi.typesetting.rich_text;
export import mo_yanxi.graphic.color;
import mo_yanxi.font;
import mo_yanxi.font.manager;
import mo_yanxi.typesetting.util;
import mo_yanxi.hb.wrap;
import mo_yanxi.math;
import mo_yanxi.graphic.image_region;
import mo_yanxi.math.vector2;
import mo_yanxi.cache;
import mo_yanxi.cond_exist;

namespace mo_yanxi::typesetting{
struct indicator_cache{
	font::glyph_borrow texture;
	math::frect glyph_aabb;
	math::vec2 advance;
};

struct run_metrics{
	float run_font_asc;
	float run_font_desc;
	float ul_position;
	float ul_thickness;
	math::vec2 req_size_vec;
	font::glyph_size_type snapped_size;
	math::vec2 run_scale_factor;
};

struct ul_start_info{
	math::vec2 pos;
	typst_szt gap_count;
};

export
struct layout_ctx_base{
protected:
	font::font_manager* manager_{font::default_font_manager};
	lru_cache<font::font_face_handle*, font::hb::font_ptr, 4> hb_cache_;
	font::hb::buffer_ptr hb_buffer_;
	std::vector<hb_feature_t> feature_stack_;
	layout_state_t state_{};
	indicator_cache cached_indicator_{};

	layout_buffer layout_buffer_{};

public:
	layout_ctx_base() = default;

	explicit layout_ctx_base(std::in_place_t){
		hb_buffer_ = font::hb::make_buffer();
	}

	explicit layout_ctx_base(font::font_manager& m)
		: manager_(&m){
		hb_buffer_ = font::hb::make_buffer();
	}

	[[nodiscard]] FORCE_INLINE inline layout_direction
	get_actual_direction(const layout_config& config_) const noexcept{
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

	[[nodiscard]] FORCE_INLINE inline hb_font_t* get_hb_font(font::font_face_handle* face) noexcept{
		if(const auto ptr = hb_cache_.get(face)) return ptr->get();
		auto new_hb_font = create_harfbuzz_font(*face);
		hb_ft_font_set_load_flags(new_hb_font.get(), FT_LOAD_NO_HINTING);
		const auto rst = new_hb_font.get();
		hb_cache_.put(face, std::move(new_hb_font));
		return rst;
	}

protected:
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

	FORCE_INLINE inline void finalize(glyph_layout& results) const{
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
};

struct run_style_state{
	font::font_face_handle* face = nullptr;
	bool synthetic_italic = false;
	bool synthetic_bold = false;

	bool operator!=(const run_style_state& rhs) const noexcept{
		return face != rhs.face ||
			synthetic_italic != rhs.synthetic_italic ||
			synthetic_bold != rhs.synthetic_bold;
	}
};

export
template <
	concepts::ClusterPolicy ClusterPol = policies::store_clusters,
	concepts::RichTextPolicy<layout_state_t> RichTextPol = policies::rich_text_enabled>
class layout_context_impl : public layout_ctx_base{
public:
	using layout_ctx_base::layout_ctx_base;

	//used for heap resource reuse
	[[nodiscard]] explicit(false) layout_context_impl(layout_ctx_base&& base) noexcept
		: layout_ctx_base(std::move(base)){
	}

private:
	static constexpr bool enable_cluster_record = ClusterPol::enabled;
	static constexpr bool enable_dynamic_rich_text_state = RichTextPol::enabled;

	// --- 零开销挂载点 ---
	ADAPTED_NO_UNIQUE_ADDRESS ClusterPol cluster_policy_;
	ADAPTED_NO_UNIQUE_ADDRESS RichTextPol rich_text_policy_;
	ADAPTED_NO_UNIQUE_ADDRESS cond_exist<rich_text_state, enable_dynamic_rich_text_state> rich_text_state_;

#pragma region Cluster
	// 现在需要传入 results 引用
	FORCE_INLINE inline void cluster_reserve_(glyph_layout& results, std::size_t sz){
		cluster_policy_.reserve(results, sz);
	}

	FORCE_INLINE inline void cluster_push_(glyph_layout& results, const logical_cluster& cluster){
		cluster_policy_.push(results, cluster);
	}
#pragma endregion

#pragma region RichTextStateGetter
	FORCE_INLINE inline constexpr bool is_italic_enabled_(const layout_config& config_) const noexcept{
		if constexpr(enable_dynamic_rich_text_state){
			return rich_text_state_->rich_context.is_italic_enabled(config_.rich_text_fallback_style);
		} else{
			return config_.rich_text_fallback_style.enables_italic;
		}
	}

	FORCE_INLINE inline constexpr bool is_bold_enabled_(const layout_config& config_) const noexcept{
		if constexpr(enable_dynamic_rich_text_state){
			return rich_text_state_->rich_context.is_bold_enabled(config_.rich_text_fallback_style);
		} else{
			return config_.rich_text_fallback_style.enables_bold;
		}
	}

	FORCE_INLINE inline constexpr math::vec2 get_font_size_() const noexcept{
		return rich_text_policy_.get_size(rich_text_state_, state_);
	}

	FORCE_INLINE inline constexpr bool is_underline_enabled_(const layout_config& config_) const noexcept{
		return rich_text_policy_.is_underline_enabled(rich_text_state_, config_);
	}


	FORCE_INLINE inline constexpr graphic::color get_font_color_(const layout_config& config_) const noexcept{
		return rich_text_policy_.get_color(rich_text_state_, config_);
	}

	FORCE_INLINE inline constexpr math::vec2 get_font_offset_(const layout_config& config_) const noexcept{
		return rich_text_policy_.get_rich_offset(rich_text_state_, config_);
	}

	FORCE_INLINE inline void rich_text_sync_(const tokenized_text_view& full_text, const layout_config& config_,
		std::uint32_t current_cluster){
		if constexpr(enable_dynamic_rich_text_state){
			rich_text_policy_.sync(rich_text_state_, layout_buffer_, state_, config_, *manager_, full_text,
				current_cluster);
		}
	}

	FORCE_INLINE inline const font::font_family* get_font_view_(const layout_config& config_) const noexcept{
		if constexpr(enable_dynamic_rich_text_state){
			const rich_text_state& state = rich_text_state_;
			return &state.rich_context.get_font(config_.rich_text_fallback_style, manager_->get_default_family());
		} else{
			return config_.rich_text_fallback_style.family
				       ? config_.rich_text_fallback_style.family
				       : manager_->get_default_family();
		}
	}

#pragma endregion

	FORCE_INLINE glyph_elem get_line_indicator_elem(math::frect font_bound) const noexcept{
		return {font_bound.expand(font::font_draw_expand), graphic::colors::white * .68f, cached_indicator_.texture};
	}

	void submit_underline(glyph_layout& results, const ul_start_info& start_info, graphic::color ctx_color,
		const run_metrics& metrics, bool is_delimiter){
		math::vec2 offset_vec{};
		offset_vec.*state_.minor_p = -metrics.ul_position;
		underline ul;
		ul.start = start_info.pos + offset_vec;
		ul.end = layout_buffer_.cursor + offset_vec;
		ul.thickness = metrics.ul_thickness;
		ul.color = ctx_color;
		ul.start_gap_count = start_info.gap_count;
		ul.end_gap_count = std::max(ul.start_gap_count, this->get_current_gap_index(results, is_delimiter));

		results.underlines.push_back(ul);

		// 由于移除了 push_back_underline，这里手动更新当前 block 的包围盒
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
		layout_buffer_.block_pos_min.min(ul_min);
		layout_buffer_.block_pos_max.max(ul_max);
	}

	typst_szt get_current_gap_index(const glyph_layout& results, bool is_delimiter) const noexcept{
		const auto size = results.elems.size() - layout_buffer_.line_span.elem_start;
		return (is_delimiter && size > 0) ? size - 1 : size;
	}

	run_metrics calculate_metrics(const layout_config& config_, const font::font_face_handle& face) const noexcept{
		run_metrics m{};
		m.req_size_vec = (get_font_size_() * config_.throughout_scale).max({1, 1});
		m.snapped_size = get_snapped_size_vec(m.req_size_vec);
		m.run_scale_factor = m.req_size_vec / m.snapped_size.as<float>();

		auto ft_face = face.get();
		(void)face.set_size(m.snapped_size);

		if(this->is_vertical()){
			m.run_font_asc = m.req_size_vec.x / 2.f;
			m.run_font_desc = m.req_size_vec.x / 2.f;
		} else{
			const float raw_asc = font::normalize_len(ft_face->size->metrics.ascender) * m.run_scale_factor.y;
			const float raw_desc = std::abs(
				font::normalize_len(ft_face->size->metrics.descender) * m.run_scale_factor.y);
			const float raw_height = raw_asc + raw_desc;

			if(raw_height > 0.001f){
				const float em_scale = m.req_size_vec.y / raw_height;
				m.run_font_asc = raw_asc * em_scale;
				m.run_font_desc = raw_desc * em_scale;
			} else{
				m.run_font_asc = m.req_size_vec.y * 0.88f;
				m.run_font_desc = m.req_size_vec.y * 0.12f;
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

	void initialize_state(const tokenized_text_view& full_text, const layout_config& config_){
		assert(manager_ != nullptr);
		assert(hb_buffer_ != nullptr);
		auto base_view = manager_->use_family(config_.rich_text_fallback_style.family
			                                      ? config_.rich_text_fallback_style.family
			                                      : manager_->get_default_family());
		if(base_view.view.empty()){
			throw std::runtime_error{"failed to find a usable font family"};
		}

		state_.reset();
		layout_buffer_.clear();

		if constexpr(enable_dynamic_rich_text_state){
			rich_text_state& rtstate = rich_text_state_;
			rtstate.reset(full_text);
		}

		feature_stack_.clear();
		feature_stack_ = {
				config_.rich_text_fallback_style.features.begin(), config_.rich_text_fallback_style.features.end()
			};
		feature_stack_.reserve(8);

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
			state_.is_vertical_mode = true;
		} else{
			state_.major_p = &math::vec2::x;
			state_.minor_p = &math::vec2::y;
			state_.is_vertical_mode = false;
		}

		state_.default_font_size = config_.get_default_font_size();
		const auto snapped_base_size = get_snapped_size_vec(state_.default_font_size);
		auto& primary_face = base_view.view.face();

		(void)primary_face.set_size(snapped_base_size);
		FT_Load_Char(primary_face, FT_ULong{' '}, FT_LOAD_NO_HINTING);
		const math::vec2 base_scale_factor = state_.default_font_size / snapped_base_size.as<float>();

		if(this->is_vertical()){
			state_.default_ascender = state_.default_font_size.x / 2.f;
			state_.default_descender = state_.default_font_size.x / 2.f;
			state_.default_line_thickness = font::normalize_len(primary_face->size->metrics.max_advance) *
				base_scale_factor.x;
			state_.default_space_width = state_.default_line_thickness;
		} else{
			const float raw_asc = font::normalize_len(primary_face->size->metrics.ascender) * base_scale_factor.y;
			const float raw_desc = std::abs(
				font::normalize_len(primary_face->size->metrics.descender) * base_scale_factor.y);
			const float raw_height = raw_asc + raw_desc;

			if(raw_height > 0.001f){
				state_.default_ascender = raw_asc;
				state_.default_descender = raw_desc;
			} else{
				state_.default_ascender = state_.default_font_size.y * 0.88f;
				state_.default_descender = state_.default_font_size.y * 0.12f;
			}

			state_.default_line_thickness = font::normalize_len(primary_face->size->metrics.height) * base_scale_factor.
				y;
			state_.default_space_width = font::normalize_len(primary_face->glyph->advance.x) * base_scale_factor.x;
		}

		if(config_.has_wrap_indicator()){
			auto [face, index] = base_view.view.find_glyph_of(config_.wrap_indicator_char);
			if(index){
				font::glyph_identity id{index, snapped_base_size};
				font::glyph g = manager_->get_glyph_exact(*face, id);
				const auto& m = g.metrics();
				const auto advance = m.advance * base_scale_factor;
				math::vec2 adv{};
				const float fallback_adv = (std::abs(advance.*state_.major_p) > 0.001f)
					                           ? std::abs(advance.*state_.major_p)
					                           : std::abs(advance.*state_.minor_p);

				adv.*state_.major_p = is_reversed() ? -fallback_adv : fallback_adv;


				math::frect local_aabb = m.place_to({}, base_scale_factor);
				if(state_.target_hb_dir == HB_DIRECTION_RTL){
					local_aabb.move({-fallback_adv, 0.f});
				} else if(state_.target_hb_dir == HB_DIRECTION_TTB){
					const auto cx = -(local_aabb.vert_11().x + local_aabb.vert_00().x) / 2.f;
					const auto cy = fallback_adv / 2.f - (local_aabb.vert_11().y + local_aabb.vert_00().y) / 2.f;
					local_aabb.move({cx, cy});
				} else if(state_.target_hb_dir == HB_DIRECTION_BTT){
					const auto cx = -(local_aabb.vert_11().x + local_aabb.vert_00().x) / 2.f;
					const auto cy = -fallback_adv / 2.f - (local_aabb.vert_11().y + local_aabb.vert_00().y) / 2.f;
					local_aabb.move({cx, cy});
				}

				// ReSharper disable once CppPossiblyUnintendedObjectSlicing
				cached_indicator_ = indicator_cache{std::move(g), local_aabb, adv};
			}
		}
	}

	// ==========================================
	// 编译期分支派发核心逻辑
	// ==========================================
	template <math::bool2 IsInf>
	bool process_text_run_impl(
		const tokenized_text_view& full_text, const layout_config& config_, glyph_layout& results,
		typst_szt start, typst_szt length,
		font::font_face_handle& face, bool synthetic_italic, bool synthetic_bold
	){
		hb_buffer_clear_contents(hb_buffer_.get());
		hb_buffer_add_utf32(hb_buffer_.get(), reinterpret_cast<const std::uint32_t*>(full_text.get_text().data()),
			static_cast<int>(full_text.get_text().size()), static_cast<unsigned int>(start), static_cast<int>(length));
		hb_buffer_set_direction(hb_buffer_.get(), state_.target_hb_dir);
		hb_buffer_guess_segment_properties(hb_buffer_.get());

		const run_metrics metrics = this->calculate_metrics(config_, face);
		::hb_shape(this->get_hb_font(&face), hb_buffer_.get(), feature_stack_.data(), feature_stack_.size());

		unsigned int len;
		hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(hb_buffer_.get(), &len);
		hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(hb_buffer_.get(), &len);

		if constexpr(enable_dynamic_rich_text_state){
			rich_text_state& rtstate = rich_text_state_;
			rtstate.next_apply_pos = start;
		}

		std::optional<ul_start_info> active_ul_start;
		std::optional<logical_cluster> current_logic_cluster;
		graphic::color prev_color = graphic::colors::white;

		results.elems.reserve(results.elems.size() + len);
		// cluster_reserve_(results, len);

		for(std::uint32_t i = 0; i < len; ++i){
			const font::glyph_index_t gid = infos[i].codepoint;
			const auto current_cluster = infos[i].cluster;
			const auto ch = full_text.get_text()[current_cluster];
			const bool is_delimiter = (ch == U' ' || ch == U'\t' || ch == U'\r' || ch == U'\n' || is_separator(ch));

			bool was_ul = false;
			bool is_ul = false;
			math::vec2 rich_offset{};

			if constexpr(enable_dynamic_rich_text_state){
				was_ul = is_underline_enabled_(config_);
				rich_text_sync_(full_text, config_, current_cluster);
				is_ul = is_underline_enabled_(config_);
			} else{
				is_ul = is_underline_enabled_(config_);
			}

			prev_color = get_font_color_(config_);
			rich_offset = get_font_offset_(config_);

			if(was_ul && !is_ul && active_ul_start){
				this->submit_underline(results, *active_ul_start, prev_color, metrics, is_delimiter);
				active_ul_start.reset();
			}

			if(is_ul && !active_ul_start){
				active_ul_start = ul_start_info{
						layout_buffer_.cursor, this->get_current_gap_index(results, is_delimiter)
					};
			}

			if(is_delimiter){
				if(active_ul_start){
					this->submit_underline(results, *active_ul_start, prev_color, metrics, true);
					active_ul_start.reset();
				}

				if(current_logic_cluster){
					cluster_push_(results, *current_logic_cluster);
					current_logic_cluster.reset();
				}

				if(!this->flush_block_impl<IsInf>(config_, results)) return false;

				if constexpr(enable_dynamic_rich_text_state){
					if(ch != U'\n' && ch != U'\r' && is_underline_enabled_(config_)){
						active_ul_start = ul_start_info{
								layout_buffer_.cursor, this->get_current_gap_index(results, true)
							};
					}
				}
			}

			const font::glyph loaded_glyph = manager_->get_glyph_exact(face, {gid, metrics.snapped_size});
			const math::vec2 run_advance{
					font::normalize_len(pos[i].x_advance) * metrics.run_scale_factor.x,
					font::normalize_len(pos[i].y_advance) * metrics.run_scale_factor.y
				};

			// 控制字符逻辑
			if(ch == U'\r'){
				if(current_logic_cluster){
					cluster_push_(results, *current_logic_cluster);
					current_logic_cluster.reset();
				}

				layout_buffer_.max_bound_height(metrics.run_font_asc, metrics.run_font_desc);
				const math::vec2 logical_base = layout_buffer_.cursor + rich_offset;
				const math::frect r_rect = this->is_vertical()
					                           ? math::frect{
						                           tags::from_extent,
						                           {logical_base.x - metrics.run_font_asc, logical_base.y},
						                           {metrics.run_font_asc + metrics.run_font_desc, -1.f}
					                           }
					                           : math::frect{
						                           tags::from_extent,
						                           {logical_base.x, logical_base.y - metrics.run_font_asc},
						                           {1.f, metrics.run_font_asc + metrics.run_font_desc}
					                           };

				cluster_push_(results, logical_cluster{current_cluster, 1, r_rect});

				if(config_.line_feed_type == linefeed_type::crlf){
					layout_buffer_.line_bound.width = {};
					active_ul_start.reset();
				}
				continue;
			} else if(ch == U'\n'){
				if(current_logic_cluster){
					cluster_push_(results, *current_logic_cluster);
					current_logic_cluster.reset();
				}

				static_cast<block_data&>(layout_buffer_).max_bound_height(metrics.run_font_asc, metrics.run_font_desc);
				const math::vec2 logical_base = layout_buffer_.cursor + rich_offset;
				const math::frect n_rect = this->is_vertical()
					                           ? math::frect{
						                           tags::from_extent,
						                           {logical_base.x - metrics.run_font_asc, logical_base.y},
						                           {metrics.run_font_asc + metrics.run_font_desc, -1.f}
					                           }
					                           : math::frect{
						                           tags::from_extent,
						                           {logical_base.x, logical_base.y - metrics.run_font_asc},
						                           {1.f, metrics.run_font_asc + metrics.run_font_desc}
					                           };

				cluster_push_(results, logical_cluster{current_cluster, 1, n_rect});

				if(!this->flush_block_impl<IsInf>(config_, results) || !this->advance_line_impl<
					IsInf>(config_, results))
					return false;

				layout_buffer_.cursor = {};
				if(config_.line_feed_type == linefeed_type::crlf) layout_buffer_.cursor.*state_.minor_p = 0;
				active_ul_start.reset();
				continue;
			} else if(ch == U'\t'){
				if(current_logic_cluster){
					cluster_push_(results, *current_logic_cluster);
					current_logic_cluster.reset();
				}

				static_cast<block_data&>(layout_buffer_).max_bound_height(metrics.run_font_asc, metrics.run_font_desc);

				math::vec2 old_cursor = layout_buffer_.cursor;
				const float tab_step = config_.tab_scale * this->get_scaled_default_space_width();
				if(tab_step > std::numeric_limits<float>::epsilon()){
					const float current_pos = layout_buffer_.cursor.*state_.major_p;
					layout_buffer_.cursor.*state_.major_p = (std::floor(current_pos / tab_step) + 1) * tab_step;
				}

				math::vec2 step_advance{};
				step_advance.*state_.major_p = std::abs(
					layout_buffer_.cursor.*state_.major_p - old_cursor.*state_.major_p);
				const math::vec2 logical_base = old_cursor + rich_offset;
				const math::frect tab_rect = this->is_vertical()
					                             ? math::frect{
						                             tags::from_extent,
						                             {logical_base.x - metrics.run_font_asc, logical_base.y},
						                             {metrics.run_font_asc + metrics.run_font_desc, -step_advance.y}
					                             }
					                             : math::frect{
						                             tags::from_extent,
						                             {logical_base.x, logical_base.y - metrics.run_font_asc},
						                             {step_advance.x, metrics.run_font_asc + metrics.run_font_desc}
					                             };

				cluster_push_(results, logical_cluster{current_cluster, 1, tab_rect});
				continue;
			} else if(ch == U' '){
				if(current_logic_cluster){
					cluster_push_(results, *current_logic_cluster);
					current_logic_cluster.reset();
				}


				static_cast<block_data&>(layout_buffer_).max_bound_height(metrics.run_font_asc, metrics.run_font_desc);
				const math::vec2 logical_base = layout_buffer_.cursor + rich_offset;
				const math::frect space_rect = this->is_vertical()
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

				cluster_push_(results, logical_cluster{current_cluster, 1, space_rect});
				layout_buffer_.cursor = this->move_pen(layout_buffer_.cursor, run_advance);
				continue;
			}

			if constexpr(!IsInf.x){
				const float current_word_w = std::abs(layout_buffer_.cursor.*state_.major_p);
				const float line_w = std::abs(layout_buffer_.line_bound.width);
				const float next_w = std::abs(run_advance.*state_.major_p);
				const float max_w = config_.max_extent.*state_.major_p;
				const float indicator_w = config_.has_wrap_indicator()
					                          ? std::abs(
						                          cached_indicator_.advance.*state_.major_p * this->
						                          get_current_relative_scale())
					                          : 0.f;

				if(line_w + current_word_w + next_w > max_w + 0.001f){
					bool block_has_elems = (results.elems.size() > layout_buffer_.block_span.elem_start) ||
						(results.underlines.size() > layout_buffer_.block_span.ul_start);

					if((results.elems.size() > layout_buffer_.line_span.elem_start) && (current_word_w + next_w +
						indicator_w <= max_w + 0.001f)){
						if(!this->advance_line_impl<IsInf>(config_, results)) return false;
						if(config_.has_wrap_indicator()){
							const float scale = this->get_current_relative_scale();
							auto scaled_aabb = cached_indicator_.glyph_aabb.copy();
							scaled_aabb = {
									tags::unchecked, tags::from_vertex, scaled_aabb.vert_00() * scale,
									scaled_aabb.vert_11() * scale
								};
							const math::vec2 shift_adv = cached_indicator_.advance * scale;
							layout_buffer_.push_front_visual<enable_cluster_record>(
								results,
								scaled_aabb,
								get_line_indicator_elem(scaled_aabb),
								shift_adv
							);
							if(current_logic_cluster) current_logic_cluster->logical_rect.move(shift_adv);
							if(active_ul_start) active_ul_start->pos += shift_adv;
						}
					} else{
						if(block_has_elems){
							if(active_ul_start){
								this->submit_underline(results, *active_ul_start, prev_color, metrics, false);
								active_ul_start.reset();
							}
							if(current_logic_cluster){
								cluster_push_(results, *current_logic_cluster);
								current_logic_cluster.reset();
							}

							layout_buffer_.append<enable_cluster_record>(results, state_.major_p);
							static_cast<block_data&>(layout_buffer_).clear();
							layout_buffer_.block_sync_start<enable_cluster_record>(results);
							if(!this->advance_line_impl<IsInf>(config_, results)) return false;

							if(config_.has_wrap_indicator()){
								const float scale = this->get_current_relative_scale();
								auto scaled_aabb = cached_indicator_.glyph_aabb.copy();
								scaled_aabb = {
										tags::unchecked, tags::from_vertex, scaled_aabb.vert_00() * scale,
										scaled_aabb.vert_11() * scale
									};
								layout_buffer_.push_front_visual<enable_cluster_record>(
									results,
									scaled_aabb,
									get_line_indicator_elem(scaled_aabb),
									cached_indicator_.advance * scale
								);
							}

							if(is_underline_enabled_(config_)){
								active_ul_start = ul_start_info{
										layout_buffer_.cursor, this->get_current_gap_index(results, false)
									};
							}
						}
					}
				}
			}

			if(ch != U'\n' && ch != U'\r'){
				static_cast<block_data&>(layout_buffer_).max_bound_height(metrics.run_font_asc, metrics.run_font_desc);
			}

			if(this->is_reversed()) layout_buffer_.cursor = this->move_pen(layout_buffer_.cursor, run_advance);

			const math::vec2 logical_base = layout_buffer_.cursor + rich_offset;
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

			if(current_logic_cluster && current_logic_cluster->cluster_index == current_cluster){
				current_logic_cluster->logical_rect.expand_by(advance_rect);
				current_logic_cluster->cluster_span = std::max(current_logic_cluster->cluster_span, span);
			} else{
				if(current_logic_cluster) cluster_push_(results, *current_logic_cluster);
				current_logic_cluster = logical_cluster{current_cluster, span, advance_rect};
			}

			const math::vec2 glyph_local_draw_pos = {
					font::normalize_len(pos[i].x_offset) * metrics.run_scale_factor.x + rich_offset.x,
					-font::normalize_len(pos[i].y_offset) * metrics.run_scale_factor.y + rich_offset.y
				};

			const math::vec2 visual_base_pos = layout_buffer_.cursor + glyph_local_draw_pos;
			const math::frect actual_aabb = loaded_glyph.metrics().place_to(visual_base_pos, metrics.run_scale_factor);
			math::frect draw_aabb = actual_aabb.copy().expand(font::font_draw_expand * metrics.run_scale_factor);

			// ==========================================
			// 新增：在此处计算合成参数并扩张绘制包围盒 (防裁剪)
			// ==========================================
			float slant_factor_asc = 0.f;
			float slant_factor_desc = 0.f;
			float weight_offset = 0.f;

			if(synthetic_bold){
				// 设定粗体扩展量为当前字体高度的 3% (具体比例视你的 SDF 规范调整)
				weight_offset = metrics.req_size_vec.y * 0.03f;
			}

			if(synthetic_italic){
				// 设定 14° 的错切斜率 (tan(14°) ≈ 0.25)
				constexpr static float slant_factor = 0.25f;

				// 斜体会导致字形顶部向一侧偏移，必须拉宽包围盒
				slant_factor_asc = loaded_glyph.metrics().ascender() * slant_factor * metrics.run_scale_factor.y;
				slant_factor_desc = loaded_glyph.metrics().descender() * slant_factor * metrics.run_scale_factor.y;

				// 注意：这里需要根据你的引擎 Y 轴朝向调整。
				// 若 Y 轴向下，错切可能发生在不同方向。最安全的做法是横向双向扩展，
				// 或者根据实际变换矩阵只扩展对应的 max.x 或 min.x
				// draw_aabb.expand(math::vec2{shear_width, 0.f});
			}

			// 将计算好的具体参数打包进 glyph_elem
			// ReSharper disable once CppPossiblyUnintendedObjectSlicing
			layout_buffer_.push_back(results, actual_aabb,
				{draw_aabb, prev_color, std::move(loaded_glyph), slant_factor_asc, slant_factor_desc, weight_offset}
			);

			if(!this->is_reversed()) layout_buffer_.cursor = this->move_pen(layout_buffer_.cursor, run_advance);

		}

		if(current_logic_cluster) cluster_push_(results, *current_logic_cluster);
		if(active_ul_start) this->submit_underline(results, *active_ul_start, prev_color, metrics, true);

		if constexpr(enable_dynamic_rich_text_state){
			rich_text_state& rtstate = rich_text_state_;

			if(rtstate.next_apply_pos < start + length){
				for(auto p = rtstate.next_apply_pos; p < start + length; ++p){
					auto tokens = full_text.get_token_group(p, full_text.get_init_token());
					if(!tokens.empty()){
						rtstate.rich_context.update(*manager_,
							{
								state_.default_font_size, layout_buffer_.block_ascender, layout_buffer_.block_descender,
								state_.is_vertical_mode
							},
							config_.rich_text_fallback_style, tokens, context_update_mode::soft_only);
						rtstate.token_soft_last = tokens.end();
					}
				}
			}
		}

		return true;
	}


	template <math::bool2 IsInf>
	bool advance_line_impl(const layout_config& config_, glyph_layout& results) noexcept{
		const float scale = this->get_current_relative_scale();
		const float min_asc = state_.default_ascender * scale;
		const float min_desc = state_.default_descender * scale;

		const float current_asc = std::max(min_asc, layout_buffer_.line_bound.ascender);
		const float current_desc = std::max(min_desc, layout_buffer_.line_bound.descender);
		float next_baseline = state_.current_baseline_pos;

		if(state_.is_first_line){
			next_baseline = current_asc;
		} else{
			const float metrics_sum = state_.prev_line_descender + current_asc;
			next_baseline += metrics_sum * config_.line_spacing_scale + config_.line_spacing_fixed_distance;
		}

		if constexpr(!IsInf.x){
			const float container_width = config_.max_extent.*state_.major_p;
			if(layout_buffer_.line_bound.width > container_width + 0.001f) return false;
		}

		math::vec2 offset_vec{};
		offset_vec.*state_.major_p = {};
		offset_vec.*state_.minor_p = next_baseline;

		// 【关键修复 1】明确当前行的数据终点，使用 layout_buffer_.span 作为隔离墙，避免将尚未换行的字符卷入。
		const std::size_t line_elem_end = layout_buffer_.block_span.elem_start;
		const std::size_t line_ul_end = layout_buffer_.block_span.ul_start;
		const std::size_t line_cluster_end = enable_cluster_record ? layout_buffer_.block_span.cluster_start : 0;

		bool has_elems = (line_elem_end > layout_buffer_.line_span.elem_start) ||
			(line_ul_end > layout_buffer_.line_span.ul_start);

		if(has_elems){
			const float visual_min_y = layout_buffer_.line_pos_min.*state_.minor_p + offset_vec.*state_.minor_p;
			const float visual_max_y = layout_buffer_.line_pos_max.*state_.minor_p + offset_vec.*state_.minor_p;
			const float logical_min_y = offset_vec.*state_.minor_p - current_asc;
			const float logical_max_y = offset_vec.*state_.minor_p + current_desc;

			const float line_min_y = std::min(visual_min_y, logical_min_y);
			const float line_max_y = std::max(visual_max_y, logical_max_y);
			const float global_min_y = std::min(state_.min_bound.*state_.minor_p, line_min_y);
			const float global_max_y = std::max(state_.max_bound.*state_.minor_p, line_max_y);

			if constexpr(!IsInf.y){
				if((global_max_y - global_min_y) > config_.max_extent.*state_.minor_p + 0.001f){
					// 越界：利用 resize 零开销裁切掉本行及之后产生的所有废弃数据
					results.elems.resize(layout_buffer_.line_span.elem_start);
					results.underlines.resize(layout_buffer_.line_span.ul_start);
					if constexpr(enable_cluster_record) results.clusters.resize(layout_buffer_.line_span.cluster_start);
					return false;
				}
			}
		} else{
			if constexpr(!IsInf.y){
				if(next_baseline + current_desc > config_.max_extent.*state_.minor_p) return false;
			}
		}

		state_.current_baseline_pos = next_baseline;
		state_.is_first_line = false;

		line new_line;
		new_line.start_pos = offset_vec;
		new_line.rect = layout_buffer_.line_bound;

		new_line.glyph_range = subrange(layout_buffer_.line_span.elem_start,
			line_elem_end - layout_buffer_.line_span.elem_start);
		new_line.underline_range = subrange(layout_buffer_.line_span.ul_start,
			line_ul_end - layout_buffer_.line_span.ul_start);
		if constexpr(enable_cluster_record){
			new_line.cluster_range = subrange(layout_buffer_.line_span.cluster_start,
				line_cluster_end - layout_buffer_.line_span.cluster_start);
		}

		for(std::size_t i = layout_buffer_.line_span.elem_start; i < line_elem_end; ++i){
			const auto& elem = results.elems[i];
			state_.min_bound.min(elem.aabb.vert_00() + offset_vec);
			state_.max_bound.max(elem.aabb.vert_11() + offset_vec);
		}

		for(std::size_t i = layout_buffer_.line_span.ul_start; i < line_ul_end; ++i){
			const auto& ul = results.underlines[i];
			math::vec2 ul_min = math::min(ul.start, ul.end) + offset_vec;
			math::vec2 ul_max = math::max(ul.start, ul.end) + offset_vec;
			ul_min.y -= ul.thickness / 2.f;
			ul_max.y += ul.thickness / 2.f;

			state_.min_bound.min(ul_min);
			state_.max_bound.max(ul_max);
		}

		math::vec2 line_logical_min = offset_vec;
		line_logical_min.*state_.minor_p = offset_vec.*state_.minor_p - current_asc;
		line_logical_min.*state_.major_p = offset_vec.*state_.major_p;

		math::vec2 line_logical_max = offset_vec;
		line_logical_max.*state_.minor_p = offset_vec.*state_.minor_p + current_desc;
		line_logical_max.*state_.major_p = offset_vec.*state_.major_p + layout_buffer_.line_bound.width;

		state_.min_bound.min(line_logical_min);
		state_.max_bound.max(line_logical_max);

		results.lines.push_back(std::move(new_line));
		state_.prev_line_descender = current_desc;

		static_cast<line_data&>(layout_buffer_).clear();
		layout_buffer_.line_span = layout_buffer_.block_span;

		return true;
	}

	template <math::bool2 IsInf>
	bool flush_block_impl(const layout_config& config_, glyph_layout& results){
		if(layout_buffer_.empty<enable_cluster_record>(results)) return true;

		if(layout_buffer_.check_block_fit<IsInf>(state_, config_, results, get_current_relative_scale())){
			layout_buffer_.append<enable_cluster_record>(results, state_.major_p);
			static_cast<block_data&>(layout_buffer_).clear();
			layout_buffer_.block_sync_start<enable_cluster_record>(results);
			return true;
		}

		if(!this->advance_line_impl<IsInf>(config_, results)) return false;

		if(config_.has_wrap_indicator()){
			const float scale = this->get_current_relative_scale();
			auto scaled_aabb = cached_indicator_.glyph_aabb.copy();
			scaled_aabb = {
					tags::unchecked, tags::from_vertex, scaled_aabb.vert_00() * scale, scaled_aabb.vert_11() * scale
				};
			layout_buffer_.push_front_visual<enable_cluster_record>(
				results,
				scaled_aabb,
				get_line_indicator_elem(scaled_aabb),
				cached_indicator_.advance * scale
			);
		}

		if(layout_buffer_.check_block_fit<IsInf>(state_, config_, results, get_current_relative_scale())){
			layout_buffer_.append<enable_cluster_record>(results, state_.major_p);
		} else{
			return false;
		}

		static_cast<block_data&>(layout_buffer_).clear();
		layout_buffer_.block_sync_start<enable_cluster_record>(results);
		return true;
	}

	template <math::bool2 IsInf>
	bool process_layout_impl(const tokenized_text_view& full_text, const layout_config& config_, glyph_layout& results){
		typst_szt current_idx = 0;
		typst_szt run_start = 0;

		run_style_state current_style{};

		auto resolve_style = [&](char32_t codepoint, bool req_italic, bool req_bold) -> run_style_state{
			const auto* family = get_font_view_(config_);
			font::font_style target = font::make_font_style(req_italic, req_bold);

			// 调用新的 API 获取 styled_font_face_view
			auto styled_view = manager_->use_family(family, target);
			auto [best_face, _] = styled_view.view.find_glyph_of(codepoint);

			return run_style_state{
					best_face,
					req_italic && !styled_view.is_italic_satisfied,
					req_bold && !styled_view.is_bold_satisfied
				};
		};

		while(current_idx < full_text.get_text().size()){
			if constexpr(enable_dynamic_rich_text_state){
				rich_text_state& rtstate = rich_text_state_;
				auto tokens = full_text.get_token_group(current_idx, rtstate.token_hard_last);
				rtstate.token_hard_last = tokens.end();

				if(check_token_group_need_another_run(tokens)){
					if(current_style.face && current_idx > run_start){
						// 注意：这里向下传递了合成参数
						if(!process_text_run_impl<IsInf>(full_text, config_, results, run_start,
							current_idx - run_start, *current_style.face,
							current_style.synthetic_italic, current_style.synthetic_bold))
							return false;
					}

					rtstate.rich_context.update(
						*manager_,
						update_param{
							state_.default_font_size, layout_buffer_.block_ascender, layout_buffer_.block_descender,
							state_.is_vertical_mode
						},
						config_.rich_text_fallback_style, tokens, context_update_mode::hard_only,
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
					current_style = {};
					if(!this->flush_block_impl<IsInf>(config_, results)) return false;
				}
			}

			bool req_italic = is_italic_enabled_(config_);
			bool req_bold = is_bold_enabled_(config_);

			const auto codepoint = full_text.get_text()[current_idx];
			run_style_state best_style = resolve_style(codepoint, req_italic, req_bold);

			if(!current_style.face){
				current_style = best_style;
			} else if(best_style != current_style){
				if(!process_text_run_impl<IsInf>(full_text, config_, results, run_start, current_idx - run_start,
					*current_style.face, current_style.synthetic_italic, current_style.synthetic_bold))
					return false;
				current_style = best_style;
				run_start = current_idx;
			}
			current_idx++;
		}

		if(current_style.face){
			if(!process_text_run_impl<IsInf>(full_text, config_, results, run_start,
				full_text.get_text().size() - run_start, *current_style.face,
				current_style.synthetic_italic, current_style.synthetic_bold))
				return false;
		}

		return this->flush_block_impl<IsInf>(config_, results) && this->advance_line_impl<IsInf>(config_, results);
	}

public:
	// ==========================================
	// 6. 动态到编译期的桥接入口 (Dynamic Dispatcher)
	// ==========================================
	void layout(const tokenized_text_view& full_text, const layout_config& config_, glyph_layout& layout_ref){
		this->initialize_state(full_text, config_);

		layout_ref.clear();
		layout_ref.direction = this->get_actual_direction(config_);

		const std::size_t raw_text_size = full_text.get_text().size();
		layout_ref.elems.reserve(raw_text_size);
		cluster_reserve_(layout_ref, raw_text_size);
		layout_ref.lines.reserve((raw_text_size / 64) + 1);

		const bool inf_major = std::isinf(config_.max_extent.*state_.major_p);
		const bool inf_minor = std::isinf(config_.max_extent.*state_.minor_p);

		bool result = false;
		if(inf_major && inf_minor){
			result = this->process_layout_impl<math::bool2{true, true}>(full_text, config_, layout_ref);
		} else if(inf_major && !inf_minor){
			result = this->process_layout_impl<math::bool2{true, false}>(full_text, config_, layout_ref);
		} else if(!inf_major && inf_minor){
			result = this->process_layout_impl<math::bool2{false, true}>(full_text, config_, layout_ref);
		} else{
			result = this->process_layout_impl<math::bool2{false, false}>(full_text, config_, layout_ref);
		}
		layout_ref.is_exhausted = result;

		this->finalize(layout_ref);
	}

	[[nodiscard]] glyph_layout layout(const tokenized_text_view& full_text, const layout_config& config_){
		glyph_layout results{};
		this->layout(full_text, config_, results);
		return results;
	}

	[[nodiscard]] FORCE_INLINE inline font::glyph_size_type get_current_snapped_size() const noexcept{
		return layout_context_impl::get_snapped_size_vec(get_font_size_());
	}

	[[nodiscard]] FORCE_INLINE static font::glyph_size_type get_snapped_size_vec(font::glyph_size_type v) noexcept{
		return {font::get_snapped_size(v.x), font::get_snapped_size(v.y)};
	}

	[[nodiscard]] FORCE_INLINE static font::glyph_size_type get_snapped_size_vec(math::vec2 v) noexcept{
		return get_snapped_size_vec(v.as<font::glyph_size_type::value_type>());
	}

	[[nodiscard]] FORCE_INLINE inline float get_current_relative_scale() const noexcept{
		return get_font_size_().y / state_.default_font_size.y;
	}

	[[nodiscard]] FORCE_INLINE inline float get_scaled_default_line_thickness() const noexcept{
		return state_.default_line_thickness * this->get_current_relative_scale();
	}

	[[nodiscard]] FORCE_INLINE inline float get_scaled_default_space_width() const noexcept{
		return state_.default_space_width * this->get_current_relative_scale();
	}
};

export using layout_context = layout_context_impl<
	policies::store_clusters,
	policies::rich_text_enabled
>;

export using fast_plain_layout_context = layout_context_impl<
	policies::ignore_clusters,
	policies::plain_text_only
>;

constexpr std::size_t sz = sizeof(fast_plain_layout_context);
}
