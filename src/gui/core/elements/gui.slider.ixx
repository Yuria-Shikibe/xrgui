export module mo_yanxi.gui.elem.slider;

export import mo_yanxi.gui.infrastructure;

import mo_yanxi.gui.elem.slider_logic;
import mo_yanxi.snap_shot;
import mo_yanxi.math;
import mo_yanxi.gui.style.interface;
import std;

namespace mo_yanxi::gui {

export template <std::size_t Dim, typename Derived> struct slider_base;
export struct slider1d;
export struct slider2d;

namespace style {
    export struct slider1d_drawer : style_drawer<slider1d> {
        using style_drawer::style_drawer;
    };
    
    export struct default_slider1d_drawer : slider1d_drawer {
        [[nodiscard]] constexpr default_slider1d_drawer()
        : slider1d_drawer(tags::persistent, layer_top_only) {}

    protected:
        void draw_layer_impl(
            const slider1d& element,
            math::frect region,
            float opacityScl,
            fx::layer_param layer_param) const override;
    };

    export struct slider2d_drawer : style_drawer<slider2d> {
        using style_drawer::style_drawer;
    };

    export struct default_slider2d_drawer : slider2d_drawer {
        [[nodiscard]] constexpr default_slider2d_drawer()
        : slider2d_drawer(tags::persistent, layer_top_only) {}

    protected:
        void draw_layer_impl(
            const slider2d& element,
            math::frect region,
            float opacityScl,
            fx::layer_param layer_param) const override;
    };
}

// ==========================================
// 泛型滑动条基类 (CRTP)
// ==========================================
export template <std::size_t Dim, typename Derived>
struct slider_base : elem {
    static_assert(Dim == 1 || Dim == 2, "Only 1D and 2D sliders are supported");

    using value_type = float;
    using logic_type = slider_nd<value_type, Dim>;
    using array_type = std::array<value_type, Dim>;

    logic_type bar;

    float approach_speed_scl = 0.05f;

    // 维度完全与模板参数 Dim 对齐
    array_type scroll_sensitivity;
    array_type sensitivity;
    array_type bar_handle_extent;

protected:
    bool smooth_scroll_{};
    bool smooth_drag_{};
    bool smooth_jump_{};

    std::optional<array_type> drag_src_{std::nullopt};

    // CRTP 静态下行转换
    [[nodiscard]] Derived& derived() noexcept { return static_cast<Derived&>(*this); }
    [[nodiscard]] const Derived& derived() const noexcept { return static_cast<const Derived&>(*this); }

    // 原来的 virtual 接口改为编译期判定并直接完成逻辑
    [[nodiscard]] array_type extract_vec2(math::vec2 v) const noexcept {
        if constexpr (Dim == 1) {
            return {derived().is_vertical() ? v.y : v.x};
        } else {
            return {v.x, v.y};
        }
    }

    [[nodiscard]] math::vec2 expand_to_vec2(const array_type& a) const noexcept {
        if constexpr (Dim == 1) {
            return derived().is_vertical() ? math::vec2{0.0f, a[0]} : math::vec2{a[0], 0.0f};
        } else {
            return {a[0], a[1]};
        }
    }

    void update_approach_state() noexcept {
        if(bar.is_sliding()) this->post_task([](elem& e){util::update_insert(e, update_channel::value_approach);});
        else this->post_task([](elem& e){util::update_erase(e, update_channel::value_approach);});
    }

    void check_apply() {
        if(bar.apply()) on_changed();
        update_approach_state();
    }


    virtual void on_changed() {}

private:
	const auto* get_drawer() const noexcept{
		return static_cast<const Derived&>(*this).get_drawer();
	}

public:

	void record_draw_layer(draw_call_stack_recorder& call_stack_builder) const override{
		elem::record_draw_layer(call_stack_builder);
		if(get_drawer()){
			static_cast<const Derived&>(*this).record_content_drawer_draw_context(
				call_stack_builder,
				[](const Derived& self, draw_call_stack_recorder& r){
					style::record_draw_layer(*self.get_drawer(), r);
				});
		}
	}

    void on_inbound_changed(bool is_inbounded, bool changed) override {
        elem::on_inbound_changed(is_inbounded, changed);
        set_focused_scroll(is_inbounded);
    }

    bool update(float delta_in_ticks) override {
        if(elem::update(delta_in_ticks)) {
            if(!drag_src_ && (smooth_scroll_ || smooth_jump_ || smooth_drag_)) {
                if(bar.update(delta_in_ticks * approach_speed_scl)) on_changed();
                if(!bar.is_sliding()) this->post_task([](elem& e){util::update_erase(e, update_channel::value_approach);});
            }
            return true;
        }
        return false;
    }

    [[nodiscard]] slider_base(scene& scene, elem* parent) : elem(scene, parent) {
        interactivity = interactivity_flag::enabled;
        extend_focus_until_mouse_drop = true;
        scroll_sensitivity.fill(1.0f / 25.0f);
        sensitivity.fill(1.0f);
        bar_handle_extent.fill(10.0f);
    }

