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

namespace mo_yanxi::typesetting {

// ==========================================
// 1. 基础工具与配置
// ==========================================

export struct layout_config {
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

    [[nodiscard]] math::vec2 get_default_font_size() const noexcept {
        return default_font_size.value_or(
            glyph_size::get_glyph_std_size_at(glyph_size::standard_size, get_screen_ppi()));
    }

    [[nodiscard]] math::vec2 get_screen_ppi() const noexcept {
        return screen_ppi.value_or(glyph_size::screen_ppi);
    }

    [[nodiscard]] constexpr bool has_wrap_indicator() const noexcept { return wrap_indicator_char != 0; }
    constexpr bool operator==(const layout_config&) const noexcept = default;
};


// ==========================================
// 2. Concepts 约束定义
// ==========================================
namespace concepts {
    template <typename T>
    concept ClusterPolicy = requires(const T policy, layout_block<true>& block, const logical_cluster& cluster, std::size_t size) {
        { T::enabled } -> std::convertible_to<bool>;
        policy.push(block, cluster);
        policy.reserve(block, size);
    };

    template <typename T, typename StateType>
    concept RichTextPolicy = requires(
        const T policy,
        StateType& state,
        const layout_config& config,
        font::font_manager& manager,
        const tokenized_text_view& text,
        std::size_t target
    ) {
        { T::enabled } -> std::convertible_to<bool>;
        // policy.sync(state, config, manager, text, target);
        // { policy.get_color(state, config) } -> std::convertible_to<graphic::color>;
        // { policy.is_underline_enabled(state, config) } -> std::convertible_to<bool>;
        // { policy.get_rich_offset(state, config) } -> std::convertible_to<math::vec2>;
        // { policy.get_size(state, config) } -> std::convertible_to<math::vec2>;
    };
}

// ==========================================
// 3. Policies 策略实现
// ==========================================
namespace policies {
    // --- Cluster 策略 ---
    struct ignore_clusters {
        static constexpr bool enabled = false;

        static constexpr void push(auto& block, const logical_cluster& cluster) noexcept {}
        static constexpr void reserve(auto& block, std::size_t size) noexcept {}
    };

    struct store_clusters {
        static constexpr bool enabled = true;

        static constexpr void push(layout_block<true>& block, const logical_cluster& cluster) {
            block.clusters.push_back(cluster);
        }

        static constexpr void reserve(layout_block<true>& block, std::size_t size) {
            block.clusters.reserve(block.clusters.size() + size);
        }
    };

    // --- 富文本策略 ---
    struct plain_text_only {
        static constexpr bool enabled = false;

        FORCE_INLINE static constexpr void sync(const auto&, const auto&, const layout_state_t& state, const layout_config& config, font::font_manager& manager, const tokenized_text_view& full_text, std::size_t target_cluster) noexcept {}

    	FORCE_INLINE static graphic::color get_color(const auto&, const layout_config& config) noexcept {
            return config.rich_text_fallback_style.color;
        }

    	FORCE_INLINE static bool is_underline_enabled(const auto&, const layout_config& config) noexcept {
            return config.rich_text_fallback_style.enables_underline;
        }

    	FORCE_INLINE static math::vec2 get_rich_offset(const auto&, const layout_config& config) noexcept {
            return config.rich_text_fallback_style.offset;
        }

    	FORCE_INLINE static math::vec2 get_size(const auto&, const layout_state_t& state) noexcept {
            return state.default_font_size;
        }

    };

    struct rich_text_enabled {
        static constexpr bool enabled = true;

        FORCE_INLINE static void sync(
        	rich_text_state& rich_context, const layout_block<true>& block, layout_state_t& state, const layout_config& config, font::font_manager& manager, const tokenized_text_view& full_text, std::size_t target_cluster){
            for(typst_szt p = rich_context.next_apply_pos; p <= target_cluster; ++p) {
                auto tokens = full_text.get_token_group(p, rich_context.token_soft_last);
                if(!tokens.empty()) {
                    rich_context.rich_context.update(
                        manager,
                        {state.default_font_size, block.block_ascender, block.block_descender, state.is_vertical_mode},
                        config.rich_text_fallback_style,
                        tokens, context_update_mode::soft_only
                    );
                    rich_context.token_soft_last = tokens.end();
                }
            }
            rich_context.next_apply_pos = target_cluster + 1;
        }

        FORCE_INLINE static graphic::color get_color(const rich_text_state& rich_context, const layout_config& config) noexcept {
	        return rich_context.rich_context.get_color(config.rich_text_fallback_style);
        }

        FORCE_INLINE static bool is_underline_enabled(const rich_text_state& rich_context, const layout_config& config) noexcept {
	        return rich_context.rich_context.is_underline_enabled(config.rich_text_fallback_style);
        }

        FORCE_INLINE static math::vec2 get_rich_offset(const rich_text_state& rich_context, const layout_config& config) noexcept {
	        return rich_context.rich_context.get_offset(config.rich_text_fallback_style);
        }

        FORCE_INLINE static math::vec2 get_size(const rich_text_state& rich_context, const layout_state_t& state) noexcept {
	        return rich_context.rich_context.get_size(state.default_font_size);
        }
    };
}


struct indicator_cache {
	font::glyph_borrow texture;
	math::frect glyph_aabb;
	math::vec2 advance;
};

struct run_metrics {
	float run_font_asc = 0.f;
	float run_font_desc = 0.f;
	float ul_position = 0.f;
	float ul_thickness = 0.f;
	math::vec2 run_scale_factor{};
	math::vec2 req_size_vec{};
	font::glyph_size_type snapped_size{};
};

struct ul_start_info {
	math::vec2 pos;
	typst_szt gap_count;
};

struct layout_ctx_base{
protected:
	font::font_manager* manager_{font::default_font_manager};
	lru_cache<font::font_face_handle*, font::hb::font_ptr, 4> hb_cache_;
	font::hb::buffer_ptr hb_buffer_;
	std::vector<hb_feature_t> feature_stack_;
	layout_config config_{};
	layout_state_t state_{};
	indicator_cache cached_indicator_{};



public:

