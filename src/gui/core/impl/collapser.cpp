module mo_yanxi.gui.elem.collapser;
import mo_yanxi.graphic.draw.instruction;

namespace mo_yanxi::gui{


void collapser::update_collapse(float delta) noexcept{
	const bool enterable = expandable();
	switch(state_){
	case collapser_state::un_expand:{
		if (enterable){
			expand_reload_ += delta;
			if(expand_reload_ >= settings.expand_enter_spacing){
				expand_reload_ = 0.f;

				if(std::isinf(settings.expand_speed)){
					state_ = collapser_state::expanded;
				}else{
					state_ = collapser_state::expanding;
				}

			}
		}else{
			expand_reload_ = 0;
			util::update_erase(*this, update_channel::layout);
		}
		break;
	}
	case collapser_state::expanding:{
		expand_reload_ += settings.expand_speed * delta;
		notify_layout_changed(propagate_mask::force_upper);
		require_scene_cursor_update();

		if(expand_reload_ >= 1){
			expand_reload_ = 0.f;
			state_ = collapser_state::expanded;
			if(update_opacity_during_expand_)body().update_context_opacity(get_draw_opacity());
			body().on_display_state_changed(true, false);
			if(expandable())util::update_erase(*this, update_channel::layout);
		}else if(update_opacity_during_expand_){
			body().update_context_opacity(get_interped_progress() * get_draw_opacity());
		}

		if(transpose_head_and_body_){
			set_children_src();
		}

		break;
	}
	case collapser_state::expanded:{
		if (!enterable){
			expand_reload_ += delta;
			if(expand_reload_ >= settings.expand_exit_spacing){
				expand_reload_ = 1.f;

				if(std::isinf(settings.expand_speed)){
					state_ = collapser_state::un_expand;
				}else{
					state_ = collapser_state::exiting_expand;
				}
			}
		}else{
			expand_reload_ = 0;
			util::update_erase(*this, update_channel::layout);

		}
		break;
	}
	case collapser_state::exiting_expand:{
		expand_reload_ = std::fdim(expand_reload_, settings.expand_speed * delta);
		notify_layout_changed(propagate_mask::force_upper);
		require_scene_cursor_update();

		if(expand_reload_ == 0.f){
			state_ = collapser_state::un_expand;
			if(update_opacity_during_expand_)body().update_context_opacity(0);
			body().on_display_state_changed(false, false);
			util::update_erase(*this, update_channel::layout);

		}else if(update_opacity_during_expand_){
			body().update_context_opacity(get_interped_progress() * get_draw_opacity());
		}

		if(transpose_head_and_body_){
			set_children_src();
		}

		break;
	}
	default: std::unreachable();
	}
}


void collapser::record_draw_layer(draw_call_stack_recorder& call_stack_builder) const{
	elem::record_draw_layer(call_stack_builder);

	call_stack_builder.push_call_enter(
		*this, [](const elem& s, const draw_call_param& p, draw_call_stack&) static -> draw_call_param{
			const auto space = s.content_bound_abs().intersection_with(p.draw_bound);
			return {
					.current_subject = &s,
					.draw_bound = space,
					.opacity_scl = s.get_draw_opacity(),
					.layer_param = p.layer_param
				};
		});

	items[0]->record_draw_layer(call_stack_builder);

	{
		call_stack_builder.push_call_enter(
			*this, [](const collapser& s, const draw_call_param& p, draw_call_stack&) static -> draw_call_param{
				const auto space = s.content_bound_abs().intersection_with(p.draw_bound);

				bool allow_next_layer = false;
				switch(s.state_){
				case collapser_state::un_expand : break;
				case collapser_state::expanding :[[fallthrough]];
				case collapser_state::exiting_expand :{
					allow_next_layer = true;
					auto& r = s.renderer();
					r.push_scissor({s.get_expand_region()});
					r.notify_viewport_changed();
					break;
				}
				case collapser_state::expanded :
					allow_next_layer = true;
					break;
				default : std::unreachable();
				}

				return {
						.current_subject = allow_next_layer ? &s : nullptr,
						.draw_bound = space,
						.opacity_scl = s.get_draw_opacity(),
						.layer_param = p.layer_param
					};
			});
		items[1]->record_draw_layer(call_stack_builder);

		call_stack_builder.push_call_leave(*this, [](const collapser& s, const draw_call_param& p, draw_call_stack&){
			if(s.state_ == collapser_state::exiting_expand || s.state_ == collapser_state::expanding){
				auto& r = s.renderer();
				r.pop_scissor();
				r.notify_viewport_changed();
			}
		});
	}

	call_stack_builder.push_call_leave();
}

std::optional<math::vec2> collapser::pre_acquire_size_impl(layout::optional_mastering_extent extent){
	auto pendings = extent.get_pending();
	auto [pd_major, pd_minor] = layout::get_vec_ptr<bool>(layout_policy_);
	auto [major, minor] = layout::get_vec_ptr(layout_policy_);

	auto potential = extent.potential_extent();

	if(pendings.*pd_major){
		auto head_sz = head().pre_acquire_size(extent).value_or(head().extent());
		potential.*major = head_sz.*major;
	}

	if(item_size[0].type == layout::size_category::passive || item_size[1].type == layout::size_category::passive){
		return std::nullopt;
	}

	auto layout_rst = get_layout_minor_dim_config(potential.*major);
	const auto prog = get_interped_progress();
	layout_rst.size[1] *= prog;
	potential.*minor = std::min(std::ranges::fold_left(layout_rst.size, pad_ * prog, std::plus<>{}), potential.*minor);

	return potential;
}


float collapser::get_interped_progress() const noexcept{
	static constexpr auto smoother = [](float a) static noexcept{
		return a * a * a * (a * (a * 6.0f - 15.0f) + 10.0f);
	};
	switch(state_){
	case collapser_state::expanding: [[fallthrough]];
	case collapser_state::exiting_expand: return smoother(expand_reload_);
	case collapser_state::un_expand: return 0;
	case collapser_state::expanded: return 1;
	default: std::unreachable();
	}
}

rect collapser::get_expand_region() const noexcept{
	const auto [_, minor] = layout::get_vec_ptr(layout_policy_);
	const auto prog = get_interped_progress();
	auto content_src = content_src_pos_abs();


	if(transpose_head_and_body_){
		auto content_ext = body().extent();
		content_ext.*minor *= prog;
		return rect{tags::from_extent, content_src, content_ext};
	}else{
		auto content_ext = content_extent();
		const auto off = head().extent().*minor + pad_ * prog;
		content_src.*minor += off;
		content_ext.*minor = math::fdim(content_ext.*minor, off);

		return rect{tags::from_extent, content_src, content_ext};
	}

}
}