    // 编译期内联处理 handle_extent
    [[nodiscard]] math::vec2 get_bar_handle_extent(math::vec2 content_ext) const noexcept {
        if constexpr (Dim == 1) {
            return derived().is_vertical() ? 
                math::vec2{content_ext.x, bar_handle_extent[0]} : 
                math::vec2{bar_handle_extent[0], content_ext.y};
        } else {
            return {bar_handle_extent[0], bar_handle_extent[1]};
        }
    }

    [[nodiscard]] math::vec2 get_bar_handle_extent() const noexcept {
        return get_bar_handle_extent(content_extent());
    }

    [[nodiscard]] math::vec2 get_bar_handle_movable_extent() const noexcept {
        return content_extent().fdim(get_bar_handle_extent());
    }

    [[nodiscard]] bool is_sliding() const noexcept { return bar.is_sliding(); }

    events::op_afterwards on_scroll(const events::scroll event, std::span<elem* const> aboves) override {
        math::vec2 move = event.delta;
        if(input_handle::matched(event.mode, input_handle::mode::shift)) move.swap_xy();

        array_type mov;
        if constexpr (Dim == 1) {
            // 对1D滚动进行泛化：吸收所有的滚轮增量
            mov[0] = -scroll_sensitivity[0] * sensitivity[0] * (move.x + move.y);
        } else {
            mov[0] = -scroll_sensitivity[0] * sensitivity[0] * move.x;
            mov[1] = -scroll_sensitivity[1] * sensitivity[1] * move.y;
        }

        bar.move_minimum_delta(mov, smooth_scroll_);
        if(!smooth_scroll_) check_apply();
        else update_approach_state();

        return events::op_afterwards::intercepted;
    }

    events::op_afterwards on_drag(const events::drag event) override {
        if(event.key.mode_bits == input_handle::mode::ctrl) return events::op_afterwards::fall_through;
        if(!drag_src_) return events::op_afterwards::intercepted;

        if(event.key.as_mouse() == input_handle::mouse::LMB) {
            auto move_arr = extract_vec2(event.delta() / content_extent());
            for (std::size_t i = 0; i < Dim; ++i) {
                move_arr[i] *= sensitivity[i];
            }
            bar.move_progress(move_arr, *drag_src_);
        } else if(event.key.as_mouse() == input_handle::mouse::RMB) {
            bar.resume();
        }

        update_approach_state();
        return events::op_afterwards::intercepted;
    }

    events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override {
        elem::on_click(event, aboves);
        const auto [key, action, mode] = event.key;

        if(static_cast<input_handle::mouse>(key) == input_handle::mouse::_1) {
            if(action == input_handle::act::press) {
                drag_src_ = bar.get_temp_progress();
                if(mode == input_handle::mode::ctrl) {
                    auto diff = event.get_content_pos(*this) - expand_to_vec2(bar.get_progress()) * content_extent();
                    auto move_arr = extract_vec2(diff / content_extent());
                    for (std::size_t i = 0; i < Dim; ++i) {
                        float sign = sensitivity[i] > 0.0f ? 1.0f : (sensitivity[i] < 0.0f ? -1.0f : 0.0f);
                        move_arr[i] *= sign;
                    }
                    bar.move_progress_target(move_arr, smooth_jump_);
                    if(!smooth_jump_) check_apply();
                    else update_approach_state();
                }
            } else if(action == input_handle::act::release) {
                drag_src_.reset();
                if(!smooth_drag_) check_apply();
                else update_approach_state();
            }
        } else if(static_cast<input_handle::mouse>(key) == input_handle::mouse::_2) {
            drag_src_.reset();
            bar.resume();
        }
        return events::op_afterwards::intercepted;
    }

    void set_smooth_scroll(const bool smooth = true) {
        if(util::try_modify(smooth_scroll_, smooth) && !smooth) check_apply();
    }
    void set_smooth_drag(const bool smooth = true) {
        if(util::try_modify(smooth_drag_, smooth) && !smooth) check_apply();
    }
    void set_smooth_jump(const bool smooth = true) {
        if(util::try_modify(smooth_jump_, smooth) && !smooth) check_apply();
    }
};

// ==========================================
// 1D 滑动条特化
// ==========================================
export struct slider1d : slider_base<1, slider1d> {
private:
    bool is_vertical_{false};

    referenced_ptr<const style::slider1d_drawer> drawer_{post_sync_assign(*this, &slider1d::drawer_, [](const slider1d& s){
		return s.get_style_manager().get_default<style::slider1d_drawer>();
	})};

public:
    using slider_base<1, slider1d>::slider_base;

	const style::slider1d_drawer* get_drawer() const noexcept{
		return drawer_.get();
	}

    void set_drawer(referenced_ptr<const style::slider1d_drawer>&& drawer) {
	    if(util::try_modify(drawer_, std::move(drawer))){
		    get_scene().notify_display_state_changed(get_channel());
	    }
    }

