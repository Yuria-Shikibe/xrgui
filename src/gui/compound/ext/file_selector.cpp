module mo_yanxi.gui.compound.file_selector;

import mo_yanxi.gui.fx.compound;
import mo_yanxi.gui.fx.fringe;
import mo_yanxi.math.interpolation;
import mo_yanxi.gui.elem.table;
import mo_yanxi.gui.elem.double_side;
import mo_yanxi.gui.elem.check_box;

import mo_yanxi.gui.elem.scroll_pane;
import mo_yanxi.gui.elem.drag_split;
import mo_yanxi.gui.action.elem;

import mo_yanxi.core.platform;


namespace mo_yanxi::gui::cpd{
	namespace{
		void set_elem_style_(elem& e, std::string_view name){
			post_sync_execute(e, [name](elem& el){
				el.set_style(el.get_style_manager().get_slice<style::elem_style_drawer>()->get_or_default(name));
			});
		}

		void set_style_side_bar(elem& e){
			set_elem_style_(e, "side_bar_left");
		}

		void set_style_edge_only(elem& e){
			set_elem_style_(e, "round_edge_only");
		}

		void set_style_base_only(elem& e){
			set_elem_style_(e, "round_base_only");
		}
	}

	struct trace_entry;

	struct arrow_button : elem{
		enum state{
			closed, expanding, expanded, closing,
		};

		state s{};
		float progress_{};

		trace_entry& get_trace() const noexcept;
		[[nodiscard]] arrow_button(scene& scene, elem* parent);

		bool update(float delta_in_ticks) override{
			constexpr float scl = .12f;
			if(elem::update(delta_in_ticks)){
				switch(s){
				case closed : get_scene().active_update_to_be_removed_elems.insert(this);
					progress_ = 0;
					break;
				case expanding : progress_ += delta_in_ticks * scl;
					if(progress_ >= 1.f){
						progress_ = 1;
						s = expanded;
					}
					break;
				case expanded : get_scene().active_update_to_be_removed_elems.insert(this);
					progress_ = 1;
					break;
				case closing : progress_ -= delta_in_ticks * scl;
					if(progress_ <= 0.f){
						progress_ = 0;
						s = closed;
					}
					break;
				}
				return true;
			}
			return false;
		}

	protected:
		void tooltip_on_drop_behavior_impl() override{
			elem::tooltip_on_drop_behavior_impl();
			switch(s){
			case closed : s = closed;
				break;
			default : s = closing;
				get_scene().active_update_elems.insert(this);
				break;
			}
		}

	public:
		void on_display_state_changed(bool is_shown) override{
			if(!is_shown){
				drop_tooltip();
				invisible = true;
			} else{
				invisible = false;
				if(!has_tooltip()){
					s = closed;
					progress_ = 0;
				}
			}
		}

		events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override{
			elem::on_click(event, aboves);
			if(event.within_elem(*this) && event.key.on_release()){
				switch(s){
				case closed :
				case closing : s = expanding;
					if(!has_tooltip() && !invisible) create_tooltip();
					break;
				case expanding :
				case expanded : s = closing;
					drop_tooltip();
					break;
				}
				get_scene().active_update_elems.insert(this);
			}
			return events::op_afterwards::intercepted;
		}

		void draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const override{
			elem::draw_layer(clipSpace, param);

			if(param.is_top()){
				auto arrow = fx::compound::generate_centered_arrow(content_extent().fdim({4, 4}), 1.5f, 12);
				fx::fringe::inplace_line_context<(7 + 4) * 2> context{};
				float prog = progress_ | math::interp::smoother;
				auto [cos, sin] = math::cos_sin(prog * math::pi_half);
				for(auto vertex : arrow.vertices){
					context.push(vertex.rotate(cos, sin) + content_bound_abs().get_center(), arrow.thick,
					             graphic::colors::white);
				}

				context.add_cap();
				context.add_fringe_cap();
				context.dump_mid(renderer(), graphic::draw::instruction::line_segments{});
				context.dump_fringe_inner(renderer(), graphic::draw::instruction::line_segments{});
				context.dump_fringe_outer(renderer(), graphic::draw::instruction::line_segments{});
			}
		}
	};

