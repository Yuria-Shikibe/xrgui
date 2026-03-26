module mo_yanxi.gui.compound.file_selector;

import mo_yanxi.gui.fx.compound;
import mo_yanxi.gui.fx.fringe;
import mo_yanxi.math.interpolation;
import mo_yanxi.gui.elem.table;
import mo_yanxi.gui.elem.double_side;
import mo_yanxi.gui.elem.check_box;

namespace mo_yanxi::gui::cpd{
struct trace_entry;

struct arrow_button : elem{
	enum state{
		closed,
		expanding,
		expanded,
		closing,
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
		}else{
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
			case closed : s = expanding;
				if(!has_tooltip() && !invisible) create_tooltip();
				break;
			case expanding : s = closing;
				drop_tooltip();
				break;
			case expanded : s = closing;
				drop_tooltip();
				break;
			case closing : s = expanding;
				if(!has_tooltip() && !invisible) create_tooltip();
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

	[[nodiscard]] trace_entry(scene& scene, elem* parent, file_selector& selector, const std::filesystem::path& path_,
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

arrow_button::arrow_button(scene& scene, elem* parent): elem(scene, parent){
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
					for(auto&& pth : std::filesystem::directory_iterator{s.get_trace().path}){
						if(!pth.is_directory())continue;
						++count;
						seq.create_back([&](button<direct_label>& but){
							but.set_style(get_side_bar_style(but));
							but.set_fit_type(label_fit_type::scl);
							but.set_tokenized_text({
									pth.path().filename().u32string(), typesetting::tokenize_tag::raw
								});
							but.set_button_callback([this, p = pth]{
								get_trace().selector->visit_directory(p);
							});
						});
					}
				});
			});