    [[nodiscard]] bool is_vertical() const noexcept{
        return is_vertical_;
    }

    void set_vertical(bool vertical) noexcept{
        is_vertical_ = vertical;
    }

    void set_vertical(layout::layout_policy policy) noexcept{
        switch(policy){
        case layout::layout_policy::none: break;
        case layout::layout_policy::hori_major: is_vertical_ = false; break;
        case layout::layout_policy::vert_major: is_vertical_ = true; break;
        }
    }

    [[nodiscard]] float get_progress() const noexcept { return bar.get_progress()[0]; }
    [[nodiscard]] float get_temp_progress() const noexcept { return bar.get_temp_progress()[0]; }

    void set_progress(float progress) {
        if(bar.set_progress(0, progress)) on_changed();
    }

    void set_progress_from_segments(std::uint32_t current, std::uint32_t total) {
        if(bar.set_progress_from_segments(0, current, total)) on_changed();
    }

    void clamp_progress(float from, float to) noexcept {
        bar.clamp({from}, {to});
        check_apply();
    }

	style::cursor_style get_cursor_type(math::vec2 cursor_pos_at_content_local) const noexcept override {
    	if(is_disabled()) return elem::get_cursor_type(cursor_pos_at_content_local);
    	if(cursor_state().pressed || (cursor_pos_at_content_local.axis_greater(vec2{}) && cursor_pos_at_content_local.axis_less(content_extent()))) {
    		if(is_vertical()){
    			return {style::cursor_type::drag, {style::cursor_decoration_type::to_up, style::cursor_decoration_type::to_down}};
    		}else{
    			return {style::cursor_type::drag, {style::cursor_decoration_type::to_left, style::cursor_decoration_type::to_right}};
    		}
    	}
    	return elem::get_cursor_type(cursor_pos_at_content_local);
    }
};

// ==========================================
// 2D 滑动条特化
// ==========================================
export struct slider2d : slider_base<2, slider2d> {
private:
	referenced_ptr<const style::slider2d_drawer> drawer_{post_sync_assign(*this, &slider2d::drawer_, [](const slider2d& s){
		return s.get_style_manager().get_default<style::slider2d_drawer>();
	})};

public:
	using slider_base<2, slider2d>::slider_base;

	const style::slider2d_drawer* get_drawer() const noexcept{
		return drawer_.get();
	}

	void set_drawer(referenced_ptr<const style::slider2d_drawer>&& drawer) {
		if(util::try_modify(drawer_, std::move(drawer))){
			get_scene().notify_display_state_changed(get_channel());
		}
	}

    [[nodiscard]] math::vec2 get_progress() const noexcept { return expand_to_vec2(bar.get_progress()); }
    [[nodiscard]] math::vec2 get_temp_progress() const noexcept { return expand_to_vec2(bar.get_temp_progress()); }

    void set_progress(math::vec2 progress) {
        if(bar.set_progress({progress.x, progress.y})) on_changed();
    }

	style::cursor_style get_cursor_type(math::vec2 cursor_pos_at_content_local) const noexcept override {
    	if(is_disabled()) return elem::get_cursor_type(cursor_pos_at_content_local);
    	if(cursor_state().pressed || (cursor_pos_at_content_local.axis_greater(vec2{}) && cursor_pos_at_content_local.axis_less(content_extent()))) {
    		return {style::cursor_type::drag, {style::cursor_decoration_type::to_left, style::cursor_decoration_type::to_right, style::cursor_decoration_type::to_up, style::cursor_decoration_type::to_down}};
    	}
    	return elem::get_cursor_type(cursor_pos_at_content_local);
    }

    void set_progress_from_segments(math::usize2 current, math::usize2 total) {
        bool c1 = bar.set_progress_from_segments(0, current.x, total.x);
        bool c2 = bar.set_progress_from_segments(1, current.y, total.y);
        if(c1 || c2) on_changed();
    }

    void clamp_progress(math::vec2 from, math::vec2 to) noexcept {
        bar.clamp({from.x, from.y}, {to.x, to.y});
        check_apply();
    }
};

export struct slider1d_with_output : slider1d {
private:
    react_flow::node_holder<react_flow::provider_cached<float>> output_node;
public:
    slider1d_with_output(scene& scene, elem* parent) : slider1d(scene, parent) {}

    react_flow::provider_cached<float>& get_provider() noexcept { return output_node.node; }

protected:
    void on_changed() override { output_node->update_value(get_progress()); }
};

export struct slider2d_with_output : slider2d {
private:
    react_flow::node_holder<react_flow::provider_cached<math::vec2>> output_node;
public:
    slider2d_with_output(scene& scene, elem* parent) : slider2d(scene, parent) {}

    react_flow::provider_cached<math::vec2>& get_provider() noexcept { return output_node.node; }

protected:
    void on_changed() override { output_node->update_value(get_progress()); }
};

} // namespace mo_yanxi::gui