	struct trace_entry : gui::head_body{
		file_selector* selector;
		std::filesystem::path path;

		[[nodiscard]] trace_entry(scene& scene, elem* parent, file_selector& selector,
		                          const std::filesystem::path& path_,
		                          bool is_root = false)
			: head_body(scene, parent, layout::layout_policy::vert_major), selector(&selector), path(path_){
			set_style();
			create_head([&](button<direct_label>& l){
				set_style_base_only(l);
				l.set_fit_type(label_fit_type::scl);
				l.set_tokenized_text({
						is_root ? path.u32string() : path.filename().u32string(), typesetting::tokenize_tag::raw
					});
				l.set_button_callback([this, &selector]{
					selector.visit_directory(path);
				});
			});

			create_body([&](arrow_button& l){
				set_style_base_only(l);
			});
			set_pad(4);
			set_head_size({layout::size_category::pending});
			set_body_size(40);
		}

		void clear_body(){
			emplace_body<gui::elem>().set_style();
			set_body_size(0);
		}
	};

	trace_entry& arrow_button::get_trace() const noexcept{
		return parent_ref<trace_entry, true>();
	}

	arrow_button::arrow_button(scene& scene, elem* parent) : elem(scene, parent){
		interactivity = interactivity_flag::enabled;

		set_tooltip_state(
			{
				.layout_info = tooltip::align_meta{
					.follow = tooltip::anchor_type::owner,
					.attach_point_spawner = align::pos::bottom_center,
					.attach_point_tooltip = align::pos::top_center,
				},
				.auto_release = true,
				.min_hover_time = tooltip::create_config::disable_auto_tooltip
			}, [this](const arrow_button& s, table& t){
				t.set_style();
				std::size_t count{};
				auto hdl = t.create_back([&](scroll_pane& p){
					p.create([&](sequence& seq){
						seq.template_cell.set_size(60).set_pad({2, 2});
						seq.set_style();

						std::error_code ec;
						auto dir_it = std::filesystem::directory_iterator(s.get_trace().path, ec);
						if(!ec){
							for(; dir_it != std::filesystem::directory_iterator{}; dir_it.increment(ec)){
								if(ec) continue; // 忽略无权限的子项

								auto& pth = *dir_it;
								if(!pth.is_directory(ec)) continue;
								if(ec) continue;

								++count;
								seq.create_back([&](button<direct_label>& but){
									set_style_side_bar(but);
									but.set_fit_type(label_fit_type::scl);
									but.set_tokenized_text({
											pth.path().filename().u32string(), typesetting::tokenize_tag::raw
										});
									but.set_button_callback([this, p = pth.path()]{
										get_trace().selector->visit_directory(p);
									});
								});
							}
						}
					});
				});

				if(count){
					hdl.cell().set_size({400.f, std::min(8uz, count) * 80.f});
				} else{
					t.clear();
					t.create_back([](gui::direct_label& b){
						using namespace std::literals;
						b.set_tokenized_text(typesetting::tokenized_text{U"No More Other Dirs"sv});
					}).cell().set_pending();
				}
			});
	}

	struct current_position_bar : double_side<2>{
		file_selector* selector;

		[[nodiscard]] current_position_bar(scene& scene, elem* parent, file_selector& s)
			: double_side<2>(scene, parent), selector(&s){
			interactivity = interactivity_flag::enabled;
			set_expand_policy(layout::expand_policy::passive);
			set_style_side_bar(*this);
		}

		void on_last_clicked_changed(bool isFocused) override{
			if(isFocused){
				switch_to(1);
				auto& edit = at<text_edit>(1);
				edit.apply_edit([this](std::u32string& s){
					s = selector->get_current_directory().u32string();
					return true;
				});
				edit.on_last_clicked_changed(true);
				edit.action_select_all();
				get_scene().overwrite_last_inbound_click_quiet(&edit);
			}
		}

		events::op_afterwards on_click(events::click event, std::span<elem* const> aboves) override{
			elem::on_click(event, aboves);
			return events::op_afterwards::intercepted;
		}

		events::op_afterwards on_esc() override{
			if(get_current_active_index() == 1){
				switch_to(0);
				return events::op_afterwards::intercepted;
			}
			return events::op_afterwards::fall_through;
		}

