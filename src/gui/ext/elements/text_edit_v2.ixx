module;

#include <cassert>
#include <mo_yanxi/enum_operator_gen.hpp>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.elem.text_edit_v2;

import std;
export import mo_yanxi.gui.text_edit_core;
export import mo_yanxi.gui.text_render_cache;
import mo_yanxi.typesetting.rich_text;
import mo_yanxi.gui.infrastructure;
import mo_yanxi.graphic.color;
import mo_yanxi.math;
import mo_yanxi.unicode;

namespace mo_yanxi::gui {
export constexpr inline std::string_view text_edit_key_binding_name{"_text_edit_bind"};

export struct text_edit_key_binding;

enum struct text_edit_change_type : std::uint32_t {
    none = 0,
    text = 1 << 0,
    max_extent = 1 << 1,
    config = 1 << 2,
};
BITMASK_OPS(export, text_edit_change_type);

struct text_layout_result {
    math::vec2 required_extent;
    bool updated;
};

struct caret_cache{
	math::frect caret_rect_{};
	math::frect selection_rects_[3]{};
	unsigned selection_rect_count_{0};
	caret_section last_cached_caret_{};
};

export struct text_edit_v2 : elem {
protected:

	text_editor_core core_{};
	typesetting::tokenized_text tokenized_text_{};
	typesetting::glyph_layout glyph_layout_{};
	typesetting::layout_context context_{std::in_place};
	text_render_cache render_cache_{};

	layout::expand_policy expand_policy_{};
	text_edit_change_type change_mark_{};
	bool fit_{};
	bool is_idle_{true};
	bool apply_tokens_{false};

	float failed_hint_timer_{};
	float caret_blink_timer_{};

	float on_changed_interval_{30.f};
	float on_changed_timer_{};

	//TODO use static size instead?
	std::u32string hint_text_when_idle_{U">_..."};
	std::size_t maximum_code_points_{std::numeric_limits<std::size_t>::max()};
	mr::heap_uset<char32_t> filter_code_points{mr::get_default_heap_allocator()};
	// 新增：过滤模式标志位，默认 false 为黑名单模式以保持兼容
	bool is_code_filter_whitelist_{false};

	caret_cache caret_cache_{};

public:
    [[nodiscard]] text_edit_v2(scene& scene, elem* parent)
        : elem(scene, parent) {
        get_scene().active_update_elems.insert(this);
        interactivity = interactivity_flag::enabled;
        extend_focus_until_mouse_drop = true;

        // 初始状态下直接显示 Hint 占位符
        set_text_internal(hint_text_when_idle_);
    }

    ~text_edit_v2() override {
        set_focus(false);
    }

    // --- 文本同步与状态管理 ---
    template <typename StrTy>
        requires std::constructible_from<std::u32string, StrTy&&>
    void set_hint_text(StrTy&& ty) {
        hint_text_when_idle_ = std::forward<StrTy>(ty);
        if (is_idle_) {
            set_text_internal(hint_text_when_idle_);
        }
    }

    [[nodiscard]] std::u32string_view get_text() const noexcept {
        if (is_idle_) return {};
        return tokenized_text_.get_text();
    }

    [[nodiscard]] bool is_idle() const noexcept { return is_idle_; }
    [[nodiscard]] bool is_failed() const noexcept { return failed_hint_timer_ > 0.f; }

#pragma region TypeSetting_Settings
    align::pos text_entire_align{align::pos::top_left};

    [[nodiscard]] layout::expand_policy get_expand_policy() const noexcept {
        return expand_policy_;
    }

    void set_expand_policy(const layout::expand_policy expand_policy) {
        if (util::try_modify(expand_policy_, expand_policy)) {
            notify_isolated_layout_changed();
        }
    }

    void set_apply_tokens(const bool allow) {
        if (util::try_modify(apply_tokens_, allow)) {
        	change_mark_ |= text_edit_change_type::text;
            notify_isolated_layout_changed();
        }
    }

    void set_typesetting_config(const typesetting::layout_config& config) {
        if (context_.get_config() != config) {
            context_.set_config(config);
            change_mark_ |= text_edit_change_type::config;
            notify_isolated_layout_changed();
        }
    }

    void set_fit(bool fit = true) {
        if (util::try_modify(fit_, fit)) {
            change_mark_ |= text_edit_change_type::max_extent | text_edit_change_type::config;
            notify_isolated_layout_changed();
        }
    }

    [[nodiscard]] std::optional<graphic::color> get_text_color_scl() const noexcept {
        return render_cache_.get_text_color_scl();
    }