	layout_ctx_base() = default;

	explicit layout_ctx_base(std::in_place_t){
		hb_buffer_ = font::hb::make_buffer();
	}

	explicit layout_ctx_base(const layout_config& c, font::font_manager* m = font::default_font_manager)
		: manager_(m), config_(c) {
		assert(manager_ != nullptr);
		hb_buffer_ = font::hb::make_buffer();
	}


	bool set_max_extent(math::vec2 ext){
		if(config_.max_extent == ext) return false;
		config_.max_extent = ext;
		return true;
	}

    void set_config(const layout_config& c){ config_ = c; }
    [[nodiscard]] const layout_config& get_config() const noexcept{ return config_; }

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

    [[nodiscard]] FORCE_INLINE inline hb_font_t* get_hb_font(font::font_face_handle* face) noexcept{
        if(const auto ptr = hb_cache_.get(face)) return ptr->get();
        auto new_hb_font = create_harfbuzz_font(*face);
        hb_ft_font_set_load_flags(new_hb_font.get(), FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING);
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
        if(results.lines.empty()) {
            results.extent = {0, 0};
            return;
        }
        const auto extent = state_.max_bound - state_.min_bound;
        results.extent = extent + math::vec2{1, 1};
        for(auto& line : results.lines) {
            line.start_pos -= state_.min_bound;
        }
    }

};

// ==========================================
// 5. 核心排版引擎 (NTTP 与策略挂载)
// ==========================================
template<
    concepts::ClusterPolicy ClusterPol = policies::store_clusters,
    concepts::RichTextPolicy<layout_state_t> RichTextPol = policies::rich_text_enabled
>
class layout_context_impl : public layout_ctx_base{
public:
	using layout_ctx_base::layout_ctx_base;

private:

	static constexpr bool enable_cluster_record = ClusterPol::enabled;
	static constexpr bool enable_dynamic_rich_text_state = RichTextPol::enabled;

    // --- 零开销挂载点 ---
    ADAPTED_NO_UNIQUE_ADDRESS ClusterPol cluster_policy_;
    ADAPTED_NO_UNIQUE_ADDRESS RichTextPol rich_text_policy_;
	ADAPTED_NO_UNIQUE_ADDRESS cond_exist<rich_text_state, enable_dynamic_rich_text_state> rich_text_state_;
	line_buffer_t<enable_cluster_record> line_buffer_{};
	layout_block<enable_cluster_record> current_block_{};

#pragma region Cluster
	FORCE_INLINE inline void cluster_reserve_(std::size_t sz){
		cluster_policy_.reserve(current_block_, sz);
	}
	
	FORCE_INLINE inline void cluster_push_(const logical_cluster& cluster){
		cluster_policy_.push(current_block_, cluster);
	}
#pragma endregion 
	
#pragma region RichTextStateGetter
	FORCE_INLINE inline constexpr math::vec2 get_font_size_() const noexcept{
		return rich_text_policy_.get_size(rich_text_state_, state_);
	}

	FORCE_INLINE inline constexpr bool is_underline_enabled_() const noexcept{
		return rich_text_policy_.is_underline_enabled(rich_text_state_, config_);
	}


	FORCE_INLINE inline constexpr graphic::color get_font_color_() const noexcept{
		return rich_text_policy_.get_color(rich_text_state_, config_);
	}

	FORCE_INLINE inline constexpr math::vec2 get_font_offset_() const noexcept{
		return rich_text_policy_.get_rich_offset(rich_text_state_, config_);
	}

	FORCE_INLINE inline void rich_text_sync_(const tokenized_text_view& full_text, std::uint32_t current_cluster){
		if constexpr (enable_dynamic_rich_text_state){
			rich_text_policy_.sync(rich_text_state_, current_block_, state_, config_, *manager_, full_text, current_cluster);
		}
	}

	FORCE_INLINE inline const font::font_family* get_font_view_() const noexcept{
		if constexpr (enable_dynamic_rich_text_state){
			const rich_text_state& state = rich_text_state_;
			return &state.rich_context.get_font(config_.rich_text_fallback_style, manager_->get_default_family());
		}else{
			return config_.rich_text_fallback_style.family ? config_.rich_text_fallback_style.family : manager_->get_default_family();
		}
	}

#pragma endregion

	void submit_underline(const ul_start_info& start_info, graphic::color ctx_color, const run_metrics& metrics, bool is_delimiter) {
		math::vec2 offset_vec{};
		offset_vec.*state_.minor_p = -metrics.ul_position;
		underline ul;
		ul.start = start_info.pos + offset_vec;
		ul.end = current_block_.cursor + offset_vec;
		ul.thickness = metrics.ul_thickness;
		ul.color = ctx_color;
		ul.start_gap_count = start_info.gap_count;
		ul.end_gap_count = std::max(ul.start_gap_count, this->get_current_gap_index(is_delimiter));
		current_block_.push_back_underline(ul);
	}

