module mo_yanxi.gui.elem.text_edit_v2;

import mo_yanxi.unicode;
import mo_yanxi.math.matrix3;
import mo_yanxi.graphic.draw.instruction;

namespace mo_yanxi::gui{
bool text_edit::update(float delta_in_ticks){
	if(!elem::update(delta_in_ticks)) return false;

	if(on_changed_timer_ > 0.f){
		on_changed_timer_ -= delta_in_ticks;
		if(on_changed_timer_ <= 0.f){
			on_changed_timer_ = 0.f;
			on_changed();
		}
	}

	if(failed_hint_timer_ > 0.f){
		failed_hint_timer_ -= delta_in_ticks;
	}

	if(!is_focused_key() && core_.get_caret().has_region()){
		core_.action_move_left(tokenized_text_.get_text(), false);
	}

	if(is_focused_key()){
		caret_blink_timer_ += delta_in_ticks;
		if(caret_blink_timer_ > 60.f) caret_blink_timer_ = 0.f;
	}

	if(caret_cache_.last_cached_caret_ != core_.get_caret()){
		update_caret_cache();
	}

	return true;
}

text_layout_result text_edit::layout_text(math::vec2 bound){
	if(!fit_ && bound.area() < 32.0f) return {};

	math::vec2 local_bound = bound;
	math::vec2 abs_scale = {std::abs(scale_.x), std::abs(scale_.y)};

	// 逆向应用绝对缩放系数
	if(abs_scale.x > 0.0001f && abs_scale.y > 0.0001f){
		local_bound /= abs_scale;
	}

	auto process_result_ext = [&]() -> math::vec2{
		return glyph_layout_.extent * abs_scale;
	};

	if(fit_){
		if((change_mark_ & text_edit_change_type::max_extent) != text_edit_change_type::none){
			if(layout_config_.set_max_extent(mo_yanxi::math::vectors::constant2<float>::inf_positive_vec2)){
			} else{
				change_mark_ = change_mark_ & ~text_edit_change_type::max_extent;
			}
		}

		if(is_layout_expired_()){
			if(layout_config_.set_max_extent(mo_yanxi::math::vectors::constant2<float>::inf_positive_vec2) ||
				((change_mark_ & text_edit_change_type::config) != text_edit_change_type::none) || ((change_mark_ &
					text_edit_change_type::text) != text_edit_change_type::none)){
				layout_context.layout(tokenized_text_, layout_config_, glyph_layout_);
				render_cache_.update_buffer(glyph_layout_, get_text_draw_color());
				change_mark_ = text_edit_change_type::none;
				update_caret_cache();
				return {process_result_ext(), true};
			}
			change_mark_ = text_edit_change_type::none;
		}
	} else if(layout_config_.set_max_extent(local_bound) || is_layout_expired_()){
		// 判断本次排版是否由文本改变引发。这很重要，可以防止用户单纯缩小输入框边界时误触发文本回滚
		bool is_text_changed = (change_mark_ & text_edit_change_type::text) != text_edit_change_type::none;

		layout_context.layout(tokenized_text_, layout_config_, glyph_layout_);

		if(!glyph_layout_.is_exhausted && is_text_changed){
			tokenized_text_.modify(apply_tokens_ ? typesetting::tokenize_tag::kep : typesetting::tokenize_tag::raw,
				[&](std::u32string& text) -> bool{
					// 回滚文本到上一个状态。由于是整体撤销，caret也会随之恢复到输入前的位置（即不移动）
					core_.undo(text);
					// 截断当前的状态防止 redo 异常。
					// 假设 text_editor_core 具有 clear_redo() 方法（如果没有，请替换为对应的清空历史/提交空历史接口）
					core_.clear_redo();
					return true;
				});

			// 触发输入无效（内部包含了 reset_blink() 逻辑）
			set_input_invalid();

			// 文本回滚后，必须立刻重新进行一次排版以同步正确的数据
			layout_context.layout(tokenized_text_, layout_config_, glyph_layout_);
		}
		// ==============================================================

		render_cache_.update_buffer(glyph_layout_, get_text_draw_color());
		change_mark_ = text_edit_change_type::none;
		update_caret_cache();
		return {process_result_ext(), true};
	}

	return {process_result_ext(), false};
}

void text_edit::layout_elem(){
	elem::layout_elem();
	if(is_layout_expired_()){
		auto maxSz = restriction_extent.potential_extent();
		const auto resutlSz = layout_text(maxSz.fdim(boarder_extent()));
		if(resutlSz.updated && !resutlSz.required_extent.equals(content_extent())){
			notify_layout_changed(propagate_mask::force_upper);
		}
	}
}

bool text_edit::resize_impl(const math::vec2 size){
	if(elem::resize_impl(size)){
		layout_text(content_extent());
		return true;
	}
	return false;
}

std::optional<math::vec2> text_edit::pre_acquire_size_impl(layout::optional_mastering_extent extent){
	if(get_expand_policy() == layout::expand_policy::passive){
		return std::nullopt;
	}

	if(!fit_){
		const auto text_size = layout_text(extent.potential_extent());
		extent.apply(text_size.required_extent);
	}

	const auto ext = extent.potential_extent().inf_to0();
	return util::select_prefer_extent(get_expand_policy() == layout::expand_policy::prefer, ext,
		get_prefer_content_extent());
}

void text_edit::draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const{
	draw_style(param);
	if(param != 0) return;

	draw_selection_and_caret();

	if(!render_cache_.has_drawable_text()) return;

	auto t_params = get_transform_params();

	// 局部偏移 + 组件屏幕偏移 = 最终的绝对偏移
	math::vec2 offset_abs = t_params.offset_local + content_src_pos_abs();

	math::mat3 mat = math::mat3_idt;
	mat.c1.x = t_params.scale.x;
	mat.c2.y = t_params.scale.y;
	mat.c3.x = offset_abs.x;
	mat.c3.y = offset_abs.y;

	state_guard guard{
			renderer(),
			fx::batch_draw_mode::msdf
		};
	transform_guard _t{renderer(), mat};

	render_cache_.push_to_renderer(renderer());
}

// --------------------------------------------------------
// --- 核心主动缓存逻辑 ---
// --------------------------------------------------------
void text_edit::update_caret_cache(){
	auto caret = core_.get_caret();
	caret_cache_.last_cached_caret_ = caret;
	caret_cache_.selection_rect_count_ = 0;

	float fallback_height = layout_config_.get_default_font_size().y;
	if(fallback_height < 1.0f) fallback_height = 16.0f;

	if(glyph_layout_.lines.empty()){
		caret_cache_.caret_rect_ = math::frect{
				tags::from_extent,
				{0.0f, -fallback_height * 0.8f},
				{2.0f, fallback_height}
			};
		return;
	}

	bool has_sel = caret.has_region();
	auto ordered = caret.get_ordered();
	auto text_u32 = tokenized_text_.get_text();

	bool caret_found = false;
	math::vec2 final_caret_pos{};
	math::vec2 final_caret_ext{};

	std::size_t next_line_start_idx = 0;

	bool sel_started = false;
	float first_min_x = 0.f, first_top_y = 0.f, first_bottom_y = 0.f;
	float last_max_x = 0.f, last_top_y = 0.f, last_bottom_y = 0.f;
	float global_min_x = 0.0f;
	float global_max_x = glyph_layout_.extent.x;
	std::size_t first_line_idx = 0;
	std::size_t last_line_idx = 0;

	for(std::size_t line_idx = 0; line_idx < glyph_layout_.lines.size(); ++line_idx){
		const auto& line = glyph_layout_.lines[line_idx];
		auto align_res = line.calculate_alignment(glyph_layout_.extent, render_cache_.get_line_align(),
			glyph_layout_.direction);
		math::vec2 line_src = align_res.start_pos;

		float line_height = line.rect.ascender + line.rect.descender;
		float line_top_y = -line.rect.ascender;

		if(line_height < 0.1f){
			line_height = fallback_height;
			line_top_y = -fallback_height * 0.8f;
		}

		std::size_t line_start_idx = next_line_start_idx;
		std::size_t line_end_idx = line_start_idx;

		if(line.cluster_range.size > 0){
			line_start_idx = glyph_layout_.clusters[line.cluster_range.pos].cluster_index;
			const auto& last_cluster = glyph_layout_.clusters[line.cluster_range.pos + line.cluster_range.size - 1];
			line_end_idx = last_cluster.cluster_index + last_cluster.cluster_span;
		}
		next_line_start_idx = line_end_idx;

		if(has_sel){
			bool line_has_sel = false;
			float sel_min_x = std::numeric_limits<float>::max();
			float sel_max_x = std::numeric_limits<float>::lowest();
			bool extend_to_edge = false;

			if(line.cluster_range.size == 0){
				if(ordered.src <= line_start_idx && ordered.dst > line_start_idx){
					line_has_sel = true;
					sel_min_x = 0.0f;
					sel_max_x = 8.0f;
					extend_to_edge = true;
				}
			} else{
				for(std::size_t i = 0; i < line.cluster_range.size; ++i){
					const auto& cluster = glyph_layout_.clusters[line.cluster_range.pos + i];
					if(cluster.cluster_index >= ordered.src && cluster.cluster_index < ordered.dst){
						line_has_sel = true;
						sel_min_x = std::min(sel_min_x, cluster.logical_rect.vert_00().x);
						sel_max_x = std::max(sel_max_x, cluster.logical_rect.vert_11().x);

						if(cluster.cluster_index < text_u32.size() && (text_u32[cluster.cluster_index] == U'\n' ||
							text_u32[cluster.cluster_index] == U'\r')){
							extend_to_edge = true;
						}
					}
				}

				if(ordered.dst >= next_line_start_idx && ordered.dst > line_start_idx){
					extend_to_edge = true;
				}
			}

			if(line_has_sel){
				float abs_min_x = line_src.x + sel_min_x;
				float abs_max_x = line_src.x + sel_max_x;
				float abs_top_y = line_src.y + line_top_y;
				float abs_bottom_y = line_src.y + line_top_y + line_height;

				if(extend_to_edge){
					float layout_right_edge = glyph_layout_.extent.x;
					abs_max_x = std::max({abs_max_x, layout_right_edge, abs_min_x + 8.0f});
				}

				if(!sel_started){
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

		if(!caret_found){
			for(std::size_t i = 0; i < line.cluster_range.size; ++i){
				const auto& cluster = glyph_layout_.clusters[line.cluster_range.pos + i];

				if(caret.dst >= cluster.cluster_index && caret.dst < cluster.cluster_index + cluster.cluster_span){
					final_caret_ext = {2.0f, line_height};

					float span_ratio = 0.0f;
					if(cluster.cluster_span > 1){
						span_ratio = static_cast<float>(caret.dst - cluster.cluster_index) / static_cast<float>(cluster.
							cluster_span);
					}

					float left_x = cluster.logical_rect.vert_00().x;
					float right_x = cluster.logical_rect.vert_11().x;
					float target_x = left_x + (right_x - left_x) * span_ratio;

					final_caret_pos = line_src + math::vec2{target_x, line_top_y};
					caret_found = true;
					break;
				}
			}

			if(!caret_found && caret.dst == line_end_idx){
				bool is_last_line = (line_idx == glyph_layout_.lines.size() - 1);
				bool ends_with_newline = (caret.dst > 0 && caret.dst <= text_u32.size() && text_u32[caret.dst - 1] ==
					U'\n');

				if(line.cluster_range.size == 0){
					final_caret_ext = {2.0f, line_height};
					final_caret_pos = line_src + math::vec2{0.0f, line_top_y};
					caret_found = true;
				} else if(is_last_line || !ends_with_newline){
					const auto& last_cluster = glyph_layout_.clusters[line.cluster_range.pos + line.cluster_range.size -
						1];
					final_caret_ext = {2.0f, line_height};
					final_caret_pos = line_src + math::vec2{last_cluster.logical_rect.get_end_x(), line_top_y};
					caret_found = true;
				}
			}
		}
	}

	if(sel_started){
		if(first_line_idx == last_line_idx){
			caret_cache_.selection_rects_[0] = math::frect{
					tags::from_vertex,
					{first_min_x, first_top_y},
					{last_max_x, last_bottom_y}
				};
			caret_cache_.selection_rect_count_ = 1;
		} else{
			caret_cache_.selection_rects_[0] = math::frect{
					tags::from_vertex,
					{first_min_x, first_top_y},
					{global_max_x, first_bottom_y}
				};
			caret_cache_.selection_rect_count_ = 1;

			if(last_top_y > first_bottom_y){
				caret_cache_.selection_rects_[1] = math::frect{
						tags::from_vertex,
						{global_min_x, first_bottom_y},
						{global_max_x, last_top_y}
					};
				caret_cache_.selection_rect_count_ = 2;
			}

			caret_cache_.selection_rects_[caret_cache_.selection_rect_count_] = math::frect{
					tags::from_vertex,
					{global_min_x, last_top_y},
					{last_max_x, last_bottom_y}
				};
			caret_cache_.selection_rect_count_++;
		}
	}

	if(caret_found){
		caret_cache_.caret_rect_ = math::frect{
				tags::from_extent,
				final_caret_pos,
				final_caret_ext
			};
	} else{
		caret_cache_.caret_rect_ = math::frect{
				tags::from_extent,
				{0.0f, -fallback_height * 0.8f},
				{2.0f, fallback_height}
			};
	}
}

// --------------------------------------------------------
// --- 使用轻量代数映射选区与游标 ---
// --------------------------------------------------------
void text_edit::draw_selection_and_caret() const{
	if(!is_focused_key()) return;

	if(!glyph_layout_.is_exhausted){
		constexpr static auto color = graphic::colors::red_dusted.copy_set_a(.6f);

		using namespace graphic::draw::instruction;
		auto& r = renderer();
		auto b = content_bound_abs();

		r.push(fx::slide_line_config{
				.angle = 45,
				.spacing = 16,
				.stroke = 16,
			});

		r.push(rect_aabb{
				.generic = {.mode = std::to_underlying(fx::primitive_draw_mode::draw_slide_line)},
				.v00 = b.vert_00(),
				.v11 = b.vert_11(),
				.vert_color = {color},
			});
	} else{
		bool has_sel = caret_cache_.selection_rect_count_ > 0;
		bool show_caret = caret_blink_timer_ < 30.f;

		if(!has_sel && !show_caret) return;

		using namespace graphic::draw::instruction;
		auto& r = renderer();

		const graphic::color selection_color = (is_failed()
			                                        ? graphic::colors::red_dusted
			                                        : graphic::colors::gray.create_lerp(graphic::colors::ROYAL, .3f)).
		                                       copy().mul_a(0.65f);
		const graphic::color caret_color = (is_failed() ? graphic::colors::red_dusted : graphic::colors::white).copy().
			mul_a(get_draw_opacity());

		auto t_params = get_transform_params();
		math::vec2 base_abs = content_src_pos_abs();

		for(std::size_t i = 0; i < caret_cache_.selection_rect_count_; ++i){
			// 先映射到局部坐标
			auto t_rect = t_params.forward_local(caret_cache_.selection_rects_[i]);
			r.push(rect_aabb{
					.v00 = t_rect.vert_00() + base_abs, // 再平移到绝对屏幕坐标
					.v11 = t_rect.vert_11() + base_abs,
					.vert_color = {selection_color}
				});
		}

		if(show_caret){
			auto t_caret = t_params.forward_local(caret_cache_.caret_rect_);
			r.push(rect_aabb{
					.v00 = t_caret.vert_00() + base_abs,
					.v11 = t_caret.vert_11() + base_abs,
					.vert_color = {caret_color}
				});
		}
	}
}

math::frect text_edit::get_caret_local_aabb() const{
	math::frect logical_rect;
	if(caret_cache_.selection_rect_count_ > 0){
		float min_x = std::numeric_limits<float>::max();
		float min_y = std::numeric_limits<float>::max();
		float max_x = std::numeric_limits<float>::lowest();
		float max_y = std::numeric_limits<float>::lowest();
		for(std::size_t i = 0; i < caret_cache_.selection_rect_count_; ++i){
			min_x = std::min(min_x, caret_cache_.selection_rects_[i].vert_00().x);
			min_y = std::min(min_y, caret_cache_.selection_rects_[i].vert_00().y);
			max_x = std::max(max_x, caret_cache_.selection_rects_[i].vert_11().x);
			max_y = std::max(max_y, caret_cache_.selection_rects_[i].vert_11().y);
		}
		logical_rect = math::frect{
				tags::unchecked, tags::from_vertex,
				{min_x, min_y}, {max_x, max_y}
			};
	} else{
		logical_rect = caret_cache_.caret_rect_;
	}

	auto t_params = get_transform_params();
	return t_params.forward_local(logical_rect);
}

events::op_afterwards text_edit::on_drag(const events::drag event){
	auto t_params = get_transform_params();
	math::vec2 raw_hit_pos = t_params.inverse_local(event.dst);

	core_.action_hit_test(glyph_layout_, tokenized_text_.get_text(), raw_hit_pos, render_cache_.get_line_align(), true);
	reset_blink();
	return events::op_afterwards::intercepted;
}

events::op_afterwards text_edit::on_unicode_input(const char32_t val){
	if(!is_character_allowed(val)){
		set_input_invalid();
	} else{
		auto caret = core_.get_caret();
		std::size_t sel_len = caret.has_region() ? caret.get_ordered().length() : 0;

		if(tokenized_text_.get_text().size() - sel_len + 1 > maximum_code_points_){
			set_input_invalid();
		} else{
			const std::u32string_view buf{&val, 1};
			action_do_insert(buf);
			update_ime_position();
		}
	}
	return events::op_afterwards::intercepted;
}

events::op_afterwards text_edit::on_key_input(const input_handle::key_set key){
	update_ime_position();
	return events::op_afterwards::intercepted;
}

events::op_afterwards text_edit::on_esc(){
	if(elem::on_esc() == events::op_afterwards::fall_through && is_focused_key()){
		set_focus(false);
		set_focused_key(false);
		return events::op_afterwards::intercepted;
	}

	return events::op_afterwards::fall_through;
}

graphic::color text_edit::get_text_draw_color() const noexcept{
	if(is_idle_){
		return graphic::colors::light_gray.copy().mul_a(0.65f * get_draw_opacity());
	}
	return render_cache_.get_draw_color(get_draw_opacity(), is_disabled());
}

void text_edit::set_text_internal(std::u32string_view str){
	tokenized_text_.reset(str, apply_tokens_ ? typesetting::tokenize_tag::kep : typesetting::tokenize_tag::raw);
	change_mark_ |= text_edit_change_type::text;
	notify_isolated_layout_changed();
}

void text_edit::set_focus(bool keyFocused){
	if(keyFocused){
		get_scene().active_update_elems.insert(this);

		if(is_idle_){
			is_idle_ = false;
			core_.reset_state();
			set_text_internal(U"");
		}

		if(auto map = get_scene().find_input(text_edit_key_binding_name)){
			auto& kmap = dynamic_cast<text_edit_key_binding&>(*map);
			kmap.set_context(std::ref(*this));
			kmap.set_activated(true);
		} else{
			auto& kmap = get_scene().get_inputs().register_sub_input<text_edit_key_binding>(text_edit_key_binding_name);
			kmap.use_default_setting();
			kmap.set_context(std::ref(*this));
			kmap.set_activated(true);
		}
	} else{
		get_scene().active_update_elems.erase(this);

		if(tokenized_text_.get_text().empty()){
			is_idle_ = true;
			set_text_internal(hint_text_when_idle_);
		}

		if(auto map = get_scene().find_input(text_edit_key_binding_name)){
			auto& kmap = dynamic_cast<text_edit_key_binding&>(*map);
			auto [host] = kmap.get_context();
			if(host && &host.get() == this){
				kmap.set_context(text_edit_key_binding::context_tuple_t{});
				kmap.set_activated(false);
			}
		}
	}

	if(const auto cmt = get_scene().get_communicator()){
		cmt->set_ime_enabled(keyFocused);
		if(keyFocused){
			update_ime_position();
		}
	}
}

void text_edit::action_copy() const{
	auto caret = core_.get_caret();
	if(!caret.has_region()) return;
	auto ordered = caret.get_ordered();

	if(const auto cmt = get_scene().get_communicator()){
		auto sel_u32 = tokenized_text_.get_text().substr(ordered.src, ordered.length());
		cmt->set_clipboard(unicode::utf32_to_utf8(sel_u32));
	}
}

void text_edit::action_paste(){
	if(const auto cmt = get_scene().get_communicator()){
		const auto str = cmt->get_clipboard();
		if(!str.empty()){
			auto rst = unicode::utf8_to_utf32(str);

			if(!filter_code_points.empty()){
				const std::size_t original_size = rst.size();

				std::erase_if(rst, [this](char32_t ch){
					return !is_character_allowed(ch);
				});

				if(rst.empty()){
					if(original_size > 0) set_input_invalid();
					return;
				}

				if(rst.size() < original_size){
					set_input_invalid();
				}
			}

			auto caret = core_.get_caret();
			std::size_t sel_len = caret.has_region() ? caret.get_ordered().length() : 0;
			std::size_t current_len = tokenized_text_.get_text().size();

			if(current_len - sel_len + rst.size() > maximum_code_points_){
				std::size_t allowed_len = maximum_code_points_ - (current_len - sel_len);
				if(allowed_len == 0){
					set_input_invalid();
					return;
				}
				rst.resize(allowed_len);
				set_input_invalid();
			}

			action_do_insert(std::u32string_view{rst.data(), rst.size()});
		}
	}
}

void text_edit::action_cut(){
	action_copy();
	action_do_delete();
}

template <auto Fn, auto ... Args>
	requires (std::invocable<decltype(Fn), text_edit&, decltype(Args)...>)
consteval auto make_bind() noexcept{
	return [](input_handle::key_set key, float, text_edit& text_edit) static{
		std::invoke(Fn, text_edit, Args...);
	};
}

void load_default_text_edit_v2_key_binding(text_edit_key_binding& bind){
	using namespace input_handle;

	auto add = [&](key key_enum, mode mode_enum, auto func){
		bind.add_binding(key_set{key_enum, act::press, mode_enum}, func);
		bind.add_binding(key_set{key_enum, act::repeat, mode_enum}, func);
	};

	auto add_deduced = [&](key key_enum, auto func){
		bind.add_binding(key_set{key_enum, act::press}, func);
		bind.add_binding(key_set{key_enum, act::repeat}, func);
	};

	add_deduced(key::right, [](key_set key, float, text_edit& self) static{
		self.action_move_right(matched(key.mode_bits, mode::shift), matched(key.mode_bits, mode::ctrl));
	});
	add_deduced(key::left, [](key_set key, float, text_edit& self) static{
		self.action_move_left(matched(key.mode_bits, mode::shift), matched(key.mode_bits, mode::ctrl));
	});

	add_deduced(key::up, [](key_set key, float, text_edit& self) static{
		self.action_move_up(matched(key.mode_bits, mode::shift));
	});
	add_deduced(key::down, [](key_set key, float, text_edit& self) static{
		self.action_move_down(matched(key.mode_bits, mode::shift));
	});

	add_deduced(key::home, [](key_set key, float, text_edit& self) static{
		self.action_move_line_begin(matched(key.mode_bits, mode::shift));
	});
	add_deduced(key::end, [](key_set key, float, text_edit& self) static{
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
} // namespace mo_yanxi::gui
