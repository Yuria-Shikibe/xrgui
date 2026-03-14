module;

#include <cassert>

export module mo_yanxi.gui.elem.text_edit_v2;

export import mo_yanxi.gui.elem.label_v2;
export import mo_yanxi.gui.text_edit_core;
import mo_yanxi.gui.infrastructure;
import mo_yanxi.graphic.color;
import mo_yanxi.math;
import std;

namespace mo_yanxi::gui {

export struct text_edit_v2_key_binding;

export struct text_edit_v2 : label_v2 {
public:
    [[nodiscard]] text_edit_v2(scene& scene, elem* parent)
        : label_v2(scene, parent) {
        interactivity = interactivity_flag::enabled;
        extend_focus_until_mouse_drop = true;

        // 初始状态下直接显示 Hint 占位符
        label_v2::set_text(hint_text_when_idle_);
    }

    ~text_edit_v2() override {
        set_focus(false);
    }

    // --- 文本同步与状态管理 ---
    template <typename StrTy>
        requires std::constructible_from<std::string, StrTy&&>
    void set_hint_text(StrTy&& ty) {
        hint_text_when_idle_ = std::forward<StrTy>(ty);
        if (is_idle_) {
            label_v2::set_text(hint_text_when_idle_);
        }
    }

    void set_banned_characters(std::initializer_list<char32_t> chars) {
        prohibited_codes_ = chars;
    }

    void add_file_banned_characters() {
        prohibited_codes_.insert_range(std::initializer_list<char32_t>{U'<', U'>', U':', U'"', U'/', U'\\', U'|', U'*', U'?'});
    }

    [[nodiscard]] std::string_view get_text() const noexcept override {
        if (is_idle_) return {};
        return label_v2::get_text();
    }

    [[nodiscard]] bool is_idle() const noexcept { return is_idle_; }
    [[nodiscard]] bool is_failed() const noexcept { return failed_hint_timer_ > 0.f; }

    // --- 核心动作封装 ---
    void undo() { if (core_.undo()) sync_view_from_core(); }
    void redo() { if (core_.redo()) sync_view_from_core(); }

    void action_do_insert(std::u32string_view str) {
        if (core_.insert_text(str)) sync_view_from_core();
    }

    void action_do_delete() { if (core_.action_delete()) sync_view_from_core(); }
    void action_do_backspace() { if (core_.action_backspace()) sync_view_from_core(); }
    void action_enter() {
        if (!prohibited_codes_.contains(U'\n')) {
            core_.insert_text(U"\n");
            sync_view_from_core();
        }
    }
    void action_tab() {
        if (!prohibited_codes_.contains(U'\t')) {
            core_.insert_text(U"\t");
            sync_view_from_core();
        }
    }

    void action_move_left(bool select, bool jump) {
        if(jump) core_.action_jump_left(glyph_layout_, select);
        else core_.action_move_left(select);
        reset_blink();
    }

    void action_move_right(bool select, bool jump) {
        if(jump) core_.action_jump_right(glyph_layout_, select);
        else core_.action_move_right(select);
        reset_blink();
    }

    void action_move_up(bool select) {
        core_.action_move_up(glyph_layout_, text_line_align, select);
        reset_blink();
    }
    void action_move_down(bool select) {
        core_.action_move_down(glyph_layout_, text_line_align, select);
        reset_blink();
    }
    void action_move_line_begin(bool select) {
        core_.action_move_line_begin(glyph_layout_, select);
        reset_blink();
    }
    void action_move_line_end(bool select) {
        core_.action_move_line_end(glyph_layout_, select);
        reset_blink();
    }

    void action_select_all() {
        core_.action_select_all();
        reset_blink();
    }

    void action_copy() const;
    void action_paste();
    void action_cut();

protected:
    text_editor_core core_{};
    std::string hint_text_when_idle_{">_..."};
    bool is_idle_{true};
    float failed_hint_timer_{0.f};
    float caret_blink_timer_{0.f};
    std::size_t maximum_units_{std::numeric_limits<std::size_t>::max()};
    mr::heap_uset<char32_t> prohibited_codes_{mr::get_default_heap_allocator()};

    void draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const override;
    graphic::color get_text_draw_color() const noexcept override;

public:
    bool update(float delta_in_ticks) override;

    events::op_afterwards on_drag(const events::drag event) override;
    events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override;
    events::op_afterwards on_key_input(const input_handle::key_set key) override;
    events::op_afterwards on_unicode_input(const char32_t val) override;
    events::op_afterwards on_esc() override;

private:
    void set_focus(bool keyFocused);
    void sync_view_from_core();
    void reset_blink() { caret_blink_timer_ = 0.f; }
    void set_input_invalid() { failed_hint_timer_ = 10.f; }
    void draw_selection_and_caret() const;
};

export struct text_edit_v2_key_binding : input_handle::key_mapping<text_edit_v2&> {
    using key_mapping::key_mapping;
};

export void load_default_text_edit_v2_key_binding(text_edit_v2_key_binding& bind);

} // namespace mo_yanxi::gui