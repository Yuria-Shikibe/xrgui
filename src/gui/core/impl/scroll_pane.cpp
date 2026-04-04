module;

#include <cassert>

module mo_yanxi.gui.elem.scroll_pane;

import mo_yanxi.graphic.draw.instruction;

namespace mo_yanxi::gui{
	namespace style{
		referenced_ptr<const scroll_pane_bar_drawer> global_scroll_pane_bar_drawer = default_scroll_pane_drawer;
	}


	void style::scroll_pane_bar_drawer::draw_layer_impl(const scroll_adaptor_base& element, math::frect region,
	                                                    float opacityScl,
	                                                    fx::layer_param layer_param) const{
		if(layer_param.is_top() && opacityScl > 0.001f){
			each_scroll_rect(element, region, [&](math::raw_frect bar_rect, bool){
				element.renderer().push(graphic::draw::instruction::rect_aabb{
						.v00 = bar_rect.vert_00(),
						.v11 = bar_rect.vert_11(),
						.vert_color = graphic::colors::light_gray.copy_set_a(opacityScl)
					});
			});
		}
	}

	bool scroll_adaptor_base::update(const float delta_in_ticks){
		if(!elem::update(delta_in_ticks)) return false;

		if(overlay_scroll_bars_ && update_flag.is_self_update_required(update_channel::draw)){
			bool active = false;

			if(scroll_velocity_.length2() > 0.1f){
				active = true;
			} else if(cursor_state().inbound){
				const auto check_pos = last_local_cursor_pos_ - scroll_.temp;
				const auto vp = rect{
						tags::unchecked, tags::from_extent, {}, get_viewport_extent().fdim(get_bar_extent())
					};
				active = !vp.contains_loose(check_pos) && (is_hori_scroll_enabled() || is_vert_scroll_enabled());
			}

			if(active){
				activity_timer_ = 0.0f;
			} else{
				activity_timer_ += delta_in_ticks;
			}

			float target_opacity;
			float expected_final_opacity;
			if(activity_timer_ < fade_delay_ticks){
				target_opacity = 1.0f;
				expected_final_opacity = 1.f;
			} else{
				const float fade_progress = (activity_timer_ - fade_delay_ticks) / fade_duration_ticks;
				target_opacity = 1.0f - math::clamp(fade_progress, 0.0f, 1.0f);
				expected_final_opacity = 0.f;
			}

			math::approach_inplace(bar_opacity_, target_opacity, delta_in_ticks * 0.2f);
			if(bar_opacity_ == expected_final_opacity && bar_opacity_ == 0.f){
				this->post_task([](const elem& e){util::update_erase(e, update_channel::draw);});
			}
		} else{
			bar_opacity_ = 1.0f;
			activity_timer_ = 0.0f;
		}

		{
			//scroll update
			scroll_velocity_.lerp_inplace(scroll_target_velocity_, math::clamp(delta_in_ticks * VelocitySensitivity));
			scroll_target_velocity_.lerp_inplace({}, math::clamp(delta_in_ticks * VelocitySensitivity));

			if(util::try_modify(
				scroll_.base,
				math::fma(scroll_velocity_, delta_in_ticks, scroll_.base).clamp_xy({}, scrollable_extent()) *
				get_vel_clamp())){
				scroll_.resume();

				scroll_changed_in_update_ = true;

				activity_timer_ = 0.f;
				this->post_task([](elem& e){util::update_insert(e, update_channel::position);});
			} else{
				scroll_changed_in_update_ = false;
				this->post_task([](elem& e){util::update_erase(e, update_channel::position);});
			}
		}

		return true;
	}

	void scroll_adaptor_base::draw_scroll_bar(fx::layer_param_pass_t param) const{
		if(drawer) drawer->draw_layer(*this, content_bound_abs(), bar_opacity_ * get_draw_opacity(), param);
	}

	events::op_afterwards scroll_adaptor_base::on_scroll(const events::scroll e, std::span<elem* const> aboves){
		activity_timer_ = 0.0f;
		this->sync_run([](scroll_adaptor_base& e){util::update_insert(e, update_channel::position | (e.overlay_scroll_bars_ ? update_channel::draw : update_channel{}));});

		auto cmp = -e.delta;
		if(input_handle::matched(e.mode, input_handle::mode::shift) || (is_hori_scroll_enabled() && !
			is_vert_scroll_enabled())){
			cmp.swap_xy();
		}
		scroll_target_velocity_ = cmp * get_vel_clamp();
		scroll_velocity_ = scroll_target_velocity_.scl(VelocityScale);
		return {};
	}

	events::op_afterwards scroll_adaptor_base::on_drag(const events::drag e){
		if(util::contains(e.src, get_viewport_extent())){
			return events::op_afterwards::fall_through;
		}
		activity_timer_ = 0.0f;
		this->sync_run([](scroll_adaptor_base& e){util::update_insert(e, update_channel::position | (e.overlay_scroll_bars_ ? update_channel::draw : update_channel{}));});

		scroll_target_velocity_ = scroll_velocity_ = {};
		const auto trans = e.delta() * get_vel_clamp();

		const auto blank = get_viewport_extent() - math::vec2{bar_hori_length(), bar_vert_length()};
		auto rst = scroll_.base + (trans / blank) * scrollable_extent();

		if(!is_hori_scroll_enabled()) rst.x = 0;
		if(!is_vert_scroll_enabled()) rst.y = 0;

		rst.clamp_xy({}, scrollable_extent());

		if(util::try_modify(scroll_.temp, rst)){
			require_scene_cursor_update();
		}

		return events::op_afterwards::intercepted;
	}
}
