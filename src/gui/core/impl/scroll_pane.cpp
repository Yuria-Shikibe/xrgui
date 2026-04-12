module;

#include <cassert>

module mo_yanxi.gui.elem.scroll_pane;

import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.math.interpolation;

namespace mo_yanxi::gui{





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

		if(overlay_scroll_bars_){
			bool is_interacting = false;


			if(scroll_velocity_.length2() > 0.1f){
				is_interacting = true;
			} else if(cursor_state().inbound){
				is_interacting = is_pos_in_bar_section(last_local_cursor_pos_) && (is_hori_scroll_enabled() || is_vert_scroll_enabled());
			}

			bool keep_draw_update = true;

			switch(overlay_state_) {
				case overlay_bar_state::hidden:
					if(is_interacting) {
						overlay_state_ = overlay_bar_state::fading_in;
					} else {
						keep_draw_update = false;
					}
					break;

				case overlay_bar_state::fading_in:
					bar_opacity_ = math::clamp(bar_opacity_ + delta_in_ticks / fade_duration_ticks, 0.0f, 1.0f);
					if(bar_opacity_ >= 1.0f) {
						overlay_state_ = is_interacting ? overlay_bar_state::active : overlay_bar_state::cooling_down;
						activity_timer_ = 0.0f;
					}
					break;

				case overlay_bar_state::active:
					bar_opacity_ = 1.0f;
					if(!is_interacting) {

						overlay_state_ = overlay_bar_state::cooling_down;
						activity_timer_ = 0.0f;
					} else {

						keep_draw_update = false;
					}
					break;

				case overlay_bar_state::cooling_down:
					bar_opacity_ = 1.0f;
					if(is_interacting) {
						overlay_state_ = overlay_bar_state::active;
					} else {
						activity_timer_ += delta_in_ticks;
						if(activity_timer_ >= fade_delay_ticks) {
							overlay_state_ = overlay_bar_state::fading_out;
						}
					}
					break;

				case overlay_bar_state::fading_out:
					if(is_interacting) {

						overlay_state_ = overlay_bar_state::fading_in;
					} else {
						bar_opacity_ = math::clamp(bar_opacity_ - delta_in_ticks / fade_duration_ticks, 0.0f, 1.0f);
						if(bar_opacity_ <= 0.0f) {
							overlay_state_ = overlay_bar_state::hidden;
							keep_draw_update = false;
						}
					}
					break;
			}


			if(!keep_draw_update) {
				util::update_erase(*this, update_channel::draw);
			}
		} else {

			bar_opacity_ = 1.0f;
			activity_timer_ = 0.0f;
			overlay_state_ = overlay_bar_state::active;
		}

		{


			scroll_velocity_.lerp_inplace(scroll_target_velocity_, math::clamp(delta_in_ticks * scroll_velocity_sensitivity));
			scroll_target_velocity_.lerp_inplace({}, math::clamp(delta_in_ticks * scroll_velocity_sensitivity));

			if(util::try_modify(
				scroll_.base,
				math::fma(scroll_velocity_, delta_in_ticks, scroll_.base).clamp_xy({}, scrollable_extent()) *
				get_vel_clamp())){
				scroll_.resume();

				scroll_changed_in_update_ = true;


				saved_scroll_ratio_ = scroll_progress_at(scroll_.base);

				util::update_insert(*this, update_channel::position);

				} else{
					scroll_changed_in_update_ = false;
					util::update_erase(*this, update_channel::position);
				}
		}

		return true;
	}

	void scroll_adaptor_base::draw_scroll_bar(fx::layer_param_pass_t param) const{
		if(drawer) drawer->draw_layer(*this, content_bound_abs(), bar_opacity_ * get_draw_opacity(), param);
	}

	void scroll_adaptor_base::record_draw_scroll_bar(draw_call_stack_recorder& call_stack_builder) const{
		if(!drawer)return;
		call_stack_builder.push_call_enter(*this, [](const scroll_adaptor_base& s, const draw_call_param& p, draw_call_stack&) static -> draw_call_param {
			const rect bound = s.content_bound_abs();
				return {
					.current_subject = p.draw_bound.overlap_exclusive(bound) ? &s : nullptr,
					.draw_bound = bound,
					.opacity_scl = s.get_draw_opacity() * math::interp::smooth(s.bar_opacity_),
					.layer_param = p.layer_param
				};
			});

		drawer->record_draw_layer(call_stack_builder );

		call_stack_builder.push_call_leave();
	}

	referenced_ptr<const style::scroll_pane_bar_drawer> scroll_adaptor_base::init_drawer_(){
		return post_sync_assign(*this, &scroll_adaptor_base::drawer, [](scroll_adaptor_base& b){
			return b.get_style_manager().get_default<style::scroll_pane_bar_drawer>();
		});
	}

events::op_afterwards scroll_adaptor_base::on_scroll(const events::scroll e, std::span<elem* const> aboves){
		activity_timer_ = 0.0f;
		util::update_insert(*this, update_channel::position | (overlay_scroll_bars_ ? update_channel::draw : update_channel{}));

		auto cmp = -e.delta;
		if(input_handle::matched(e.mode, input_handle::mode::shift) || (is_hori_scroll_enabled() && !
			is_vert_scroll_enabled())){
			cmp.swap_xy();
			}


		math::vec2 velocity_scale{};
		if (sensitivity_mode == scroll_pane_mode::absolute) {
			velocity_scale = math::vec2{scroll_scale, scroll_scale};
		} else {

			velocity_scale = get_viewport_extent() * scroll_scale;
		}

		scroll_target_velocity_ = cmp * get_vel_clamp();
		scroll_target_velocity_.x *= velocity_scale.x;
		scroll_target_velocity_.y *= velocity_scale.y;


		scroll_velocity_ = scroll_target_velocity_;

		return {};
	}

	events::op_afterwards scroll_adaptor_base::on_drag(const events::drag e){
		if(util::contains(e.src, content_extent().fdim(get_bar_extent()))){
			return events::op_afterwards::fall_through;
		}
		activity_timer_ = 0.0f;
		util::update_insert(*this, update_channel::position | (overlay_scroll_bars_ ? update_channel::draw : update_channel{}));

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