		gui::style::cursor_style get_cursor_type(math::vec2 cursor_pos_at_content_local) const noexcept override{
			return {style::cursor_type::textarea};
		}
	};

	struct path_edit : text_edit{
		current_position_bar& get_current_position_bar() const{
			return parent_ref<current_position_bar, false>();
		}

		[[nodiscard]] path_edit(scene& scene, elem* parent)
			: text_edit(scene, parent){
			set_character_filter_mode(false);
			set_filter_characters(platform::get_invalid_path_chars());
		}

		void on_last_clicked_changed(bool isFocused) override{
			text_edit::on_last_clicked_changed(isFocused);
			if(!isFocused) get_current_position_bar().switch_to(0);
		}

		void action_enter() override{
			try{
				std::filesystem::path p = get_text();
				p = std::filesystem::absolute(p).make_preferred();
				if(std::filesystem::is_directory(p)){
					get_current_position_bar().selector->visit_directory(std::move(p));
				} else{
					set_input_invalid();
				}
			} catch(...){
				set_input_invalid();
			}
		}

		events::op_afterwards on_key_input(const input_handle::key_set key) override{
			return text_edit::on_key_input(key);
		}
	};

	struct filter_editor : text_edit{
		file_selector* s;

		[[nodiscard]] filter_editor(scene& scene, elem* parent, file_selector& s)
			: text_edit(scene, parent), s(&s){
		}

		void on_changed() override{
			s->refresh();
		}
	};

	struct sort_type_button : select_box<3>{
		file_selector* s{};

		[[nodiscard]] sort_type_button(scene& scene, elem* group, file_selector& s)
			: select_box(scene, group), s(&s){
			assets::builtin::shape_id ids[] = {
					assets::builtin::shape_id::alphabetical_sorting, assets::builtin::shape_id::time,
					assets::builtin::shape_id::row_height
				};
			for(std::int32_t i = 0; i < std::ranges::size(ids); ++i){
				icons[i].components.color = {graphic::colors::white};
				icons[i].components.enabled = true;
				icons[i].components.mode = fx::batch_draw_mode::msdf;
				icons[i].image_region = assets::builtin::get_page()[ids[i]].value_or({});
			}
		}

	protected:
		void on_selected_val_updated(unsigned value) override{
			s->set_sort_category(static_cast<file_sort_category>(value));
		}
	};

	struct sort_method_button : select_box<2>{
		file_selector* s{};

		[[nodiscard]] sort_method_button(scene& scene, elem* group, file_selector& s)
			: select_box(scene, group), s(&s){
			assets::builtin::shape_id ids[] = {
					assets::builtin::shape_id::arrow_up, assets::builtin::shape_id::arrow_down
				};
			for(std::int32_t i = 0; i < std::ranges::size(ids); ++i){
				icons[i].components.color = {graphic::colors::white};
				icons[i].components.enabled = true;
				icons[i].components.mode = fx::batch_draw_mode::msdf;
				icons[i].image_region = assets::builtin::get_page()[ids[i]].value_or({});
			}
		}

	protected:
		void on_selected_val_updated(unsigned value) override{
			s->set_sort_method(value ? file_sort_method::descend : file_sort_method::none);
		}
	};

	// ==========================================
	// file_entry 实现
	// ==========================================
	file_selector& file_selector::file_entry::get_file_selector() const noexcept{
		return *selector;
	}

	file_selector::file_entry::file_entry(scene& scene, elem* parent, file_selector& selector,
	                                      file_selector::path&& entry_path)
		: head_body(scene, parent, layout::layout_policy::vert_major), selector(&selector), path(std::move(entry_path)){
		interactivity = interactivity_flag::intercept;

		std::error_code ec;
		bool is_dir = std::filesystem::is_directory(path, ec); // 加入 error_code
		bool is_root = path == path.parent_path();

		set_style_side_bar(*this);

		auto id = is_root
			          ? assets::builtin::shape_id::data_server
			          : (is_dir ? assets::builtin::shape_id::folder : assets::builtin::shape_id::file);

		auto& i = this->emplace_head<icon_frame>(id);
		i.set_style();

		this->create_body([&](direct_label& l){
			l.set_style();
			l.set_fit(true);
			l.text_entire_align = align::pos::center_left;
			l.set_tokenized_text({
					is_root ? path.u32string() : path.filename().u32string(), typesetting::tokenize_tag::raw
				});
			l.set_transform_config({.scale = {.75f, .75f}});
		});

		set_head_size({layout::size_category::scaling});
		set_pad(16);
		set_fill_parent({true});
		set_expand_policy(layout::expand_policy::passive);
	}