	// --- 内部辅助方法 ---
	typst_szt get_current_gap_index(bool is_delimiter) const noexcept {
		const auto size = line_buffer_.elems.size() + current_block_.glyphs.size();
		return (is_delimiter && size > 0) ? size - 1 : size;
	}

	run_metrics calculate_metrics(const font::font_face_handle& face) const {
		run_metrics m;
		m.req_size_vec = (get_font_size_() * config_.throughout_scale).max({1, 1});
		m.snapped_size = get_snapped_size_vec(m.req_size_vec);
		m.run_scale_factor = m.req_size_vec / m.snapped_size.as<float>();

		auto ft_face = face.get();
		(void)face.set_size(m.snapped_size);

		if(this->is_vertical()) {
			m.run_font_asc = m.req_size_vec.x / 2.f;
			m.run_font_desc = m.req_size_vec.x / 2.f;
		} else {
			const float raw_asc = font::normalize_len(ft_face->size->metrics.ascender) * m.run_scale_factor.y;
			const float raw_desc = std::abs(font::normalize_len(ft_face->size->metrics.descender) * m.run_scale_factor.y);
			const float raw_height = raw_asc + raw_desc;

			if(raw_height > 0.001f) {
				const float em_scale = m.req_size_vec.y / raw_height;
				m.run_font_asc = raw_asc * em_scale;
				m.run_font_desc = raw_desc * em_scale;
			} else {
				m.run_font_asc = m.req_size_vec.y * 0.88f;
				m.run_font_desc = m.req_size_vec.y * 0.12f;
			}
		}

		if(ft_face->units_per_EM != 0) {
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

    void initialize_state(const tokenized_text_view& full_text, font::font_face_view base_view) {
        base_view = base_view ? base_view : manager_->use_family(manager_->get_default_family());
        state_.reset();
		line_buffer_.clear();
		current_block_.clear();

		if constexpr (enable_dynamic_rich_text_state){
			rich_text_state& rtstate = rich_text_state_;
			rtstate.reset(full_text);
		}

        feature_stack_.clear();
        feature_stack_ = {config_.rich_text_fallback_style.features.begin(), config_.rich_text_fallback_style.features.end()};
        feature_stack_.reserve(8);

        if(config_.direction != layout_direction::deduced) {
            switch(config_.direction){
            case layout_direction::ltr : state_.target_hb_dir = HB_DIRECTION_LTR; break;
            case layout_direction::rtl : state_.target_hb_dir = HB_DIRECTION_RTL; break;
            case layout_direction::ttb : state_.target_hb_dir = HB_DIRECTION_TTB; break;
            case layout_direction::btt : state_.target_hb_dir = HB_DIRECTION_BTT; break;
            default : std::unreachable();
            }
        } else {
            hb_buffer_clear_contents(hb_buffer_.get());
            hb_buffer_add_utf32(hb_buffer_.get(), reinterpret_cast<const std::uint32_t*>(full_text.get_text().data()),
                static_cast<int>(full_text.get_text().size()), 0, -1);
            hb_buffer_guess_segment_properties(hb_buffer_.get());
            state_.target_hb_dir = hb_buffer_get_direction(hb_buffer_.get());
            if(state_.target_hb_dir == HB_DIRECTION_INVALID) state_.target_hb_dir = HB_DIRECTION_LTR;
        }

        if(this->is_vertical()) {
            state_.major_p = &math::vec2::y;
            state_.minor_p = &math::vec2::x;
            state_.is_vertical_mode = true;
        } else {
            state_.major_p = &math::vec2::x;
            state_.minor_p = &math::vec2::y;
            state_.is_vertical_mode = false;
        }

        state_.default_font_size = config_.get_default_font_size();
        const auto snapped_base_size = get_snapped_size_vec(state_.default_font_size);
        auto& primary_face = base_view.face();

        (void)primary_face.set_size(snapped_base_size);
        FT_Load_Char(primary_face, FT_ULong{' '}, FT_LOAD_NO_HINTING);
        const math::vec2 base_scale_factor = state_.default_font_size / snapped_base_size.as<float>();

        if(this->is_vertical()) {
            state_.default_ascender = state_.default_font_size.x / 2.f;
            state_.default_descender = state_.default_font_size.x / 2.f;
            state_.default_line_thickness = font::normalize_len(primary_face->size->metrics.max_advance) * base_scale_factor.x;
            state_.default_space_width = state_.default_line_thickness;
        } else {
            const float raw_asc = font::normalize_len(primary_face->size->metrics.ascender) * base_scale_factor.y;
            const float raw_desc = std::abs(font::normalize_len(primary_face->size->metrics.descender) * base_scale_factor.y);
            const float raw_height = raw_asc + raw_desc;

            if(raw_height > 0.001f) {
                state_.default_ascender = raw_asc;
                state_.default_descender = raw_desc;
            } else {
                state_.default_ascender = state_.default_font_size.y * 0.8f;
                state_.default_descender = state_.default_font_size.y * 0.2f;
            }

            state_.default_line_thickness = font::normalize_len(primary_face->size->metrics.height) * base_scale_factor.y;
            state_.default_space_width = font::normalize_len(primary_face->glyph->advance.x) * base_scale_factor.x;
        }

        if(config_.has_wrap_indicator()) {
            auto [face, index] = base_view.find_glyph_of(config_.wrap_indicator_char);
            if(index) {
                font::glyph_identity id{index, snapped_base_size};
                font::glyph g = manager_->get_glyph_exact(*face, id);
                const auto& m = g.metrics();
                const auto advance = m.advance * base_scale_factor;
                math::vec2 adv{};
                float fallback_adv = (std::abs(advance.*state_.major_p) > 0.001f)
                                         ? std::abs(advance.*state_.major_p)
                                         : std::abs(advance.*state_.minor_p);

            	adv.*state_.major_p = fallback_adv * (is_reversed() ? -1.f : 1.f);


                math::frect local_aabb = m.place_to({}, base_scale_factor);
                if (state_.target_hb_dir == HB_DIRECTION_RTL) {
                    local_aabb.move({-fallback_adv, 0.f});
                } else if (state_.target_hb_dir == HB_DIRECTION_TTB) {
                    float cx = -(local_aabb.vert_11().x + local_aabb.vert_00().x) / 2.f;
                    float cy = fallback_adv / 2.f - (local_aabb.vert_11().y + local_aabb.vert_00().y) / 2.f;
                    local_aabb.move({cx, cy});
                } else if (state_.target_hb_dir == HB_DIRECTION_BTT) {
                    float cx = -(local_aabb.vert_11().x + local_aabb.vert_00().x) / 2.f;
                    float cy = -fallback_adv / 2.f - (local_aabb.vert_11().y + local_aabb.vert_00().y) / 2.f;
                    local_aabb.move({cx, cy});
                }

                cached_indicator_ = indicator_cache{std::move(g), local_aabb, adv};
            } else {
                const_cast<layout_config&>(config_).wrap_indicator_char = 0;
            }
        }
    }

    // ==========================================
    // 编译期分支派发核心逻辑
    // ==========================================
    template<math::bool2 IsInf>
    bool process_text_run_impl(const tokenized_text_view& full_text, glyph_layout& results, typst_szt start, typst_szt length, font::font_face_handle& face) {
        hb_buffer_clear_contents(hb_buffer_.get());
        hb_buffer_add_utf32(hb_buffer_.get(), reinterpret_cast<const std::uint32_t*>(full_text.get_text().data()),
            static_cast<int>(full_text.get_text().size()), static_cast<unsigned int>(start), static_cast<int>(length));
        hb_buffer_set_direction(hb_buffer_.get(), state_.target_hb_dir);
        hb_buffer_guess_segment_properties(hb_buffer_.get());

        const run_metrics metrics = this->calculate_metrics(face);
        ::hb_shape(this->get_hb_font(&face), hb_buffer_.get(), feature_stack_.data(), feature_stack_.size());

        unsigned int len;
        hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(hb_buffer_.get(), &len);
        hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(hb_buffer_.get(), &len);

        if constexpr (enable_dynamic_rich_text_state){
        	rich_text_state& rtstate = rich_text_state_;
        	rtstate.next_apply_pos = start;
        }

        std::optional<ul_start_info> active_ul_start;
        std::optional<logical_cluster> current_logic_cluster;
        graphic::color prev_color = graphic::colors::white;

        current_block_.glyphs.reserve(current_block_.glyphs.size() + len);
		cluster_reserve_(len);

        for(std::uint32_t i = 0; i < len; ++i) {
            const font::glyph_index_t gid = infos[i].codepoint;
            const auto current_cluster = infos[i].cluster;
            const auto ch = full_text.get_text()[current_cluster];
            const bool is_delimiter = (ch == U' ' || ch == U'\t' || ch == U'\r' || ch == U'\n' || is_separator(ch));

            bool was_ul = false;
            bool is_ul = false;
            math::vec2 rich_offset{};

            // 策略挂载点：富文本解析同步
            if constexpr (enable_dynamic_rich_text_state) {
                was_ul = is_underline_enabled_();
            	rich_text_sync_(full_text, current_cluster);
                is_ul = is_underline_enabled_();
            }else{
            	is_ul = is_underline_enabled_();
            }

        	prev_color = get_font_color_();
        	rich_offset = get_font_offset_();

            if(was_ul && !is_ul && active_ul_start) {
                this->submit_underline(*active_ul_start, prev_color, metrics, is_delimiter);
                active_ul_start.reset();
            }

            if(is_ul && !active_ul_start) {
                active_ul_start = ul_start_info{current_block_.cursor, this->get_current_gap_index(is_delimiter)};
            }

            if(is_delimiter) {
                if(active_ul_start) {
                    this->submit_underline(*active_ul_start, prev_color, metrics, true);
                    active_ul_start.reset();
                }
                if (current_logic_cluster) {
                    cluster_push_(*current_logic_cluster);
                    current_logic_cluster.reset();
                }

                if(!this->flush_block_impl<IsInf>(results)) return false;

                if constexpr (enable_dynamic_rich_text_state) {
                    if(ch != U'\n' && ch != U'\r' && is_underline_enabled_()) {
                        active_ul_start = ul_start_info{current_block_.cursor, this->get_current_gap_index(true)};
                    }
                }
            }

            const font::glyph loaded_glyph = manager_->get_glyph_exact(face, {gid, metrics.snapped_size});
            const float asc = this->is_vertical() ? (loaded_glyph.metrics().advance.x * metrics.run_scale_factor.x / 2.f) : (loaded_glyph.metrics().ascender() * metrics.run_scale_factor.y);
            const float desc = this->is_vertical() ? asc : (loaded_glyph.metrics().descender() * metrics.run_scale_factor.y);
            const math::vec2 run_advance{
                font::normalize_len(pos[i].x_advance) * metrics.run_scale_factor.x,
                font::normalize_len(pos[i].y_advance) * metrics.run_scale_factor.y
            };

            // 控制字符逻辑
            if(ch == U'\r') {
                if(current_logic_cluster){
	                cluster_push_(*current_logic_cluster);
                	current_logic_cluster.reset();
                }

                current_block_.block_ascender = std::max(current_block_.block_ascender, metrics.run_font_asc);
                current_block_.block_descender = std::max(current_block_.block_descender, metrics.run_font_desc);
                const math::vec2 logical_base = current_block_.cursor + rich_offset;
                const math::frect r_rect = this->is_vertical()
                    ? math::frect{ tags::from_extent, {logical_base.x - metrics.run_font_asc, logical_base.y}, {metrics.run_font_asc + metrics.run_font_desc, -1.f} }
                    : math::frect{ tags::from_extent, {logical_base.x, logical_base.y - metrics.run_font_asc}, {1.f, metrics.run_font_asc + metrics.run_font_desc} };

                cluster_push_(logical_cluster{current_cluster, 1, r_rect});

                if(config_.line_feed_type == linefeed_type::crlf) {
                    line_buffer_.line_bound.width = {};
                    active_ul_start.reset();
                }
                continue;
            } else if(ch == U'\n') {
                if(current_logic_cluster){
	                cluster_push_(*current_logic_cluster);
                	current_logic_cluster.reset();
                }

                current_block_.block_ascender = std::max(current_block_.block_ascender, metrics.run_font_asc);
                current_block_.block_descender = std::max(current_block_.block_descender, metrics.run_font_desc);
                const math::vec2 logical_base = current_block_.cursor + rich_offset;
                const math::frect n_rect = this->is_vertical()
                    ? math::frect{ tags::from_extent, {logical_base.x - metrics.run_font_asc, logical_base.y}, {metrics.run_font_asc + metrics.run_font_desc, -1.f} }
                    : math::frect{ tags::from_extent, {logical_base.x, logical_base.y - metrics.run_font_asc}, {1.f, metrics.run_font_asc + metrics.run_font_desc} };

                cluster_push_(logical_cluster{current_cluster, 1, n_rect});

                if(!this->flush_block_impl<IsInf>(results) || !this->advance_line_impl<IsInf>(results)) return false;

                current_block_.cursor = {};
                if(config_.line_feed_type == linefeed_type::crlf) current_block_.cursor.*state_.minor_p = 0;
                active_ul_start.reset();
                continue;
            } else if(ch == U'\t') {
                if(current_logic_cluster){
	                cluster_push_(*current_logic_cluster);
                	current_logic_cluster.reset();
                }

                current_block_.block_ascender = std::max(current_block_.block_ascender, metrics.run_font_asc);
                current_block_.block_descender = std::max(current_block_.block_descender, metrics.run_font_desc);

                math::vec2 old_cursor = current_block_.cursor;
                const float tab_step = config_.tab_scale * this->get_scaled_default_space_width();
                if(tab_step > std::numeric_limits<float>::epsilon()) {
                    const float current_pos = current_block_.cursor.*state_.major_p;
                    current_block_.cursor.*state_.major_p = (std::floor(current_pos / tab_step) + 1) * tab_step;
                }

                math::vec2 step_advance{};
                step_advance.*state_.major_p = std::abs(current_block_.cursor.*state_.major_p - old_cursor.*state_.major_p);
                const math::vec2 logical_base = old_cursor + rich_offset;
                const math::frect tab_rect = this->is_vertical()
                    ? math::frect{ tags::from_extent, {logical_base.x - metrics.run_font_asc, logical_base.y}, {metrics.run_font_asc + metrics.run_font_desc, -step_advance.y} }
                    : math::frect{ tags::from_extent, {logical_base.x, logical_base.y - metrics.run_font_asc}, {step_advance.x, metrics.run_font_asc + metrics.run_font_desc} };

                cluster_push_(logical_cluster{current_cluster, 1, tab_rect});
                continue;
            } else if(ch == U' ') {
                if(current_logic_cluster){
	                cluster_push_(*current_logic_cluster);
                	current_logic_cluster.reset();
                }


                current_block_.block_ascender = std::max(current_block_.block_ascender, metrics.run_font_asc);
                current_block_.block_descender = std::max(current_block_.block_descender, metrics.run_font_desc);
                const math::vec2 logical_base = current_block_.cursor + rich_offset;
                const math::frect space_rect = this->is_vertical()
                    ? math::frect{ tags::from_extent, {logical_base.x - metrics.run_font_asc, logical_base.y}, {metrics.run_font_asc + metrics.run_font_desc, -run_advance.y} }
                    : math::frect{ tags::from_extent, {logical_base.x, logical_base.y - metrics.run_font_asc}, {run_advance.x, metrics.run_font_asc + metrics.run_font_desc} };

                cluster_push_(logical_cluster{current_cluster, 1, space_rect});
                current_block_.cursor = this->move_pen(current_block_.cursor, run_advance);
                continue;
            }

            if constexpr (!IsInf.x) {
                const float current_word_w = std::abs(current_block_.cursor.*state_.major_p);
                const float line_w = std::abs(line_buffer_.line_bound.width);
                const float next_w = std::abs(run_advance.*state_.major_p);
                const float max_w = config_.max_extent.*state_.major_p;
                const float indicator_w = config_.has_wrap_indicator() ? std::abs(cached_indicator_.advance.*state_.major_p * this->get_current_relative_scale()) : 0.f;

                if (line_w + current_word_w + next_w > max_w + 0.001f) {
                    if (!line_buffer_.elems.empty() && (current_word_w + next_w + indicator_w <= max_w + 0.001f)) {
                        if (!this->advance_line_impl<IsInf>(results)) return false;
                        if (config_.has_wrap_indicator()) {
                            const float scale = this->get_current_relative_scale();
                            auto scaled_aabb = cached_indicator_.glyph_aabb.copy();
                            scaled_aabb = { tags::unchecked, tags::from_vertex, scaled_aabb.vert_00() * scale, scaled_aabb.vert_11() * scale };
                            const math::vec2 shift_adv = cached_indicator_.advance * scale;
                            current_block_.push_front(
                                scaled_aabb,
                                { scaled_aabb.copy().expand(font::font_draw_expand), graphic::colors::white * .68f, cached_indicator_.texture },
                                shift_adv
                            );
                            if (current_logic_cluster) current_logic_cluster->logical_rect.move(shift_adv);
                            if (active_ul_start) active_ul_start->pos += shift_adv;
                        }
                    } else {
                        if (!current_block_.glyphs.empty()) {
                            if (active_ul_start) {
                                this->submit_underline(*active_ul_start, prev_color, metrics, false);
                                active_ul_start.reset();
                            }
                            if (current_logic_cluster) {
                                cluster_push_(*current_logic_cluster);
                                current_logic_cluster.reset();
                            }

                            line_buffer_.append(current_block_, state_.major_p);
                            current_block_.clear();
                            if (!this->advance_line_impl<IsInf>(results)) return false;

                            if (config_.has_wrap_indicator()) {
                                const float scale = this->get_current_relative_scale();
                                auto scaled_aabb = cached_indicator_.glyph_aabb.copy();
                                scaled_aabb = { tags::unchecked, tags::from_vertex, scaled_aabb.vert_00() * scale, scaled_aabb.vert_11() * scale };
                                current_block_.push_front(
                                    scaled_aabb,
                                    { scaled_aabb.copy().expand(font::font_draw_expand), graphic::colors::white * .68f, cached_indicator_.texture },
                                    cached_indicator_.advance * scale
                                );
                            }

                        	if (is_underline_enabled_()) {
                        		active_ul_start = ul_start_info{current_block_.cursor, this->get_current_gap_index(false)};
                        	}
                        }
                    }
                }
            }

            if(ch != U'\n' && ch != U'\r') {
                current_block_.block_ascender = math::max(current_block_.block_ascender, asc, metrics.run_font_asc);
                current_block_.block_descender = math::max(current_block_.block_descender, desc, metrics.run_font_desc);
            }

            if(this->is_reversed()) current_block_.cursor = this->move_pen(current_block_.cursor, run_advance);

            const math::vec2 logical_base = current_block_.cursor + rich_offset;
            const math::frect advance_rect = this->is_vertical()
                ? math::frect{ tags::from_extent, {logical_base.x - metrics.run_font_asc, logical_base.y}, {metrics.run_font_asc + metrics.run_font_desc, -run_advance.y} }
                : math::frect{ tags::from_extent, {logical_base.x, logical_base.y - metrics.run_font_asc}, {run_advance.x, metrics.run_font_asc + metrics.run_font_desc} };

            const auto next_cluster = (i + 1 < len) ? infos[i + 1].cluster : (start + length);
            const auto span = next_cluster > current_cluster ? next_cluster - current_cluster : 1;

            if(current_logic_cluster && current_logic_cluster->cluster_index == current_cluster) {
                current_logic_cluster->logical_rect.expand_by(advance_rect);
                current_logic_cluster->cluster_span = std::max(current_logic_cluster->cluster_span, span);
            } else {
                if(current_logic_cluster) cluster_push_(*current_logic_cluster);
                current_logic_cluster = logical_cluster{current_cluster, span, advance_rect};
            }

            const math::vec2 glyph_local_draw_pos = {
                font::normalize_len(pos[i].x_offset) * metrics.run_scale_factor.x + rich_offset.x,
                -font::normalize_len(pos[i].y_offset) * metrics.run_scale_factor.y + rich_offset.y
            };

            const math::vec2 visual_base_pos = current_block_.cursor + glyph_local_draw_pos;
            const math::frect actual_aabb = loaded_glyph.metrics().place_to(visual_base_pos, metrics.run_scale_factor);
            const math::frect draw_aabb = actual_aabb.copy().expand(font::font_draw_expand * metrics.run_scale_factor);

            current_block_.push_back(actual_aabb, { draw_aabb, prev_color, std::move(loaded_glyph) });

            if(!this->is_reversed()) current_block_.cursor = this->move_pen(current_block_.cursor, run_advance);
        }

        if(current_logic_cluster) cluster_push_(*current_logic_cluster);
        if(active_ul_start) this->submit_underline(*active_ul_start, prev_color, metrics, true);

        if constexpr (enable_dynamic_rich_text_state) {
        	rich_text_state& rtstate = rich_text_state_;

            if(rtstate.next_apply_pos < start + length) {
                for(auto p = rtstate.next_apply_pos; p < start + length; ++p) {
                    auto tokens = full_text.get_token_group(p, full_text.get_init_token());
                    if(!tokens.empty()) {
                        rtstate.rich_context.update(*manager_,
                            { state_.default_font_size, current_block_.block_ascender, current_block_.block_descender, state_.is_vertical_mode },
                            config_.rich_text_fallback_style, tokens, context_update_mode::soft_only);
                        rtstate.token_soft_last = tokens.end();
                    }
                }
            }
        }

        return true;
    }

    template<math::bool2 IsInf>
    bool advance_line_impl(glyph_layout& results) noexcept {
        const float scale = this->get_current_relative_scale();
        const float min_asc = state_.default_ascender * scale;
        const float min_desc = state_.default_descender * scale;

        const float current_asc = std::max(min_asc, line_buffer_.line_bound.ascender);
        const float current_desc = std::max(min_desc, line_buffer_.line_bound.descender);
        float next_baseline = state_.current_baseline_pos;

        if(state_.is_first_line) {
            next_baseline = current_asc;
        } else {
            const float metrics_sum = state_.prev_line_descender + current_asc;
            next_baseline += metrics_sum * config_.line_spacing_scale + config_.line_spacing_fixed_distance;
        }

        if constexpr (!IsInf.x) {
            const float container_width = config_.max_extent.*state_.major_p;
            if(line_buffer_.line_bound.width > container_width + 0.001f) return false;
        }

        math::vec2 offset_vec{};
        offset_vec.*state_.major_p = {};
        offset_vec.*state_.minor_p = next_baseline;

        if(!line_buffer_.elems.empty()) {
            const float visual_min_y = line_buffer_.pos_min.*state_.minor_p + offset_vec.*state_.minor_p;
            const float visual_max_y = line_buffer_.pos_max.*state_.minor_p + offset_vec.*state_.minor_p;
            const float logical_min_y = offset_vec.*state_.minor_p - current_asc;
            const float logical_max_y = offset_vec.*state_.minor_p + current_desc;

            const float line_min_y = std::min(visual_min_y, logical_min_y);
            const float line_max_y = std::max(visual_max_y, logical_max_y);
            const float global_min_y = std::min(state_.min_bound.*state_.minor_p, line_min_y);
            const float global_max_y = std::max(state_.max_bound.*state_.minor_p, line_max_y);

            if constexpr (!IsInf.y) {
                if((global_max_y - global_min_y) > config_.max_extent.*state_.minor_p + 0.001f) {
                    return false;
                }
            }
        } else {
            if constexpr (!IsInf.y) {
                if(next_baseline + current_desc > config_.max_extent.*state_.minor_p) return false;
            }
        }

        state_.current_baseline_pos = next_baseline;
        state_.is_first_line = false;

        line new_line;
        new_line.start_pos = offset_vec;
        new_line.rect = line_buffer_.line_bound;
        new_line.glyph_range = subrange(results.elems.size(), line_buffer_.elems.size());
        new_line.underline_range = subrange(results.underlines.size(), line_buffer_.underlines.size());
		if constexpr (enable_cluster_record){
			new_line.cluster_range = subrange(results.clusters.size(), line_buffer_.clusters->size());
		}
        

        for(auto& elem : line_buffer_.elems) {
            state_.min_bound.min(elem.aabb.vert_00() + offset_vec);
            state_.max_bound.max(elem.aabb.vert_11() + offset_vec);
        }
		results.elems.append_range(line_buffer_.elems | std::views::as_rvalue);

        for(auto& ul : line_buffer_.underlines) {
            math::vec2 ul_min = math::min(ul.start, ul.end) + offset_vec;
            math::vec2 ul_max = math::max(ul.start, ul.end) + offset_vec;
            ul_min.y -= ul.thickness / 2.f;
            ul_max.y += ul.thickness / 2.f;

            state_.min_bound.min(ul_min);
            state_.max_bound.max(ul_max);
        }
		results.underlines.append_range(line_buffer_.underlines | std::views::as_rvalue);

        math::vec2 line_logical_min = offset_vec;
        line_logical_min.*state_.minor_p = offset_vec.*state_.minor_p - current_asc;
        line_logical_min.*state_.major_p = offset_vec.*state_.major_p;

        math::vec2 line_logical_max = offset_vec;
        line_logical_max.*state_.minor_p = offset_vec.*state_.minor_p + current_desc;
        line_logical_max.*state_.major_p = offset_vec.*state_.major_p + line_buffer_.line_bound.width;

        state_.min_bound.min(line_logical_min);
        state_.max_bound.max(line_logical_max);

        if constexpr (enable_cluster_record) {
        	results.clusters.append_range(*line_buffer_.clusters);
        }

        results.lines.push_back(std::move(new_line));
        state_.prev_line_descender = current_desc;
        line_buffer_.clear();

        return true;
    }

    template<math::bool2 IsInf>
    bool flush_block_impl(glyph_layout& results) {
        if(current_block_.glyphs.empty() && current_block_.underlines.empty() && current_block_.clusters.empty()) return true;

        auto check_block_fit = [&](const line_buffer_base& buffer, const layout_block_base& blk) -> bool {
            const float predicted_width = buffer.line_bound.width + blk.cursor.*state_.major_p;
            if constexpr (!IsInf.x) {
                if(predicted_width > config_.max_extent.*state_.major_p + 0.001f) {
                    return false;
                }
            }

            const float scale = this->get_current_relative_scale();
            const float min_asc = state_.default_ascender * scale;
            const float min_desc = state_.default_descender * scale;

            float asc = std::max({buffer.line_bound.ascender, blk.block_ascender, min_asc});
            float desc = std::max({buffer.line_bound.descender, blk.block_descender, min_desc});
            float estimated_baseline = state_.current_baseline_pos;

            if(!state_.is_first_line && buffer.elems.empty()) {
                estimated_baseline += (state_.prev_line_descender + asc) * config_.line_spacing_scale + config_.line_spacing_fixed_distance;
            }

            if constexpr (!IsInf.y) {
                const float predicted_bottom = estimated_baseline + desc;
                const float predicted_top = estimated_baseline - asc;
                if((std::max(state_.max_bound.*state_.minor_p, predicted_bottom) -
                    std::min(state_.min_bound.*state_.minor_p, predicted_top)) > config_.max_extent.*state_.minor_p + 0.001f) {
                    return false;
                }
            }
            return true;
        };

        if(check_block_fit(line_buffer_, current_block_)) {
            line_buffer_.append(current_block_, state_.major_p);
            current_block_.clear();
            return true;
        }

        if(!this->advance_line_impl<IsInf>(results)) return false;

        if(config_.has_wrap_indicator()) {
            const float scale = this->get_current_relative_scale();
            auto scaled_aabb = cached_indicator_.glyph_aabb.copy();
            scaled_aabb = { tags::unchecked, tags::from_vertex, scaled_aabb.vert_00() * scale, scaled_aabb.vert_11() * scale };
            current_block_.push_front(
                scaled_aabb,
                { scaled_aabb.copy().expand(font::font_draw_expand), graphic::colors::white * .68f, cached_indicator_.texture },
                cached_indicator_.advance * scale
            );
        }

        if(check_block_fit(line_buffer_, current_block_)) {
            line_buffer_.append(current_block_, state_.major_p);
        } else {
            return false;
        }

        current_block_.clear();
        return true;
    }

    template<math::bool2 IsInf>
    bool process_layout_impl(const tokenized_text_view& full_text, glyph_layout& results) {
        typst_szt current_idx = 0;
        typst_szt run_start = 0;
        font::font_face_handle* current_face = nullptr;

        auto resolve_face = [&](font::font_face_handle* current, char32_t codepoint) -> font::font_face_handle*{
            if(current && current->index_of(codepoint)) return current;
            const auto* family = get_font_view_();
            auto face_view = manager_->use_family(family);
            auto [best_face, _] = face_view.find_glyph_of(codepoint);
            return best_face;
        };

        while(current_idx < full_text.get_text().size()) {

            if constexpr (enable_dynamic_rich_text_state) {
            	rich_text_state& rtstate = rich_text_state_;
            	auto tokens = full_text.get_token_group(current_idx, rtstate.token_hard_last);
            	rtstate.token_hard_last = tokens.end();

                if(check_token_group_need_another_run(tokens)) {
                    if(current_face && current_idx > run_start) {
                        if(!this->process_text_run_impl<IsInf>(full_text, results, run_start, current_idx - run_start, *current_face)) return false;
                    }

                    rtstate.rich_context.update(
                        *manager_,
                        update_param{ state_.default_font_size, current_block_.block_ascender, current_block_.block_descender, state_.is_vertical_mode },
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
                    current_face = nullptr;
                    if(!this->flush_block_impl<IsInf>(results)) return false;
                }
            }

            const auto codepoint = full_text.get_text()[current_idx];
            font::font_face_handle* best_face = resolve_face(current_face, codepoint);
            if(!current_face) {
                current_face = best_face;
            } else if(best_face != current_face) {
                if(!this->process_text_run_impl<IsInf>(full_text, results, run_start, current_idx - run_start, *current_face)) return false;
                current_face = best_face;
                run_start = current_idx;
            }
            current_idx++;
        }

        if(current_face) {
            if(!this->process_text_run_impl<IsInf>(full_text, results, run_start, full_text.get_text().size() - run_start, *current_face)) return false;
        }

        return this->flush_block_impl<IsInf>(results) && this->advance_line_impl<IsInf>(results);
    }

public:

    // ==========================================
    // 6. 动态到编译期的桥接入口 (Dynamic Dispatcher)
    // ==========================================
    void layout(glyph_layout& layout_ref, const tokenized_text_view& full_text, font::font_face_view default_font_face = {}) {
        this->initialize_state(full_text, default_font_face);

        layout_ref.elems.clear(); layout_ref.underlines.clear(); layout_ref.clusters.clear(); layout_ref.lines.clear();
        layout_ref.extent = {}; layout_ref.direction = this->get_actual_direction();

        const std::size_t raw_text_size = full_text.get_text().size();
        layout_ref.elems.reserve(raw_text_size);
    	cluster_reserve_(raw_text_size);
        layout_ref.lines.reserve((raw_text_size / 20) + 1);

        // 运行时参数提取
        const bool inf_major = std::isinf(config_.max_extent.*state_.major_p);
        const bool inf_minor = std::isinf(config_.max_extent.*state_.minor_p);

        // 仅在根入口进行 1 次分支派发
        bool result = false;
        if (inf_major && inf_minor) {
            result = this->process_layout_impl<math::bool2{true, true}>(full_text, layout_ref);
        } else if (inf_major && !inf_minor) {
            result = this->process_layout_impl<math::bool2{true, false}>(full_text, layout_ref);
        } else if (!inf_major && inf_minor) {
            result = this->process_layout_impl<math::bool2{false, true}>(full_text, layout_ref);
        } else {
            result = this->process_layout_impl<math::bool2{false, false}>(full_text, layout_ref);
        }
    	layout_ref.is_exhausted = result;

        this->finalize(layout_ref);
    }

    [[nodiscard]] glyph_layout layout(const tokenized_text_view& full_text, font::font_face_view default_font_face = {}){
        glyph_layout results{};
        this->layout(results, full_text, default_font_face);
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

// ==========================================
// 7. 外观模式门面 (Facade Aliases)
// ==========================================

// 等效于重构前：全功能排版器
export using layout_context = layout_context_impl<
    policies::store_clusters,
    policies::rich_text_enabled
>;

// 极速纯文本排版器 (不会维护 cluster，不解析 token 栈，大幅降低内存访问频率)
export using fast_plain_layout_context = layout_context_impl<
    policies::ignore_clusters,
    policies::plain_text_only
>;

constexpr std::size_t sz = sizeof(fast_plain_layout_context);

} // namespace mo_yanxi::typesetting