    void set_text_color_scl(const std::optional<graphic::color>& text_color_scl) {
        if (render_cache_.set_text_color_scl(text_color_scl)) {
            render_cache_.update_buffer(glyph_layout_, get_text_draw_color());
        }
    }

    void set_line_align(typesetting::line_alignment align) {
        if (render_cache_.set_line_align(align)) {
             render_cache_.update_buffer(glyph_layout_, get_text_draw_color());
        }
    }
#pragma endregion

#pragma region EditActions
    // --- 核心动作封装 (依赖 tokenized_text 原地 modify) ---
	template <std::invocable<std::u32string&> Action>
	void apply_edit(Action&& action) {
    	bool actually_changed = false;

    	tokenized_text_.modify(apply_tokens_ ? typesetting::tokenize_tag::kep : typesetting::tokenize_tag::raw, [&](std::u32string& text) -> bool {
			std::u32string old_text = text; // 记录修改前的文本
			bool result = std::invoke(std::forward<Action>(action), text);
			if (old_text != text) {
				actually_changed = true; // 确认发生了实际修改
			}
			return result;
		});

    	change_mark_ |= text_edit_change_type::text;
    	notify_isolated_layout_changed();

    	// 触发变更回调逻辑
    	if (actually_changed) {
    		if (on_changed_interval_ > 0.f) {
    			on_changed_timer_ = on_changed_interval_; // 重置计时器以实现防抖
    		} else {
    			on_changed_timer_ = 0.f;
    			on_changed(); // 如果未设置延迟，立即触发
    		}
    	}
    }

    void undo() {
        apply_edit([&](std::u32string& text) { return core_.undo(text); });
    }

    void redo() {
        apply_edit([&](std::u32string& text) { return core_.redo(text); });
    }

    void action_do_insert(std::u32string_view str) {
        apply_edit([&](std::u32string& text) { return core_.insert_text(text, str); });
    }

    void action_do_delete() {
        apply_edit([&](std::u32string& text) { return core_.action_delete(text); });
    }

    void action_do_backspace() {
        apply_edit([&](std::u32string& text) { return core_.action_backspace(text); });
    }

	void action_enter() {
    	// 替换原本的 !prohibited_codes_.contains(U'\n')
    	if (is_character_allowed(U'\n')) {
    		apply_edit([&](std::u32string& text) { return core_.insert_text(text, U"\n"); });
    	}else{
    		set_input_invalid();
    	}
    }

	void action_tab() {
    	// 替换原本的 !prohibited_codes_.contains(U'\t')
    	if (is_character_allowed(U'\t')) {
    		apply_edit([&](std::u32string& text) { return core_.insert_text(text, U"\t"); });
    	}else{
    		set_input_invalid();
    	}
    }

    void action_move_left(bool select, bool jump) {
        if (jump) core_.action_jump_left(glyph_layout_, tokenized_text_.get_text(), select);
        else core_.action_move_left(tokenized_text_.get_text(), select);
        reset_blink();
    }

    void action_move_right(bool select, bool jump) {
        if (jump) core_.action_jump_right(glyph_layout_, tokenized_text_.get_text(), select);
        else core_.action_move_right(tokenized_text_.get_text(), select);
        reset_blink();
    }

    void action_move_up(bool select) {
        core_.action_move_up(glyph_layout_, tokenized_text_.get_text(), render_cache_.get_line_align(), select);
        reset_blink();
    }

    void action_move_down(bool select) {
        core_.action_move_down(glyph_layout_, tokenized_text_.get_text(), render_cache_.get_line_align(), select);
        reset_blink();
    }

    void action_move_line_begin(bool select) {
        core_.action_move_line_begin(glyph_layout_, tokenized_text_.get_text(), select);
        reset_blink();
    }

    void action_move_line_end(bool select) {
        core_.action_move_line_end(glyph_layout_, tokenized_text_.get_text(), select);
        reset_blink();
    }

    void action_select_all() {
        core_.action_select_all(tokenized_text_.get_text());
        reset_blink();
    }

    void action_copy() const;
    void action_paste();
    void action_cut();

#pragma endregion

	void set_banned_characters(std::initializer_list<char32_t> chars) {
		set_character_filter_mode(false);
		filter_code_points = chars;
	}

	// 新增：设置过滤模式
	void set_character_filter_mode(bool is_whitelist) noexcept {
		is_code_filter_whitelist_ = is_whitelist;
	}

	// 新增：核心判定函数
	[[nodiscard]] bool is_character_allowed(char32_t ch) const noexcept {
		bool contains = filter_code_points.contains(ch);
		return is_code_filter_whitelist_ ? contains : !contains;
	}

