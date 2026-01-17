module mo_yanxi.gui.elem.text_edit;

import mo_yanxi.graphic.draw.instruction;

namespace mo_yanxi::gui{
void text_edit::draw_layer(const rect clipSpace, gfx_config::layer_param_pass_t param) const{
	draw_style(param);

	if(param != 0)return;

	using namespace graphic;

	if(caret_ && glyph_layout.contains(caret_->range.src.pos) && glyph_layout.contains(caret_->range.dst.pos)){
		using namespace graphic::draw::instruction;
		auto& r = get_scene().renderer();
		auto off2 = get_glyph_src_abs();

		if(caret_->should_blink()){
			const color caret_Color =
				(is_failed() ? colors::red_dusted : colors::white).copy().mul_a(get_draw_opacity());

			const auto& row = glyph_layout[caret_->range.dst.pos.y];
			const auto& glyph = row[caret_->range.dst.pos.x];

			const float height = row.bound.height();

			const auto src = (off2 + row.src).add(glyph.region.get_src_x(), -row.bound.ascender);
			const math::vec2 ext = {caret::Stroke, height};

			r.push(rect_aabb{
				.v00 = src,
				.v11 = src + ext,
				.vert_color = {caret_Color}
			});
		}

		if(caret_->range.src == caret_->range.dst){
			goto drawBase;
		}

		auto [beg, end] = caret_->range.ordered();
		const auto& beginRow = glyph_layout[beg.pos.y];
		const auto& endRow = glyph_layout[end.pos.y];

		const auto& beginGlyph = beginRow[beg.pos.x];
		const auto& endGlyph = endRow[end.pos.x];

		const math::vec2 beginPos = (beginRow.src + off2).add(beginGlyph.region.get_src_x(), -beginRow.bound.ascender);

		const math::vec2 endPos = (endRow.src + off2).add(endGlyph.region.get_src_x(), endRow.bound.descender);

		const color lineSelectionColor =
			(is_failed() ? colors::red_dusted : colors::gray).copy().mul_a(.65f);

		if(beg.pos.y == end.pos.y){
			r.push(rect_aabb{
				.v00 = beginPos,
				.v11 = endPos,
				.vert_color = {lineSelectionColor}
			});

		}else{
			const auto totalSize = glyph_layout.extent();
			const math::vec2 beginLineEnd = beginRow.src.copy().add_y(beginRow.bound.descender).set_x(totalSize.x) + off2;
			math::vec2 endLineBegin = endRow.src.copy().add_y(-endRow.bound.ascender).set_x(0) + off2;

			r.push(rect_aabb{
				.v00 = beginPos,
				.v11 = beginLineEnd,
				.vert_color = {lineSelectionColor}
			});

			if(end.pos.y - beg.pos.y > 1){
				r.push(rect_aabb{
						.v00 = beginLineEnd,
						.v11 = endLineBegin,
						.vert_color = {lineSelectionColor}
					});
			} else{
				endLineBegin.y = beginPos.y + beginRow.bound.height();
			}

			r.push(rect_aabb{
					.v00 = endLineBegin,
					.v11 = endPos,
					.vert_color = {lineSelectionColor}
				});
		}
	}

	drawBase:

	draw_text();
}

void text_edit::set_focus(bool keyFocused){
	static constexpr std::string_view key_binding_name{"_text_edit"};

	set_focused_key(keyFocused);
	if(keyFocused){
		if(auto map = get_scene().find_input(key_binding_name)){
			auto& kmap = dynamic_cast<text_edit_key_binding&>(*map);
			kmap.set_context(std::ref(*this));
			kmap.set_activated(true);
		}else{
			auto& kmap = get_scene().get_inputs().register_sub_input<text_edit_key_binding>(key_binding_name);
			load_default_text_edit_key_binding(kmap);
			kmap.set_context(std::ref(*this));
		}

	}else{
		if(auto map = get_scene().find_input(key_binding_name)){
			auto& kmap = dynamic_cast<text_edit_key_binding&>(*map);
			auto [host] = kmap.get_context();
			if(&host.get() == this){
				kmap.set_context(text_edit_key_binding::context_tuple_t{});
				kmap.set_activated(false);
			}

		}
	}
}


template <auto Fn, auto ...Args>
	requires (std::invocable<decltype(Fn), text_edit&, decltype(Args)...>)
consteval auto make_bind() noexcept {
	return [](input_handle::key_set key, float, text_edit& text_edit) static {
		std::invoke(Fn, text_edit, Args...);
	};
}

void load_default_text_edit_key_binding(text_edit_key_binding& bind){
	using namespace input_handle;

	auto add = [&](key key_enum, mode mode_enum, auto func) {
		bind.add_binding(key_set{key_enum, act::press, mode_enum}, func);
		bind.add_binding(key_set{key_enum, act::repeat, mode_enum}, func);
	};

	auto add_deduced = [&](key key_enum, auto func) {
		bind.add_binding(key_set{key_enum, act::press}, func);
		bind.add_binding(key_set{key_enum, act::repeat}, func);
	};

	// --- 导航绑定 (Navigation) ---

	// Right
	add_deduced(key::right, [](key_set key, float, text_edit& self) static {
		self.action_move_right(matched(key.mode_bits, mode::shift), matched(key.mode_bits, mode::ctrl));
	});
	add_deduced(key::left, [](key_set key, float, text_edit& self) static {
		self.action_move_left(matched(key.mode_bits, mode::shift), matched(key.mode_bits, mode::ctrl));
	});

	add_deduced(key::up, [](key_set key, float, text_edit& self) static {
		self.action_move_up(matched(key.mode_bits, mode::shift));
	});
	add_deduced(key::down, [](key_set key, float, text_edit& self) static {
		self.action_move_down(matched(key.mode_bits, mode::shift));
	});

	add_deduced(key::home, [](key_set key, float, text_edit& self) static {
		self.action_move_line_begin(matched(key.mode_bits, mode::shift));
	});
	add_deduced(key::end, [](key_set key, float, text_edit& self) static {
		self.action_move_line_end(matched(key.mode_bits, mode::shift));
	});

	add(key::a, mode::ctrl, make_bind<&text_edit::action_select_all>());

	add(key::c, mode::ctrl, make_bind<&text_edit::action_copy>());
	add(key::v, mode::ctrl, make_bind<&text_edit::action_paste>());
	add(key::x, mode::ctrl, make_bind<&text_edit::action_cut>());
	add(key::z, mode::ctrl, make_bind<&text_edit::undo>());
	add(key::z, mode::ctrl | mode::shift, make_bind<&text_edit::redo>());

	add(key::del, mode::none, make_bind<&text_edit::action_do_delete>());
	add(key::backspace, mode::none, make_bind<&text_edit::action_do_backspace>());

	add(key::enter, mode::ignore, make_bind<&text_edit::action_enter>());

	add(key::tab, mode::none, make_bind<&text_edit::action_tab>());

}
}