			if(count){
				hdl.cell().set_size({400.f, std::min(8uz, count) * 80.f});
			}else{
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
		set_style(get_side_bar_style(*this));
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
		// auto& e = at(1);
		// auto pos = e.transform_to_content_space(event.pos);
		// event.pos = pos;
		// e.on_click(event, {});
		return events::op_afterwards::intercepted;
	}
	//
	// events::op_afterwards on_drag(events::drag event) override{
	// 	auto& e = get_current_active();
	// 	event.src = e.transform_to_content_space(event.src);
	// 	event.dst = e.transform_to_content_space(event.dst);
	// 	return e.on_drag(event);
	// }

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
		if(!isFocused)get_current_position_bar().switch_to(0);
	}

	void action_enter() override{
		try{
			std::filesystem::path path = get_text();
			path = std::filesystem::absolute(path).make_preferred();
			if(std::filesystem::is_directory(path)){
				get_current_position_bar().selector->visit_directory(std::move(path));
			}else{
				set_input_invalid();
			}
		}catch(...){
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
		assets::builtin::shape_id ids[] = {assets::builtin::shape_id::alphabetical_sorting, assets::builtin::shape_id::time, assets::builtin::shape_id::row_height};
		for(int i = 0; i < std::ranges::size(ids); ++i){
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
		assets::builtin::shape_id ids[] = {assets::builtin::shape_id::arrow_up, assets::builtin::shape_id::arrow_down};
		for(int i = 0; i < std::ranges::size(ids); ++i){
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

file_selector::file_selector(scene& scene, elem* parent) : head_body(scene, parent, layout::layout_policy::hori_major){
	prov_path_->update_value_quiet(this);
	interactivity = interactivity_flag::children_only;
	set_style_edge_only(*this);

	this->create_head([this](head_body_no_invariant& s){
		s.set_expand_policy(layout::expand_policy::passive);
		s.set_style();

		s.create_head([this](sequence& s){
			menu = &s;
			set_style_edge_only(s);
			s.set_layout_policy(layout::layout_policy::vert_major);
			s.set_expand_policy(layout::expand_policy::passive);
			s.template_cell.set_size({layout::size_category::scaling});
			s.template_cell.set_pad({2, 2});

#pragma region SetupMenuButtons
			{
				auto b = s.emplace_back<button<icon_frame>>(assets::builtin::shape_id::check);
				set_style_base_only(b.elem());
				button_done_ = &b.elem();
				b->set_button_callback([this]{
					prov_path_->pull_and_push(false);
				});
			}

			{
				auto b = s.emplace_back<button<icon_frame>>(assets::builtin::shape_id::left);
				set_style_base_only(b.elem());
				button_undo_ = &b.elem();
				b->set_button_callback([this]{
					undo();
				});
			}

			{
				auto b = s.emplace_back<button<icon_frame>>(assets::builtin::shape_id::right);
				set_style_base_only(b.elem());
				button_redo_ = &b.elem();
				b->set_button_callback([this]{
					redo();
				});
			}

			{
				auto b = s.emplace_back<button<icon_frame>>(assets::builtin::shape_id::up);
				set_style_base_only(b.elem());
				button_to_parent_ = &b.elem();
				b->set_button_callback([this]{
					visit_parent_directory();
				});
			}
#pragma endregion

			s.create_back([this](current_position_bar& bar){
				bar.set_expand_policy(layout::expand_policy::passive);
				bar.create(0, [this](overflow_sequence& seq){
					directory_trace_ = &seq;
					seq.is_transparent_in_inbound_filter = true;
					seq.template_cell.set_pending();
					seq.template_cell.set_pad({2, 2});
					seq.set_style();

					auto [_, cell] = seq.create_overflow_elem([](icon_frame& i){
						i.set_style();
					}, gui::assets::builtin::shape_id::more);
					cell.set_size({layout::size_category::scaling});
					seq.set_layout_policy(layout::layout_policy::vert_major);
					seq.set_split_index(2);
				});

				bar.create(1, [this](path_edit& edit){
					edit_current_directory_ = &edit;
					edit.set_view_type(text_edit_view_type::align_y);
					edit.set_expand_policy(layout::expand_policy::passive);
					edit.set_style();
				});
			}, *this).cell().set_passive().set_pad({16});

		});

		s.create_body([this](sequence& s){
			s.set_layout_policy(layout::layout_policy::vert_major);
			set_style_edge_only(s);
			s.template_cell.set_from_ratio().set_pad({2, 2});

			{
				s.create_back([](sort_type_button& b){
					set_style_base_only(b);
				}, *this);

				s.create_back([](sort_method_button& b){
					set_style_base_only(b);
				}, *this);

			}

			s.create_back([this](filter_editor& edit){
				edit_search_ = &edit;
				edit.set_hint_text(U"Search...");
				edit.set_character_filter_mode(false);
				edit.set_filter_characters(platform::get_invalid_filename_chars());
				edit.set_view_type(text_edit_view_type::align_y);
				edit.set_style(get_side_bar_style(edit));
			}, *this).cell().set_passive();


			s.set_expand_policy(layout::expand_policy::passive);
		});
	});

	//
	this->create_body([&](split_pane& b){
		b.set_expand_policy(layout::expand_policy::passive);
		b.set_layout_policy(layout::layout_policy::vert_major);
		b.set_style();
		b.set_split_pos(.3f);
		b.create_head([this](scroll_pane& p){
			set_style_edge_only(p);
			p.set_layout_policy(layout::layout_policy::hori_major);
			p.create([this](sequence& s){
				s.set_style();
				s.template_cell.set_size(60);
				s.template_cell.set_pad({3, 3});
				for(auto& quick_access_folder : platform::get_quick_access_folders()){
					if(!std::filesystem::is_directory(quick_access_folder)) continue;
					s.create_back([&](button<direct_label>& l){
						l.set_style(get_side_bar_style(l));
						l.set_fit(true);
						l.set_tokenized_text({quick_access_folder.u32string(), typesetting::tokenize_tag::raw});
						l.set_button_callback([this, pth = std::move(quick_access_folder)]{
							visit_directory(pth);
						});
					});
				}
			});
		});


		b.create_body([&](scroll_pane& p){
			set_style_edge_only(p);
			p.set_layout_policy(layout::layout_policy::hori_major);
			p.create([&](sequence& entries){
				entries.set_expand_policy(layout::expand_policy::prefer);
				entries.set_style();
				entries.template_cell.set_size(60);
				entries.template_cell.set_pad({3, 3});
				this->entries = &entries;
			});
		});
	});
	//
	// set_expand_policy(layout::expand_policy::prefer);
	set_head_size({layout::size_category::mastering, 160});
	set_expand_policy(layout::expand_policy::prefer);
	set_pad(8);

	visit_directory(std::filesystem::current_path());
	set_botton_enable_(*button_done_, false);
}

void file_selector::build_trace_() noexcept{
	directory_trace_->clear();
	if(current.empty()){
		return;
	}

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

		if(last && std::ranges::none_of(
			std::filesystem::directory_iterator{root},
			static_cast<bool(std::filesystem::directory_entry::*)() const>(&std::filesystem::directory_entry::is_directory))){
			last->clear_body();
		}
	}catch(...){
		directory_trace_->clear();
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

void file_selector::build_file_entries_(){
	if(!std::filesystem::exists(current) || !std::filesystem::is_directory(current)){
		auto pathes = platform::get_drive_letters();
		transform_list_to_entry_elements(
			pathes.view()
			| std::views::transform(convert_to<std::filesystem::path>{})
			| std::views::filter(std::bind_front(&file_selector::cared_file, this))
			| std::ranges::to<std::vector>());
	} else{
		try{
			transform_list_to_entry_elements(std::filesystem::directory_iterator(current)
				| std::views::transform(&std::filesystem::directory_entry::path)
				| std::views::filter(std::bind_front(&file_selector::cared_file, this))
				| std::ranges::to<std::vector>());
		} catch(...){
			pop_visited_and_resume();
		}
	}
}
}