	void add_file_banned_characters() {
		set_banned_characters(std::initializer_list{U'<', U'>', U':', U'"', U'/', U'\\', U'|', U'*', U'?'});
	}

	[[nodiscard]] std::size_t get_max_code_points() const noexcept {
		return maximum_code_points_;
	}

	void set_max_code_points(std::size_t max_code_points) {
		if (maximum_code_points_ == max_code_points) return;
		maximum_code_points_ = max_code_points;

		// 如果处于激活状态且当前文本超长，则执行截断
		if (!is_idle_ && tokenized_text_.get_text().size() > maximum_code_points_) {
			apply_edit([this](std::u32string& text) {
				text.resize(maximum_code_points_);
				return true;
			});
			// 文本截断后强制将光标移至末尾，避免光标越界
			core_.action_move_line_end(glyph_layout_, tokenized_text_.get_text(), false);
		}
	}

	void set_on_changed_interval(float interval_ticks) noexcept {
		on_changed_interval_ = interval_ticks;
	}

	virtual void on_changed(){

	}

protected:
    void update_caret_cache(); // 核心计算逻辑抽取

    graphic::color get_text_draw_color() const noexcept;

    bool is_layout_expired_() const noexcept {
        return change_mark_ != text_edit_change_type::none;
    }

    bool resize_impl(const math::vec2 size) override;

    std::optional<math::vec2> pre_acquire_size_impl(layout::optional_mastering_extent extent) override;

    text_layout_result layout_text(math::vec2 bound);

    [[nodiscard]] math::vec2 get_glyph_draw_extent() const noexcept {
        if (fit_) {
            return mo_yanxi::gui::align::embed_to(align::scale::fit_smaller, glyph_layout_.extent, content_extent());
        } else {
            return glyph_layout_.extent;
        }
    }

    [[nodiscard]] math::vec2 get_glyph_src_abs() const noexcept {
        const auto sz = get_glyph_draw_extent();
        return mo_yanxi::gui::align::get_offset_of(text_entire_align, sz, content_bound_abs());
    }

    [[nodiscard]] math::vec2 get_glyph_src_local() const noexcept {
        const auto sz = get_glyph_draw_extent();
        return mo_yanxi::gui::align::get_offset_of(text_entire_align, sz, rect{content_extent()});
    }

public:
	void draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const override;

	void layout_elem() override;

    bool update(float delta_in_ticks) override;

    events::op_afterwards on_drag(const events::drag event) override;
    events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override;
    events::op_afterwards on_key_input(const input_handle::key_set key) override;
    events::op_afterwards on_unicode_input(const char32_t val) override;
    events::op_afterwards on_esc() override;

    void update_ime_position() const {
    	//TODO provide better impl
        // if (const auto cmt = get_scene().get_communicator()) {
        //     float fallback = context_.get_config().get_default_font_size().y;
        //     if (fallback < 1.0f) fallback = 16.0f;
        //     auto region = get_caret_local_aabb();
        //     region.move(util::transform_local2scene(*this, get_glyph_src_local()));
        //     cmt->set_ime_cursor_rect(region);
        // }
    }

private:
    void set_focus(bool keyFocused);
    void set_text_internal(std::u32string_view str);

    void reset_blink() {
        caret_blink_timer_ = 0.f;
    }

    void set_input_invalid(){
    	reset_blink();
	    failed_hint_timer_ = 10.f;
    }

    void draw_selection_and_caret() const;
    [[nodiscard]] math::frect get_caret_local_aabb() const;
};

void load_default_text_edit_v2_key_binding(text_edit_key_binding& bind);

struct text_edit_key_binding : input_handle::key_mapping<text_edit_v2&> {
    using key_mapping::key_mapping;

	void use_default_setting(){
		load_default_text_edit_v2_key_binding(*this);
	}
};

export struct text_edit_prov : text_edit_v2{
private:
	struct trans{
		static std::u32string_view operator()(const text_edit_prov* src)  noexcept{
			assert(src != nullptr);
			return src->get_text();
		}
	};
	react_flow::node_holder<react_flow::provider_cached<const text_edit_prov*, std::u32string_view, trans>> prov_;

public:
	[[nodiscard]] text_edit_prov(scene& scene, elem* parent)
		: text_edit_v2(scene, parent){
		prov_->update_value(this);
	}

	template <typename S>
	auto& get_provider(this S& self) noexcept{
		return self.prov_.node;
	}

	void on_changed() override{
		prov_->pull_and_push(false);
	}
};

} // namespace mo_yanxi::gui