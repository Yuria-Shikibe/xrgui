module mo_yanxi.gui.elem.collapser;
import mo_yanxi.graphic.draw.instruction;

namespace mo_yanxi::gui{


void collapser::update_collapse(float delta) noexcept{

	animator_.set_speed(settings.expand_speed);
	animator_.set_enter_delay(settings.expand_enter_spacing);
	animator_.set_exit_delay(settings.expand_exit_spacing);

	// 2. 根据触发条件设置目标状态
	animator_.set_target(expandable());

	// 3. 执行状态机推进
	animator_.update(delta,
		[this] { // on_enter_complete (对应原 expanded 初始逻辑)
			if(update_opacity_during_expand_) body().set_propagate_opacity(1.f);
			body().on_display_state_changed(true, false);
		},
		[this] { // on_exit_complete (对应原 un_expand 初始逻辑)
			if(update_opacity_during_expand_) body().set_propagate_opacity(0);
			body().on_display_state_changed(false, false);
		}
	);

	// 4. 处理过渡动画期间（expanding / exiting_expand）需要执行的渲染更新
	const auto state = animator_.get_state();
	if (state == util::anim_state::entering || state == util::anim_state::exiting) {
		notify_layout_changed(propagate_mask::local | propagate_mask::force_upper);
		require_scene_cursor_update();

		if (update_opacity_during_expand_) {
			body().set_propagate_opacity(get_interped_progress());
		}

		if (transpose_head_and_body_) {
			set_children_src();
		}
	}

	// 5. 若已达目标状态且不处于过渡中，则从布局更新队列擦除自己以节省性能
	if (!animator_.is_updating() && (expandable() ? (state == util::anim_state::active) : (state == util::anim_state::idle))) {
		util::update_erase(*this, update_channel::layout);
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
					.opacity_scl = p.opacity_scl * s.get_local_draw_opacity(),
					.layer_param = p.layer_param
				};
		});

	items[0]->record_draw_layer(call_stack_builder);

	{
		call_stack_builder.push_call_enter(
			*this, [](const collapser& s, const draw_call_param& p, draw_call_stack&) static -> draw_call_param{
				const auto space = s.content_bound_abs().intersection_with(p.draw_bound);

				bool allow_next_layer = false;
				switch(s.animator_.get_state()){
				case util::anim_state::idle :
				case util::anim_state::waiting_to_enter :
					break;
				case util::anim_state::entering :
				case util::anim_state::exiting : {
					allow_next_layer = true;
					auto& r = s.renderer();
					r.push_scissor({s.get_expand_region()});
					r.notify_viewport_changed();
					break;
				}
				case util::anim_state::active :
				case util::anim_state::waiting_to_exit :
					allow_next_layer = true;
					break;
				default : std::unreachable();
				}

				return {
						.current_subject = allow_next_layer ? &s : nullptr,
						.draw_bound = space,
						.opacity_scl = p.opacity_scl * s.get_local_draw_opacity(),
						.layer_param = p.layer_param
					};
			});
		items[1]->record_draw_layer(call_stack_builder);

		call_stack_builder.push_call_leave(*this, [](const collapser& s, const draw_call_param& p, draw_call_stack&){
			const auto st = s.animator_.get_state();
			if(st == util::anim_state::exiting || st == util::anim_state::entering){
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
	auto [pd_major, pd_minor] = layout::get_vec_ptr<bool>(get_layout_policy());
	auto [major, minor] = layout::get_vec_ptr(get_layout_policy());

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

	return smoother(animator_.get_progress());
}

rect collapser::get_expand_region() const noexcept{
	const auto [_, minor] = layout::get_vec_ptr(get_layout_policy());
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