	events::op_afterwards file_selector::file_entry::on_click(const events::click event, std::span<elem* const> aboves){
		elem::on_click(event, aboves);
		if(event.key.on_release() && event.within_elem(*this)){
			auto& menu = get_file_selector();
			if(std::filesystem::is_directory(path)){
				menu.visit_directory(path);
			} else{
				menu.handle_file_selection(this, event.key.mode_bits);
			}
		}
		return events::op_afterwards::intercepted;
	}

	// ==========================================
	// file_selector 逻辑实现
	// ==========================================

	void file_selector::set_botton_enable_(icon_button_type& which, bool enabled){
		which.set_disabled(!enabled);
		which.scale_color = enabled ? graphic::colors::white : graphic::colors::gray;
	}

	void file_selector::set_sort_category(const file_sort_category sort_type){
		if(util::try_modify(sort_category_, sort_type)){
			refresh();
		}
	}

	void file_selector::set_sort_method(const file_sort_method sort_type){
		if(util::try_modify(sort_method_, sort_type)){
			refresh();
		}
	}

	void file_selector::refresh(){
		build_file_entries_();
	}

	void file_selector::set_multiple_selection(bool allow){
		if(util::try_modify(multiple_selection, allow)){
			if(!allow && selected.size() > 1){
				path last_path = selected.paths.back();
				file_entry* last_src = selected.sources.back();

				for(std::size_t i = 0; i < selected.size() - 1; ++i){
					if(selected.sources[i]){
						selected.sources[i]->set_toggled(false);
					}
				}

				selected.clear();
				selected.push_back(last_path, last_src);
				shift_anchor = last_src;
			}
		}
	}

	void file_selector::clear_history(){
		history.clear();
	}

	void file_selector::undo(){
		history.to_prev();
		if(const auto cur = history.try_get()){
			goto_dir_unchecked(*cur);
			set_botton_enable_(*button_redo_, true);
		}
		set_botton_enable_(*button_undo_, history.has_prev());
	}

	void file_selector::redo(){
		history.to_next();
		if(const auto cur = history.try_get()){
			goto_dir_unchecked(*cur);
			set_botton_enable_(*button_undo_, true);
		}
		set_botton_enable_(*button_redo_, history.has_next());
	}

	void file_selector::set_cared_suffix(const std::initializer_list<std::string_view> suffix){
		cared_suffix_.clear();
		for(const auto& basic_string_view : suffix){
			cared_suffix_.insert(path{basic_string_view});
		}
		build_ui_();
	}

	void file_selector::visit_parent_directory(){
		if(auto& currentPath = history.current(); currentPath.has_parent_path()){
			if(currentPath.parent_path() != currentPath){
				visit_directory(currentPath.parent_path());
			} else{
				visit_root_directory();
			}
		} else{
			visit_root_directory();
		}
	}

	void file_selector::visit_root_directory() noexcept{
		set_botton_enable_(*button_to_parent_, false);
		try_add_visit_history({});
		goto_dir_unchecked({});
	}

	void file_selector::visit_directory(path&& p){
		p.make_preferred();
		std::error_code ec;
		if(!std::filesystem::exists(p, ec) || !std::filesystem::is_directory(p, ec)){
			visit_root_directory();
			return;
		}
		if(!try_add_visit_history(p)) return;
		set_botton_enable_(*button_to_parent_, true);
		set_current_path(std::move(p));
	}

	void file_selector::visit_directory(const path& where){
		visit_directory(path{where});
	}

	bool file_selector::is_suffix_met(const path& p) const{
		return cared_suffix_.empty() || cared_suffix_.contains(p.extension());
	}

