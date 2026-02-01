module;

#include <cassert>

module mo_yanxi.gui.elem.scroll_pane;

import mo_yanxi.graphic.draw.instruction;

namespace mo_yanxi::gui{
bool scroll_pane::update(const float delta_in_ticks){
	if(!elem::update(delta_in_ticks))return false;

    // --- [新增] 滚动条淡入淡出逻辑 ---
    if (overlay_scroll_bars_) {
        bool active = false;

        // 1. 如果正在滚动（速度不为0），视为活跃
        if (scrollVelocity.length2() > 0.1f) {
            active = true;
        }
        // 2. 如果正在被拖拽 (通过 scrollVelocity判断通常足够，或者检查 captured 状态)
        // 3. 检查鼠标悬停
        else if (cursor_state().inbound) {
            const auto check_pos = content_src_pos_abs() + last_local_cursor_pos_;

            if (is_hori_scroll_enabled() && get_hori_bar_rect().contains(check_pos)) active = true;
            if (is_vert_scroll_enabled() && get_vert_bar_rect().contains(check_pos)) active = true;
        }

        if (active) {
            activity_timer_ = 0.0f;
        } else {
            activity_timer_ += delta_in_ticks;
        }

        // 计算目标透明度
        float target_opacity = 0.0f;
        if (activity_timer_ < fade_delay_ticks) {
            target_opacity = 1.0f;
        } else {
            float fade_progress = (activity_timer_ - fade_delay_ticks) / fade_duration_ticks;
            target_opacity = 1.0f - math::clamp(fade_progress, 0.0f, 1.0f);
        }

        // 简单的平滑过渡
        math::approach_inplace(bar_opacity_, target_opacity, delta_in_ticks * 0.2f);
    } else {
        bar_opacity_ = 1.0f;
        activity_timer_ = 0.0f;
    }

	{//scroll update
        // (保持原有逻辑)
		scrollVelocity.lerp_inplace(scrollTargetVelocity, delta_in_ticks * VelocitySensitivity);
		scrollTargetVelocity.lerp_inplace({}, delta_in_ticks * VelocityDragSensitivity);

		if(util::try_modify(
			scroll.base,
			math::fma(scrollVelocity, delta_in_ticks, scroll.base).clamp_xy({}, scrollable_extent()) * get_vel_clamp())){
			scroll.resume();
			updateChildrenAbsSrc();

            // [新增] 只要发生位置变化，就视为活跃
            activity_timer_ = 0.f;
		}
	}

	if(item)item->update(delta_in_ticks);

	return true;
}

void scroll_pane::draw_layer(rect clipSpace, gfx_config::layer_param_pass_t param) const{
	elem::draw_layer(clipSpace, param);

	auto& r = get_scene().renderer();

	const bool activeHori = is_hori_scroll_active();
	const bool activeVert = is_vert_scroll_active();
    const bool logicHori = is_hori_scroll_enabled();
    const bool logicVert = is_vert_scroll_enabled();

	assert(item);

    // [逻辑不变] 只有在内容真正溢出时，才开启 Scissor 和 Transform
	if(activeHori || activeVert){
		scissor_guard guard{r, {get_viewport()}};
		transform_guard transform_guard{r, math::mat3{}.idt().set_translation(-scroll.temp)};
		item->draw_layer(clipSpace.move(scroll.temp), param);
	}else{
		item->draw_layer(clipSpace, param);
	}

    // --- 滚动条绘制 ---
	if(param == 0 && bar_opacity_ > 0.01f){ // [优化] 完全透明时不绘制
        // 获取颜色并应用透明度
        auto get_color = [&](auto base_color) {
            auto c = base_color;
            c.a *= bar_opacity_;
            return c;
        };

		if(logicHori){
			float shrink = scroll_bar_stroke_ * .25f;
			auto bar_rect = get_hori_bar_rect();
			bar_rect.add_height(-shrink);

            if(activeHori) {
			    r.push(graphic::draw::instruction::rect_aabb{
				    .v00 = bar_rect.vert_00(),
				    .v11 = bar_rect.vert_11(),
				    .vert_color = get_color(graphic::colors::gray) // [修改] 应用透明度
			    });
            } else if (draw_track_if_locked) {
                // (锁定状态绘制逻辑，同样应用透明度)
                const auto [x, y] = content_src_pos_abs();
                const auto barSize = get_bar_extent();
                float fullWidth = get_viewport_extent().x;

                gui::rect lockedRect = {
                    x,
                    y - barSize.y + content_height(),
                    fullWidth,
                    barSize.y
                };
                lockedRect.add_height(-shrink); 

                r.push(graphic::draw::instruction::rect_aabb{
                    .v00 = lockedRect.vert_00(),
                    .v11 = lockedRect.vert_11(),
                    .vert_color = get_color(graphic::colors::light_gray) // [修改] 应用透明度
                });
            }
		}

        // --- 垂直滚动条绘制 ---
		if(logicVert){
			float shrink = scroll_bar_stroke_ * .25f;
			auto bar_rect = get_vert_bar_rect();
			bar_rect.add_width(-shrink);

            if(activeVert) {
			    r.push(graphic::draw::instruction::rect_aabb{
				    .v00 = bar_rect.vert_00(),
				    .v11 = bar_rect.vert_11(),
				    .vert_color = get_color(graphic::colors::gray) // [修改] 应用透明度
			    });
            } else if (draw_track_if_locked) {
                const auto [x, y] = content_src_pos_abs();
                const auto barSize = get_bar_extent();
                float fullHeight = get_viewport_extent().y;

                gui::rect lockedRect = {
                    x - barSize.x + content_width(),
                    y,
                    barSize.x,
                    fullHeight
                };
                lockedRect.add_width(-shrink);

                r.push(graphic::draw::instruction::rect_aabb{
                    .v00 = lockedRect.vert_00(),
                    .v11 = lockedRect.vert_11(),
                    .vert_color = get_color(graphic::colors::light_gray) // [修改] 应用透明度
                });
            }
		}
	}
}

// --- 事件处理：操作时唤醒滚动条 ---

events::op_afterwards scroll_pane::on_scroll(const events::scroll e, std::span<elem* const> aboves){
    activity_timer_ = 0.0f; // [新增] 唤醒
    // ... 原有代码 ...
    auto cmp = -e.delta;
    if(input_handle::matched(e.mode, input_handle::mode::shift) || (is_hori_scroll_enabled() && !is_vert_scroll_enabled())){
        cmp.swap_xy();
    }
    scrollTargetVelocity = cmp * get_vel_clamp();
    scrollVelocity = scrollTargetVelocity.scl(VelocityScale);
    return {};
}

events::op_afterwards scroll_pane::on_drag(const events::drag e){
    activity_timer_ = 0.0f; // [新增] 唤醒
    // ... 原有代码 ...
    scrollTargetVelocity = scrollVelocity = {};
    const auto trans = e.delta() * get_vel_clamp();
    // 注意：如果是 Overlay 模式，blank 计算的 bar_hori_length 会有所不同，
    // 但原有的 bar_hori_length() 内部使用了 get_viewport_extent，
    // 我们在 .ixx 中已经修改了 get_viewport_extent 对 Overlay 的支持，所以这里逻辑应该不用动。
    const auto blank = get_viewport_extent() - math::vec2{bar_hori_length(), bar_vert_length()};

    auto rst = scroll.base + (trans / blank) * scrollable_extent();
    // ...
    if(!is_hori_scroll_enabled())rst.x = 0;
    if(!is_vert_scroll_enabled())rst.y = 0;

    rst.clamp_xy({}, scrollable_extent());

    if(util::try_modify(scroll.temp, rst)){
        require_scene_cursor_update();
    }

    return events::op_afterwards::intercepted;
}

// --- 布局更新：核心修改点 ---

void scroll_pane::update_item_layout(){
    assert(item != nullptr);

    force_hori_scroll_enabled_ = false;
    force_vert_scroll_enabled_ = false;

    deduced_set_child_fill_parent(*item);

    // ... (中间 policy 判断部分保持不变) ...
	math::bool2 fill_mask{};
	switch(layout_policy_){
	case layout::layout_policy::hori_major: fill_mask = {true, false}; break;
	case layout::layout_policy::vert_major: fill_mask = {false, true}; break;
	case layout::layout_policy::none:       fill_mask = {false, false}; break;
	default: std::unreachable();
	}
    util::set_fill_parent(*item, content_extent(), fill_mask, !fill_mask);

    auto bound = item->restriction_extent;

    using namespace layout;

    item->set_prefer_extent(get_viewport_extent()); // 这里已经自动适配了 Overlay 模式
    if(auto sz = item->pre_acquire_size(bound)){
        bool need_self_relayout = false;

        if(bar_caps_size){
            bool need_elem_relayout = false;
            // [新增] 定义占位尺寸：如果是 Overlay 模式，占用空间为 0，否则为 stroke
            const float bar_occupied_size = overlay_scroll_bars_ ? 0.0f : scroll_bar_stroke_;

            switch(layout_policy_){
            case layout_policy::hori_major :{
                if(sz->y > content_height()){
                    // [修改] 使用 bar_occupied_size 代替 scroll_bar_stroke_
                    bound.set_width(math::clamp_positive(bound.potential_width() - bar_occupied_size));
                    need_elem_relayout = true;
                    force_vert_scroll_enabled_ = true;
                }
                if(restriction_extent.width_pending() && sz->x > content_width()){
                    need_self_relayout = true;
                }
                break;
            }
            case layout_policy::vert_major :{
                if(sz->x > content_width()){
                     // [修改] 使用 bar_occupied_size 代替 scroll_bar_stroke_
                    bound.set_height(math::clamp_positive(bound.potential_height() - bar_occupied_size));
                    need_elem_relayout = true;
                    force_hori_scroll_enabled_ = true;
                }
                if(restriction_extent.height_pending() && sz->y > content_height()){
                    need_self_relayout = true;
                }
                break;
            }
            default: break;
            }

            if(need_elem_relayout){
                auto b = bound;
                b.apply(content_extent());
                item->set_prefer_extent(b.potential_extent());
                if(auto s = item->pre_acquire_size(bound)) sz = s;
            }
        }

		item->resize(*sz, propagate_mask::local | propagate_mask::child);

		if(need_self_relayout){
			auto elemSz = item->extent();
            // [新增] 同样在 resize 自身时，如果是 Overlay，不需要额外加宽/加高
            const float bar_occupied_size = overlay_scroll_bars_ ? 0.0f : scroll_bar_stroke_;

			switch(layout_policy_){
			case layout_policy::hori_major :{
				if(elemSz.x > content_width()){
					elemSz.y = content_height();
                    // [修改]
					elemSz.x += static_cast<float>(bar_caps_size) * bar_occupied_size;
				}
				break;
			}
			case layout_policy::vert_major :{
				if(elemSz.y > content_height()){
					elemSz.x = content_width();
                    // [修改]
					elemSz.y += static_cast<float>(bar_caps_size) * bar_occupied_size;
				}
				break;
			}
			default: break;
			}

			elemSz += boarder().extent();
			resize(elemSz);
		}
	}

	item->layout_elem();
}

void scroll_pane::deduced_set_child_fill_parent(elem& element) const noexcept{
	using namespace layout;
	element.restriction_extent = extent_by_external;
	switch(layout_policy_){
	case layout_policy::hori_major :{
		element.set_fill_parent({true, false}, propagate_mask::none);
		element.restriction_extent.set_width(content_width());
		break;
	}
	case layout_policy::vert_major :{
		element.set_fill_parent({false, true}, propagate_mask::none);
		element.restriction_extent.set_height(content_height());
		break;
	}
	case layout_policy::none:
		element.set_fill_parent({false, false}, propagate_mask::none);
		break;
	default: std::unreachable();
	}
}
}
