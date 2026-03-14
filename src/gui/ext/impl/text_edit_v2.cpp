module mo_yanxi.gui.elem.text_edit_v2;

import mo_yanxi.unicode;
import mo_yanxi.graphic.draw.instruction;

namespace mo_yanxi::gui {

bool text_edit_v2::update(float delta_in_ticks) {
    if (!label_v2::update(delta_in_ticks)) return false;

    // 移除了这里针对 Hint 的轮询逻辑，由事件驱动代替
    if (failed_hint_timer_ > 0.f) {
        failed_hint_timer_ -= delta_in_ticks;
    }

    if (!is_focused_key() && core_.get_caret().has_region()) {
        core_.action_move_left(false);
    }

    if (is_focused_key()) {
        caret_blink_timer_ += delta_in_ticks;
        if (caret_blink_timer_ > 60.f) caret_blink_timer_ = 0.f;
    }

    return true;
}

void text_edit_v2::draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const {
    draw_style(param);
    if (param != 0) return;

    draw_selection_and_caret();
    label_v2::draw_text();
}

void text_edit_v2::draw_selection_and_caret() const {
    if (!is_focused_key()) return;

    auto caret = core_.get_caret();
    auto ordered = caret.get_ordered();
    bool has_sel = caret.has_region();
    bool show_caret = caret_blink_timer_ < 30.f;

    if (!has_sel && !show_caret) return;

    using namespace graphic::draw::instruction;
    auto& r = renderer();
    auto off = get_glyph_src_abs();

    const graphic::color selection_color = (is_failed() ? graphic::colors::red_dusted : graphic::colors::gray).copy().mul_a(0.65f);
    const graphic::color caret_color = (is_failed() ? graphic::colors::red_dusted : graphic::colors::white).copy().mul_a(get_draw_opacity());

    // 修复空文本状态下的光标高度问题
    if (glyph_layout_.lines.empty()) {
        if (show_caret) {
            float fallback_height = context_.get_config().get_default_font_size().y;
            if (fallback_height < 1.0f) fallback_height = 16.0f; // 极简兜底

            math::vec2 ext = {2.0f, fallback_height};
            r.push(rect_aabb { .v00 = off, .v11 = off + ext, .vert_color = {caret_color} });
        }
        return;
    }

    bool caret_drawn = false;
    math::vec2 final_caret_pos{};
    math::vec2 final_caret_ext{};

    std::size_t next_line_start_idx = 0;
    auto text_u32 = core_.get_text();

    bool sel_started = false;
    float first_min_x = 0.f, first_top_y = 0.f, first_bottom_y = 0.f;
    float last_max_x = 0.f, last_top_y = 0.f, last_bottom_y = 0.f;
    float global_min_x = off.x;
    float global_max_x = off.x + glyph_layout_.extent.x;
    std::size_t first_line_idx = 0;
    std::size_t last_line_idx = 0;

    for (std::size_t line_idx = 0; line_idx < glyph_layout_.lines.size(); ++line_idx) {
        const auto& line = glyph_layout_.lines[line_idx];
        auto align_res = line.calculate_alignment(glyph_layout_.extent, text_line_align, glyph_layout_.direction);
        math::vec2 line_src = align_res.start_pos + off;

        float line_height = line.rect.ascender + line.rect.descender;
        float line_top_y = -line.rect.ascender;

        // 修复行高异常问题
        if (line_height < 0.1f) {
            float fallback = context_.get_config().get_default_font_size().y;
            if (fallback < 1.0f) fallback = 16.0f;
            line_height = fallback;
            line_top_y = -fallback * 0.8f;
        }

        std::size_t line_start_idx = next_line_start_idx;
        std::size_t line_end_idx = line_start_idx;

        if (line.cluster_range.size > 0) {
            line_start_idx = glyph_layout_.clusters[line.cluster_range.pos].cluster_index;
            const auto& last_cluster = glyph_layout_.clusters[line.cluster_range.pos + line.cluster_range.size - 1];
            line_end_idx = last_cluster.cluster_index + last_cluster.cluster_span;
        }
        next_line_start_idx = line_end_idx;

        if (has_sel) {
            bool line_has_sel = false;
            float sel_min_x = std::numeric_limits<float>::max();
            float sel_max_x = std::numeric_limits<float>::lowest();
            bool extend_to_edge = false;

            if (line.cluster_range.size == 0) {
                if (ordered.src <= line_start_idx && ordered.dst > line_start_idx) {
                    line_has_sel = true;
                    sel_min_x = 0.0f;
                    sel_max_x = 8.0f;
                    extend_to_edge = true;
                }
            } else {
                for (std::size_t i = 0; i < line.cluster_range.size; ++i) {
                    const auto& cluster = glyph_layout_.clusters[line.cluster_range.pos + i];
                    if (cluster.cluster_index >= ordered.src && cluster.cluster_index < ordered.dst) {
                        line_has_sel = true;
                        sel_min_x = std::min(sel_min_x, cluster.logical_rect.vert_00().x);
                        sel_max_x = std::max(sel_max_x, cluster.logical_rect.vert_11().x);

                        if (cluster.cluster_index < text_u32.size() && (text_u32[cluster.cluster_index] == U'\n' || text_u32[cluster.cluster_index] == U'\r')) {
                            extend_to_edge = true;
                        }
                    }
                }

                if (ordered.dst >= next_line_start_idx && ordered.dst > line_start_idx) {
                    extend_to_edge = true;
                }
            }

            if (line_has_sel) {
                float abs_min_x = line_src.x + sel_min_x;
                float abs_max_x = line_src.x + sel_max_x;
                float abs_top_y = line_src.y + line_top_y;
                float abs_bottom_y = line_src.y + line_top_y + line_height;

                if (extend_to_edge) {
                    float layout_right_edge = off.x + glyph_layout_.extent.x;
                    abs_max_x = std::max({abs_max_x, layout_right_edge, abs_min_x + 8.0f});
                    global_max_x = std::max(global_max_x, abs_max_x);
                }

                if (!sel_started) {
                    sel_started = true;
                    first_line_idx = line_idx;
                    first_min_x = abs_min_x;
                    first_top_y = abs_top_y;
                    first_bottom_y = abs_bottom_y;
                }

                last_line_idx = line_idx;
                last_max_x = abs_max_x;
                last_top_y = abs_top_y;
                last_bottom_y = abs_bottom_y;
            }
        }

        if (show_caret && !caret_drawn) {
            for (std::size_t i = 0; i < line.cluster_range.size; ++i) {
                const auto& cluster = glyph_layout_.clusters[line.cluster_range.pos + i];
                if (cluster.cluster_index == caret.dst) {
                    final_caret_ext = {2.0f, line_height};
                    final_caret_pos = line_src + math::vec2{cluster.logical_rect.src.x, line_top_y};
                    caret_drawn = true;
                    break;
                }
            }

            if (!caret_drawn && caret.dst == line_end_idx) {
                bool is_last_line = (line_idx == glyph_layout_.lines.size() - 1);
                bool ends_with_newline = (caret.dst > 0 && caret.dst <= text_u32.size() && text_u32[caret.dst - 1] == U'\n');

                if (line.cluster_range.size == 0) {
                    final_caret_ext = {2.0f, line_height};
                    final_caret_pos = line_src + math::vec2{0.0f, line_top_y};
                    caret_drawn = true;
                } else if (is_last_line || !ends_with_newline) {
                    const auto& last_cluster = glyph_layout_.clusters[line.cluster_range.pos + line.cluster_range.size - 1];
                    final_caret_ext = {2.0f, line_height};
                    final_caret_pos = line_src + math::vec2{last_cluster.logical_rect.get_end_x(), line_top_y};
                    caret_drawn = true;
                }
            }
        }
    }

    if (sel_started) {
        if (first_line_idx == last_line_idx) {
            r.push(rect_aabb {
                .v00 = { first_min_x, first_top_y },
                .v11 = { last_max_x, last_bottom_y },
                .vert_color = {selection_color}
            });
        } else {
            r.push(rect_aabb {
                .v00 = { first_min_x, first_top_y },
                .v11 = { global_max_x, first_bottom_y },
                .vert_color = {selection_color}
            });
            if (last_top_y > first_bottom_y) {
                r.push(rect_aabb {
                    .v00 = { global_min_x, first_bottom_y },
                    .v11 = { global_max_x, last_top_y },
                    .vert_color = {selection_color}
                });
            }
            r.push(rect_aabb {
                .v00 = { global_min_x, last_top_y },
                .v11 = { last_max_x, last_bottom_y },
                .vert_color = {selection_color}
            });
        }
    }

    if (show_caret && caret_drawn) {
        r.push(rect_aabb {
            .v00 = final_caret_pos,
            .v11 = final_caret_pos + final_caret_ext,
            .vert_color = {caret_color}
        });
    }
}

events::op_afterwards text_edit_v2::on_click(const events::click event, std::span<elem* const> aboves) {
    if (!event.key.on_release()) {
        set_focus(true); // 保证获取焦点，并处理了 Idle 的清理工作

        math::vec2 localpos = event.pos - get_glyph_src_local();
        core_.action_hit_test(glyph_layout_, localpos, text_line_align, false);
        reset_blink();
    }
    return elem::on_click(event, aboves);
}

events::op_afterwards text_edit_v2::on_drag(const events::drag event) {
    set_focus(true); // 保证拖拽期间焦点处于激活状态
    math::vec2 localpos = event.dst - get_glyph_src_local();
    core_.action_hit_test(glyph_layout_, localpos, text_line_align, true);
    reset_blink();
    return events::op_afterwards::intercepted;
}

events::op_afterwards text_edit_v2::on_unicode_input(const char32_t val) {
    if (prohibited_codes_.contains(val)) {
        set_input_invalid();
    } else {
        std::u32string buf{val};
        if (core_.insert_text(buf)) {
            sync_view_from_core();
            reset_blink();
        }
    }
    return events::op_afterwards::intercepted;
}

events::op_afterwards text_edit_v2::on_key_input(const input_handle::key_set key) {
    return events::op_afterwards::intercepted;
}

events::op_afterwards text_edit_v2::on_esc() {
    if (elem::on_esc() == events::op_afterwards::fall_through) {
        set_focus(false);
    }
    return events::op_afterwards::intercepted;
}

graphic::color text_edit_v2::get_text_draw_color() const noexcept {
    if (is_idle_) {
        return graphic::colors::light_gray.copy().mul_a(0.65f * get_draw_opacity());
    }
    return label_v2::get_text_draw_color();
}

void text_edit_v2::sync_view_from_core() {
    auto u32str = core_.get_text();
    // 依赖父类的机制自动触发重排
    label_v2::set_text(unicode::utf32_to_utf8(u32str));
}

void text_edit_v2::set_focus(bool keyFocused) {
    static constexpr std::string_view key_binding_name{"_text_edit_v2"};
    set_focused_key(keyFocused);

    if (keyFocused) {
        if (is_idle_) {
            is_idle_ = false;
            label_v2::set_text(std::string_view{});
        }

        if (auto map = get_scene().find_input(key_binding_name)) {
            auto& kmap = dynamic_cast<text_edit_v2_key_binding&>(*map);
            kmap.set_context(std::ref(*this));
            kmap.set_activated(true);
        } else {
            auto& kmap = get_scene().get_inputs().register_sub_input<text_edit_v2_key_binding>(key_binding_name);
            load_default_text_edit_v2_key_binding(kmap);
            kmap.set_context(std::ref(*this));
            kmap.set_activated(true);
        }
    } else {
        if (core_.get_text().empty()) {
            is_idle_ = true;
            label_v2::set_text(hint_text_when_idle_);
        }

        if (auto map = get_scene().find_input(key_binding_name)) {
            auto& kmap = dynamic_cast<text_edit_v2_key_binding&>(*map);
            auto [host] = kmap.get_context();
            if (host && &host.get() == this) {
                kmap.set_context(text_edit_v2_key_binding::context_tuple_t{});
                kmap.set_activated(false);
            }
        }
    }
}

// --- 剪贴板支持 ---
void text_edit_v2::action_copy() const {
    auto caret = core_.get_caret();
    if (!caret.has_region()) return;
    auto ordered = caret.get_ordered();

    if (const auto cmt = get_scene().get_communicator()) {
        auto sel_u32 = core_.get_text().substr(ordered.src, ordered.length());
        cmt->set_clipboard(unicode::utf32_to_utf8(sel_u32));
    }
}

void text_edit_v2::action_paste() {
    if (const auto cmt = get_scene().get_communicator()) {
        const auto str = cmt->get_clipboard();
        if (!str.empty()) {
            auto rst = unicode::utf8_to_utf32(str);
            action_do_insert(std::u32string_view{rst.data(), rst.size()});
        }
    }
}

void text_edit_v2::action_cut() {
    action_copy();
    action_do_delete();
}

// --- Key Bindings 加载 ---
template <auto Fn, auto ...Args>
requires (std::invocable<decltype(Fn), text_edit_v2&, decltype(Args)...>)
consteval auto make_bind() noexcept {
    return [](input_handle::key_set key, float, text_edit_v2& text_edit) static {
        std::invoke(Fn, text_edit, Args...);
    };
}

void load_default_text_edit_v2_key_binding(text_edit_v2_key_binding& bind) {
    using namespace input_handle;

    auto add = [&](key key_enum, mode mode_enum, auto func) {
        bind.add_binding(key_set{key_enum, act::press, mode_enum}, func);
        bind.add_binding(key_set{key_enum, act::repeat, mode_enum}, func);
    };

    auto add_deduced = [&](key key_enum, auto func) {
        bind.add_binding(key_set{key_enum, act::press}, func);
        bind.add_binding(key_set{key_enum, act::repeat}, func);
    };

    add_deduced(key::right, [](key_set key, float, text_edit_v2& self) static {
        self.action_move_right(matched(key.mode_bits, mode::shift), matched(key.mode_bits, mode::ctrl));
    });
    add_deduced(key::left, [](key_set key, float, text_edit_v2& self) static {
        self.action_move_left(matched(key.mode_bits, mode::shift), matched(key.mode_bits, mode::ctrl));
    });

    add_deduced(key::up, [](key_set key, float, text_edit_v2& self) static {
        self.action_move_up(matched(key.mode_bits, mode::shift));
    });
    add_deduced(key::down, [](key_set key, float, text_edit_v2& self) static {
        self.action_move_down(matched(key.mode_bits, mode::shift));
    });

    add_deduced(key::home, [](key_set key, float, text_edit_v2& self) static { self.action_move_line_begin(matched(key.mode_bits, mode::shift)); });
    add_deduced(key::end, [](key_set key, float, text_edit_v2& self) static { self.action_move_line_end(matched(key.mode_bits, mode::shift)); });

    add(key::a, mode::ctrl, make_bind<&text_edit_v2::action_select_all>());
    add(key::c, mode::ctrl, make_bind<&text_edit_v2::action_copy>());
    add(key::v, mode::ctrl, make_bind<&text_edit_v2::action_paste>());
    add(key::x, mode::ctrl, make_bind<&text_edit_v2::action_cut>());
    add(key::z, mode::ctrl, make_bind<&text_edit_v2::undo>());
    add(key::z, mode::ctrl | mode::shift, make_bind<&text_edit_v2::redo>());

    add(key::del, mode::none, make_bind<&text_edit_v2::action_do_delete>());
    add(key::backspace, mode::none, make_bind<&text_edit_v2::action_do_backspace>());
    add(key::enter, mode::ignore, make_bind<&text_edit_v2::action_enter>());
    add(key::tab, mode::none, make_bind<&text_edit_v2::action_tab>());
}

} // namespace mo_yanxi::gui