	bool file_selector::cared_file(const path& p) const noexcept{
		const auto txt = edit_search_->get_text();
		std::error_code ec;
		bool is_dir = std::filesystem::is_directory(p, ec);
		// 即便探测异常我们也回退判断，如果名字匹配且满足扩展名，尽量展示
		return (txt.empty() || p.filename().u32string().contains(txt)) &&
			(is_dir || is_suffix_met(p));
	}

	bool file_selector::is_suffix_met_at_create(const path& file_name) const{
		return (!file_name.has_extension() && cared_suffix_.size() == 1) || is_suffix_met(file_name);
	}

	bool file_selector::is_file_preferred(const path& file_name) const{
		if(!is_suffix_met_at_create(file_name)) return false;
		if(file_name.has_parent_path()) return false;
		std::error_code ec;
		if(std::filesystem::exists(current / file_name, ec)) return false;
		return true;
	}

	bool file_selector::try_add_visit_history(path&& where) noexcept{
		if(const auto p = history.try_get(); p && *p == where) return false;
		history.push(std::move(where));
		set_botton_enable_(*button_redo_, false);
		set_botton_enable_(*button_undo_, history.size() > 1);
		return true;
	}

	bool file_selector::try_add_visit_history(const path& where){
		return try_add_visit_history(path{where});
	}

	void file_selector::goto_dir_unchecked(const path& p){
		if(current != p) set_current_path(p);
	}

	void file_selector::pop_visited_and_resume(){
		history.to_prev();
		history.truncate();
		if(const auto back = history.pop_and_get()){
			visit_directory(std::move(*back));
		} else{
			visit_root_directory();
		}
	}

	void file_selector::set_current_path(path&& current_path) noexcept{
		current = std::move(current_path);
		build_ui_();
	}

	void file_selector::set_current_path(const path& current_path) noexcept{
		set_current_path(path{current_path});
	}

	bool file_selector::create_file(const path& file_name){
		auto p = current / file_name;
		if(!p.has_extension() && cared_suffix_.size() == 1){
			p.replace_extension(*cared_suffix_.begin());
		}
		if(const std::ofstream stream{p, std::ios::app}; stream.is_open()){
			set_current_path(current);
			build_ui_();
			return true;
		}
		return false;
	}

	// 抽取出来的选项清除封装
	void file_selector::clear_selected_ui_state(){
		for(auto* src : selected.sources){
			if(src) src->set_toggled(false);
		}
	}

	void file_selector::reset_selection(){
		clear_selected_ui_state();
		selected.clear();
		shift_anchor = nullptr;
	}

	void file_selector::add_to_selection(file_entry* entry){
		entry->set_toggled(true);
		selected.push_back(entry->path, entry);
	}

	void file_selector::remove_from_selection(std::size_t idx){
		if(selected.sources[idx]){
			selected.sources[idx]->set_toggled(false);
		}
		selected.erase(idx);
	}

	void file_selector::handle_file_selection(file_entry* entry, input_handle::mode mode){
		auto it = std::ranges::find(selected.paths, entry->path);
		bool found = it != selected.paths.end();
		std::size_t idx = found ? std::distance(selected.paths.begin(), it) : 0;

		if(!multiple_selection){
			if(found && selected.size() == 1){
				reset_selection();
			} else{
				reset_selection();
				add_to_selection(entry);
				shift_anchor = entry;
			}
		} else{
			if(mode == input_handle::mode::shift && shift_anchor != nullptr){
				clear_selected_ui_state();
				selected.clear();

				bool in_range = false;
				for(auto& child : entries->children()){
					auto* cur = static_cast<file_entry*>(child);

					if(cur == shift_anchor || cur == entry){
						in_range = !in_range;
						add_to_selection(cur);

						if(shift_anchor == entry || !in_range) break;
						continue;
					}

					if(in_range){
						add_to_selection(cur);
					}
				}
			} else if(mode == input_handle::mode::ctrl){
				if(found){
					remove_from_selection(idx);
				} else{
					add_to_selection(entry);
				}
				shift_anchor = entry;
			} else{
				if(found && selected.size() == 1){
					reset_selection();
				} else{
					for(auto* src : selected.sources){
						if(src && src != entry) src->set_toggled(false);
					}
					selected.clear();
					add_to_selection(entry);
					shift_anchor = entry;
				}
			}
		}

		set_botton_enable_(*button_done_, selected.size());
	}

	void file_selector::toggle_file_selection(file_entry* entry){
		auto it = std::ranges::find(selected.paths, entry->path);
		bool found = it != selected.paths.end();
		std::size_t idx = found ? std::distance(selected.paths.begin(), it) : 0;

		if(multiple_selection){
			if(found){
				remove_from_selection(idx);
			} else{
				add_to_selection(entry);
			}
		} else{
			if(found && selected.size() == 1){
				reset_selection();
			} else{
				reset_selection();
				add_to_selection(entry);
			}
		}
	}

	// ==========================================
	// file_selector 初始化与 UI 构造逻辑
	// ==========================================

	file_selector::file_selector(scene& scene, elem* parent) : head_body(
		scene, parent, layout::layout_policy::hori_major){
		prov_path_->update_value_quiet(this);
		interactivity = interactivity_flag::children_only;
		this->is_transparent_in_inbound_filter = true;

		this->create_head([this](head_body_no_invariant& s){
			s.set_expand_policy(layout::expand_policy::passive);
			s.set_style();

			s.create_head([this](sequence& seq){
				menu = &seq;
				set_style_edge_only(seq);
				seq.set_layout_policy(layout::layout_policy::vert_major);
				seq.set_expand_policy(layout::expand_policy::passive);
				seq.template_cell.set_size({layout::size_category::scaling});
				seq.template_cell.set_pad({2, 2});

				{
					auto b = seq.emplace_back<button<icon_frame>>(assets::builtin::shape_id::check);
					set_style_base_only(b.elem());
					button_done_ = &b.elem();
					b->set_button_callback([this]{
						prov_path_->pull_and_push(false);
					});
				}

				{
					auto b = seq.emplace_back<button<icon_frame>>(assets::builtin::shape_id::left);
					set_style_base_only(b.elem());
					button_undo_ = &b.elem();
					b->set_button_callback([this]{ undo(); });
				}

				{
					auto b = seq.emplace_back<button<icon_frame>>(assets::builtin::shape_id::right);
					set_style_base_only(b.elem());
					button_redo_ = &b.elem();
					b->set_button_callback([this]{ redo(); });
				}

				{
					auto b = seq.emplace_back<button<icon_frame>>(assets::builtin::shape_id::up);
					set_style_base_only(b.elem());
					button_to_parent_ = &b.elem();
					b->set_button_callback([this]{ visit_parent_directory(); });
				}

				seq.create_back([this](current_position_bar& bar){
					bar.set_expand_policy(layout::expand_policy::passive);
					bar.create(0, [this](overflow_sequence& ovf_seq){
						directory_trace_ = &ovf_seq;
						ovf_seq.is_transparent_in_inbound_filter = true;
						ovf_seq.template_cell.set_pending();
						ovf_seq.template_cell.set_pad({2, 2});
						ovf_seq.set_style();

						auto [_, cell] = ovf_seq.create_overflow_elem([](icon_frame& i){
							i.set_style();
						}, gui::assets::builtin::shape_id::more);
						cell.set_size({layout::size_category::scaling});
						ovf_seq.set_layout_policy(layout::layout_policy::vert_major);
						ovf_seq.set_split_index(2);
					});

					bar.create(1, [this](path_edit& edit){
						edit_current_directory_ = &edit;
						edit.set_view_type(text_edit_view_type::align_y);
						edit.set_expand_policy(layout::expand_policy::passive);
						edit.set_style();
					});
				}, *this).cell().set_passive().set_pad({16});
			});

			s.create_body([this](sequence& seq){
				seq.set_layout_policy(layout::layout_policy::vert_major);
				set_style_edge_only(seq);
				seq.template_cell.set_from_ratio().set_pad({2, 2});

				seq.create_back([](sort_type_button& b){ set_style_base_only(b); }, *this);
				seq.create_back([](sort_method_button& b){ set_style_base_only(b); }, *this);

				seq.create_back([this](filter_editor& edit){
					edit_search_ = &edit;
					edit.set_hint_text(U"Search...");
					edit.set_character_filter_mode(false);
					edit.set_filter_characters(platform::get_invalid_filename_chars());
					edit.set_view_type(text_edit_view_type::align_y);
					set_style_side_bar(edit);
				}, *this).cell().set_passive();

				seq.set_expand_policy(layout::expand_policy::passive);
			});
		});

		this->create_body([&](split_pane& b){
			b.set_expand_policy(layout::expand_policy::passive);
			b.set_layout_policy(layout::layout_policy::vert_major);
			b.set_style();
			b.set_split_pos(.3f);

			b.create_head([this](scroll_pane& p){
				set_style_edge_only(p);
				p.set_layout_policy(layout::layout_policy::hori_major);
				p.create([this](sequence& seq){
					seq.set_style();
					seq.template_cell.set_size(60);
					seq.template_cell.set_pad({3, 3});
					for(auto& quick_access_folder : platform::get_quick_access_folders()){
						if(!std::filesystem::is_directory(quick_access_folder)) continue;
						seq.create_back([&, pth = quick_access_folder](button<direct_label>& l){
							set_style_side_bar(l);
							l.set_fit(true);
							l.set_tokenized_text({pth.u32string(), typesetting::tokenize_tag::raw});
							l.set_button_callback([this, pth]{
								visit_directory(pth);
							});
						});
					}
				});
			});

			b.create_body([&](scroll_pane& p){
				set_style_edge_only(p);
				p.set_layout_policy(layout::layout_policy::hori_major);
				p.create([&](sequence& entries_seq){
					entries_seq.set_expand_policy(layout::expand_policy::prefer);
					entries_seq.set_style();
					entries_seq.template_cell.set_size(60);
					entries_seq.template_cell.set_pad({3, 3});
					this->entries = &entries_seq;
				});
			});
		});

		set_head_size({layout::size_category::mastering, 160});
		set_expand_policy(layout::expand_policy::prefer);
		set_pad(8);

		visit_directory(std::filesystem::current_path());
		set_botton_enable_(*button_done_, false);
	}

	void file_selector::build_trace_() noexcept{
		directory_trace_->clear();
		if(current.empty()) return;

		try{
			std::filesystem::path root = current.root_path();
			directory_trace_->emplace_back<trace_entry>(*this, root, true);

			if(root == current) return;
			std::filesystem::path upper = current.lexically_relative(root);

			trace_entry* last{};
			for(auto&& p : upper){
				root /= p;
				auto rst = directory_trace_->emplace_back<trace_entry>(*this, root);
				last = &rst.elem();
			}

			if(last){
				std::error_code ec;
				auto dir_it = std::filesystem::directory_iterator(root, ec);
				bool has_child_dir = false;

				if(!ec){
					for(; dir_it != std::filesystem::directory_iterator{}; dir_it.increment(ec)){
						if(ec) continue;
						if(dir_it->is_directory(ec)){
							has_child_dir = true;
							break;
						}
					}
				}

				if(!has_child_dir){
					last->clear_body();
				}
			}
		} catch(...){
			directory_trace_->clear();
		}
	}

	void file_selector::build_file_entries_(){
		std::error_code ec;
		if(!std::filesystem::exists(current, ec) || !std::filesystem::is_directory(current, ec)){
			auto pathes = platform::get_drive_letters();
			transform_list_to_entry_elements(
				pathes.view()
				| std::views::transform(convert_to<std::filesystem::path>{})
				| std::views::filter(std::bind_front(&file_selector::cared_file, this))
				| std::ranges::to<std::vector>()
			);
		} else{
			try{
				std::vector<std::filesystem::path> valid_paths;
				auto dir_it = std::filesystem::directory_iterator(current, ec);

				if(!ec){
					for(; dir_it != std::filesystem::directory_iterator{}; dir_it.increment(ec)){
						if(ec) continue; // 忽略没有权限（如 Permission Denied）等无法访问的路径
						auto p = dir_it->path();
						if(cared_file(p)){
							valid_paths.push_back(std::move(p));
						}
					}
				} else{
					pop_visited_and_resume();
					return;
				}

				transform_list_to_entry_elements(std::move(valid_paths));
			} catch(...){
				pop_visited_and_resume();
			}
		}
	}

	void file_selector::build_ui_() noexcept{
		build_file_entries_();
		build_trace_();
		edit_current_directory_->apply_edit([this](std::u32string& s){
			s = get_current_directory().u32string();
			return true;
		});
	}
}
