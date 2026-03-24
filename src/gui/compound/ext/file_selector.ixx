//
// Created by Matrix on 2025/4/27.
//

export module mo_yanxi.gui.compound.file_selector;

import std;

export import mo_yanxi.gui.infrastructure;
import mo_yanxi.gui.elem.sequence;
import mo_yanxi.gui.elem.scroll_pane;
import mo_yanxi.gui.elem.image_frame;
import mo_yanxi.gui.elem.label;
import mo_yanxi.gui.elem.text_edit_v2;
import mo_yanxi.gui.elem.head_body_elem;
import mo_yanxi.gui.elem.button;
import mo_yanxi.gui.elem.drag_split;

import mo_yanxi.core.platform;
import mo_yanxi.history_stack;
import mo_yanxi.gui.assets.manager;

import mo_yanxi.react_flow.common;


namespace mo_yanxi::gui::cpd{
	constexpr std::string_view extension_of(const std::string_view path) noexcept{
		const auto pos = path.rfind('.');
		if(pos == std::string_view::npos)return {};
		return path.substr(pos);
	}

	export enum struct file_sort_mode{
		ascend = 0,
		descend = 1,
		name = 0b0010,
		time = 0b0100,
		size = 0b1000,
	};

	export constexpr file_sort_mode operator|(const file_sort_mode l, const file_sort_mode r) noexcept{
		return file_sort_mode{(std::to_underlying(l) | std::to_underlying(r))};
	}

	export constexpr file_sort_mode operator-(const file_sort_mode l, const file_sort_mode r) noexcept{
		return file_sort_mode{(std::to_underlying(l) & ~std::to_underlying(r))};
	}

	export constexpr bool operator&(const file_sort_mode l, const file_sort_mode r) noexcept{
		return static_cast<bool>(std::to_underlying(l) & std::to_underlying(r));
	}

	struct file_path_sorter{
	private:
		file_sort_mode type{};
		using target_type = std::filesystem::path;

		template <bool flip>
		struct file_three_way_comparator{
			static auto operator()(const target_type& lhs, const target_type& rhs) noexcept{
				if(std::filesystem::is_directory(lhs)){
					if(std::filesystem::is_directory(rhs)){
						return lhs <=> rhs;
					}

					if constexpr(flip){
						return std::strong_ordering::greater;
					}else{
						return std::strong_ordering::less;
					}
				}

				if(std::filesystem::is_directory(rhs)){
					if constexpr(flip){
						return std::strong_ordering::less;
					}else{
						return std::strong_ordering::greater;
					}
				}

				return lhs <=> rhs;
			}
		};
	public:
		[[nodiscard]] constexpr file_path_sorter() = default;

		[[nodiscard]] constexpr explicit(false) file_path_sorter(const file_sort_mode type)
			: type(type){
		}

		[[nodiscard]] constexpr bool operator()(const target_type& lhs, const target_type& rhs) const noexcept{
			using namespace std::filesystem;
			switch(std::to_underlying(type)){
			case std::to_underlying(file_sort_mode::name | file_sort_mode::descend) :{
				return std::is_gt(file_three_way_comparator<true>{}(lhs, rhs));
			}
			case std::to_underlying(file_sort_mode::name | file_sort_mode::ascend):{
				return std::is_lt(file_three_way_comparator<false>{}(lhs, rhs));
			}
			case std::to_underlying(file_sort_mode::time | file_sort_mode::descend):{
				return std::ranges::greater{}(last_write_time(lhs), last_write_time(rhs));
			}
			case std::to_underlying(file_sort_mode::time | file_sort_mode::ascend):{
				return std::ranges::less{}(last_write_time(lhs), last_write_time(rhs));
			}
			case std::to_underlying(file_sort_mode::size | file_sort_mode::descend):{
				return std::ranges::greater{}(file_size(lhs), file_size(rhs));
			}
			case std::to_underlying(file_sort_mode::size | file_sort_mode::ascend):{
				return std::ranges::less{}(file_size(lhs), file_size(rhs));
			}
			default : return std::is_gt(file_three_way_comparator<false>{}(lhs, rhs));
			}
		}
	};



	export
	class file_selector : public head_body{
		using path = std::filesystem::path;

	private:
		static void set_elem_style_(elem& e, std::string_view name){
			e.set_style(e.get_style_manager().get_slice<style::elem_style_drawer>()->get_or_default(name));
		}

		static auto get_side_bar_style(const elem& e){
			return e.get_style_manager().get_slice<style::elem_style_drawer>()->get_or_default("side_bar_left");
		}

		static void set_style_edge_only(elem& e){
			set_elem_style_(e, "round_edge_only");
		}

		static void set_style_base_only(elem& e){
			set_elem_style_(e, "round_base_only");
		}

		std::span<const path> get_selected_paths() const noexcept{
			return selected.paths;
		}

		react_flow::node_holder<react_flow::provider_member<&file_selector::get_selected_paths>> prov_path_{};

	protected:

		struct file_entry;

		struct entry_selection {
			std::vector<path> paths;
			std::vector<file_entry*> sources;

			void clear() {
				paths.clear();
				sources.clear();
			}

			void push_back(const path& p, file_entry* src) {
				paths.push_back(p);
				sources.push_back(src);
			}

			void erase(size_t index) {
				paths.erase(paths.begin() + index);
				sources.erase(sources.begin() + index);
			}

			[[nodiscard]] size_t size() const {
				return paths.size();
			}
		};

		struct file_entry : head_body{
			file_selector* selector;
			path path;

			file_selector& get_file_selector() const noexcept{
				return *selector;
			}

			[[nodiscard]] file_entry(scene& scene, elem* parent, file_selector& selector, file_selector::path&& entry_path)
				: head_body(scene, parent, layout::layout_policy::vert_major), selector(&selector), path(std::move(entry_path)){
				interactivity = interactivity_flag::intercept;

				bool is_dir = std::filesystem::is_directory(path);
				bool is_root = path == path.parent_path();

				set_style(get_side_bar_style(*this));

				auto& i = this->emplace_head<icon_frame>(is_root ? assets::builtin::shape_id::data_server : is_dir ? assets::builtin::shape_id::folder : assets::builtin::shape_id::file);
				i.set_style();

				this->create_body([&](direct_label& l){
					l.set_style();
					l.set_fit(true);
					l.text_entire_align = align::pos::center_left;
					l.set_tokenized_text({is_root ? path.u32string() : path.filename().u32string(), typesetting::tokenize_tag::raw});
					l.set_transform_config({.scale = {.75f, .75f}});
				});

				set_head_size({layout::size_category::scaling});
				set_pad(16);
				set_fill_parent({true});
				set_expand_policy(layout::expand_policy::passive);
			}

			events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override{
				elem::on_click(event, aboves);
				if(event.key.on_release() && event.within_elem(*this)){
					auto& menu = get_file_selector();
					if(std::filesystem::is_directory(path)){
						menu.visit_directory(path);
					}else{
						// 传入 mode_bits 处理复杂的选择逻辑
						menu.handle_file_selection(this, event.key.mode_bits);
					}
				}
				return events::op_afterwards::intercepted;
			}

		};

		sequence* menu{};
		sequence* entries{};

		path current{};
		history_stack<path> history{};

		std::string search_key{};
		std::unordered_set<path> cared_suffix_{};

		bool multiple_selection{};
		file_entry* shift_anchor{};
		entry_selection selected{};

		file_sort_mode sortType{file_sort_mode::name | file_sort_mode::ascend};

		[[nodiscard]] bool is_suffix_met(const path& path) const{
			return cared_suffix_.empty() || cared_suffix_.contains(path.extension());
		}

		[[nodiscard]] bool cared_file(const path& path) const noexcept{
			return true
				 && (search_key.empty() || path.filename().string().contains(search_key))
				 && (std::filesystem::is_directory(path) || is_suffix_met(path));
		}

		bool try_add_visit_history(path&& where) noexcept{
			if(const auto p = history.try_get(); p && *p == where)return false;
			history.push(std::move(where));
			set_botton_enable_(*button_redo_, false);
			set_botton_enable_(*button_undo_, history.size() > 1);
			return true;
		}

		bool try_add_visit_history(const path& where){
			return try_add_visit_history(path{where});
		}

		void goto_dir_unchecked(const path& path){
			if(current != path)set_current_path(path);
		}

		void pop_visited_and_resume(){
			history.to_prev();
			history.truncate();
			if(const auto back = history.pop_and_get()){
				visit_directory(std::move(*back));
			}else{
				visit_root_directory();
			}
		}

		void handle_file_selection(file_entry* entry, input_handle::mode mode) {
			auto it = std::ranges::find(selected.paths, entry->path);
			bool found = it != selected.paths.end();
			std::size_t idx = found ? std::distance(selected.paths.begin(), it) : 0;

			[&]{

			// 单选模式
			if (!multiple_selection) {
				if (found && selected.size() == 1) {
					// 再次点击已选中的唯一项：取消选中
					entry->set_toggled(false);
					selected.clear();
					shift_anchor = nullptr;
				} else {
					for (auto* src : selected.sources) {
						if (src) src->set_toggled(false);
					}
					selected.clear();

					entry->set_toggled(true);
					selected.push_back(entry->path, entry);
					shift_anchor = entry;
				}
				return;
			}

			// 多选模式：按住 Shift 连选
			if (mode == input_handle::mode::shift && shift_anchor != nullptr) {
				for (auto* src : selected.sources) {
					if (src) src->set_toggled(false);
				}
				selected.clear();

				bool in_range = false;
				for (auto& child : entries->children()) {
					auto* cur = static_cast<file_entry*>(child);

					if (cur == shift_anchor || cur == entry) {
						in_range = !in_range;

						cur->set_toggled(true);
						selected.push_back(cur->path, cur);

						if (shift_anchor == entry) break;
						if (!in_range) break;
						continue;
					}

					if (in_range) {
						cur->set_toggled(true);
						selected.push_back(cur->path, cur);
					}
				}
			}
			// 多选模式：按住 Ctrl 点选
			else if (mode == input_handle::mode::ctrl) {
				if (found) {
					// 如果已选中，则取消选中
					entry->set_toggled(false);
					selected.erase(idx);
				} else {
					// 如果未选中，则追加选中
					entry->set_toggled(true);
					selected.push_back(entry->path, entry);
				}
				shift_anchor = entry;
			}
			// 多选模式：普通点击 (无修饰键)
			else {
				if (found && selected.size() == 1) {
					// 需求 1：如果只选中了这一个文件，则取消它的选中
					entry->set_toggled(false);
					selected.clear();
					shift_anchor = nullptr;
				} else {
					// 需求 2：如果选中了多个文件，则只保留这个文件 (未选中时也走此逻辑，选中自身)
					for (auto* src : selected.sources) {
						// 优化：跳过当前 entry，避免先设为 false 再设为 true 引起 UI 闪烁
						if (src && src != entry) {
							src->set_toggled(false);
						}
					}
					selected.clear();

					entry->set_toggled(true);
					selected.push_back(entry->path, entry);
					shift_anchor = entry;
				}
			}
			}();

			set_botton_enable_(*button_done_, selected.size());
		}

		void toggle_file_selection(file_entry* entry) {
			auto it = std::ranges::find(selected.paths, entry->path);
			bool found = it != selected.paths.end();
			size_t idx = found ? std::distance(selected.paths.begin(), it) : 0;

			if (multiple_selection) {
				if (found) {
					entry->set_toggled(false);
					selected.erase(idx);
				} else {
					entry->set_toggled(true);
					selected.push_back(entry->path, entry);
				}
			} else {
				if (found && selected.size() == 1) {
					entry->set_toggled(false);
					selected.clear();
				} else {
					for (auto* src : selected.sources) {
						if (src) src->set_toggled(false);
					}
					selected.clear();
					entry->set_toggled(true);
					selected.push_back(entry->path, entry);
				}
			}
		}

		template <std::ranges::input_range Paths>
		void transform_list_to_entry_elements(Paths&& unsorted_paths){
			std::ranges::sort(unsorted_paths, file_path_sorter{sortType});

			// 使用引用修改 sources 中的指针
			for (auto*& src : selected.sources) {
				src = nullptr;
			}
			shift_anchor = nullptr;

			entries->clear();

			for (std::filesystem::path path : std::forward<Paths>(unsorted_paths)){
				auto hdl = entries->emplace_back<file_entry>(*this, std::move(path));

				// 在 paths 数组中查找
				auto it = std::ranges::find(selected.paths, hdl->path);
				if (it != selected.paths.end()) {
					// 计算索引并同步更新 sources 数组
					auto idx = std::distance(selected.paths.begin(), it);
					selected.sources[idx] = &(hdl.elem());
					hdl->set_toggled(true);
				}
			}
		}

		void build_ui() noexcept{
			// current_entry_bar->set_text(std::format("..< | {:?}", current.string()));

			if(!std::filesystem::exists(current) || !std::filesystem::is_directory(current)){
				auto pathes = platform::get_drive_letters();
				transform_list_to_entry_elements(
					pathes.view()
					| std::views::transform(convert_to<std::filesystem::path>{})
					| std::views::filter(std::bind_front(&file_selector::cared_file, this))
					| std::ranges::to<std::vector>());
			}else{
				try{
					transform_list_to_entry_elements(std::filesystem::directory_iterator(current)
						| std::views::transform(&std::filesystem::directory_entry::path)
						| std::views::filter(std::bind_front(&file_selector::cared_file, this))
						| std::ranges::to<std::vector>());
				}catch(...){
					pop_visited_and_resume();
				}
			}
		}

		void set_current_path(path&& current_path) noexcept{
			current = std::move(current_path);
			build_ui();
		}

		void set_current_path(const path& current_path) noexcept{
			set_current_path(path{current_path});
		}


	public:
		[[nodiscard]] file_selector(scene& scene, elem* parent)
			: head_body(scene, parent, layout::layout_policy::hori_major){
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
				});

				s.create_body([](sequence& s){
					set_style_edge_only(s);
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
						for (auto& quick_access_folder : platform::get_quick_access_folders()){
							if(!std::filesystem::is_directory(quick_access_folder))continue;
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

				// b.set_style();
				// {
				// 	auto return_to_parent_button = b.emplace_back<button<label>>();
				// 	return_to_parent_button->set_fit(true);
				// 	return_to_parent_button->set_text(std::format("..<"));
				// 	return_to_parent_button->set_button_callback([this]{
				// 		visit_parent_directory();
				// 	});
				// 	return_to_parent_button.cell().set_size(80).set_pad({8, 8});
				//
				// 	//TODO set diable listener
				// }

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

		void set_multiple_selection(bool allow){
			// util::try_modify 成功修改值时返回 true
			if(util::try_modify(multiple_selection, allow)){
				// 当且仅当从多选切换至单选 (!allow)，且当前选中的文件数量大于 1 时触发截断逻辑
				if(!allow && selected.size() > 1){
					// 1. 暂存最后一次选中的文件数据
					path last_path = selected.paths.back();
					file_entry* last_src = selected.sources.back();

					// 2. 遍历除最后一项外的所有元素，重置它们的 UI 状态
					for(size_t i = 0; i < selected.size() - 1; ++i){
						if(selected.sources[i]){
							selected.sources[i]->set_toggled(false);
						}
					}

					// 3. 清空选择器数据，并将保留的最后一项重新入队
					selected.clear();
					selected.push_back(last_path, last_src);

					// 4. 将锚点重置为这唯一的一项
					shift_anchor = last_src;
				}
			}
		}

		auto& get_prov() noexcept {
			return prov_path_.node;
		}

		void clear_history(){
			history.clear();
		}

		void undo(){
			history.to_prev();
			if(const auto cur = history.try_get()){
				goto_dir_unchecked(*cur);
				set_botton_enable_(*button_redo_, true);
			}
			set_botton_enable_(*button_undo_, history.has_prev());
		}

		void redo(){
			history.to_next();

			if(const auto cur = history.try_get()){
				goto_dir_unchecked(*cur);
				set_botton_enable_(*button_undo_, true);
			}
			set_botton_enable_(*button_redo_, history.has_next());

		}

		void set_cared_suffix(const std::initializer_list<std::string_view> suffix){
			cared_suffix_.clear();
			for (const auto & basic_string_view : suffix){
				cared_suffix_.insert(basic_string_view);
			}

			build_ui();
		}

		[[nodiscard]] bool is_under_root_dir() const noexcept{
			return history.current().empty();
		}

		void visit_parent_directory(){
			if(auto& currentPath = history.current(); currentPath.has_parent_path()){
				if(currentPath.parent_path() != currentPath){
					visit_directory(currentPath.parent_path());
				}
				else visit_root_directory();
			}else{
				visit_root_directory();
			}
		}

		void visit_root_directory() noexcept{
			set_botton_enable_(*button_to_parent_, false);
			try_add_visit_history({});
			goto_dir_unchecked({});
		}

		void visit_directory(path&& path){
			if(!std::filesystem::exists(path) || !std::filesystem::is_directory(path)){
				visit_root_directory();
				return;
			}

			if(!try_add_visit_history(path)){
				return;
			}

			set_botton_enable_(*button_to_parent_, true);
			set_current_path(std::move(path));
		}

		void visit_directory(const path& where){
			visit_directory(path{where});
		}

		file_selector(const file_selector& other) = delete;
		file_selector(file_selector&& other) noexcept = delete;
		file_selector& operator=(const file_selector& other) = delete;
		file_selector& operator=(file_selector&& other) noexcept = delete;


	protected:
		[[nodiscard]] bool is_suffix_met_at_create(const path& file_name) const{
			return (!file_name.has_extension() && cared_suffix_.size() == 1) || is_suffix_met(file_name);
		}

		[[nodiscard]] bool is_file_preferred(const path& file_name) const{
			if(!is_suffix_met_at_create(file_name))return false;
			if(file_name.has_parent_path())return false;
			if(std::filesystem::exists(current / file_name))return false;

			return true;
		}

		bool create_file(const path& file_name){
			auto p = current / file_name;
			if(!p.has_extension() && cared_suffix_.size() == 1){
				p.replace_extension(*cared_suffix_.begin());
			}

			if(const std::ofstream stream{p, std::ios::app}; stream.is_open()){
				set_current_path(current);
				build_ui();
				return true;
			}
			return false;
		}

		// void add_file_create_button(){
		// 	auto b = menu->end_line().emplace<button<icon_frame>>(theme::icons::plus);
		// 	b->set_tooltip_state(tooltip_create_info{
		// 		.layout_info = {
		// 			.follow = tooltip_follow::owner,
		// 			.align_owner = align::pos::center_right,
		// 			.align_tooltip = align::pos::center_left,
		// 		},
		// 		.use_stagnate_time = false,
		// 		.auto_release = false,
		// 		.min_hover_time = tooltip_create_info::disable_auto_tooltip
		// 	}, [this](ui::table& table){
		//
		// 		auto cb = table.emplace<button<icon_frame>>(theme::icons::check);
		// 		cb->set_style(theme::styles::no_edge);
		// 		cb.cell().set_width(60);
		// 		cb.cell().pad.right = 8;
		//
		//
		// 		auto area = table.emplace<text_input_area>();
		// 		area->set_scale(.75f);
		// 		area->set_style(theme::styles::no_edge);
		//
		// 		area->add_file_banned_characters();
		// 		area.cell().set_external();
		//
		// 		cb->set_button_callback(button_tags::general, [this, &t = area.elem()]{
		// 			create_file(t.get_text());
		// 		});
		//
		// 		cb->checkers.setDisableProv([this, &t = area.elem()](){
		// 			const auto txt = t.get_text();
		// 			if(txt.empty())return true;
		// 			return !is_file_preferred(txt);
		// 		});
		//
		// 	});
		//
		// 	b->set_style(theme::styles::no_edge);
		// 	b->set_button_callback(button_tags::general, [](elem& elem){
		// 		if(!elem.has_tooltip()){
		// 			elem.build_tooltip();
		// 		}else{
		// 			elem.tooltip_notify_drop();
		// 		}
		// 	});
		// }

	private:

		//fuck msvc so i have to put them at end
		using icon_button_type = button<icon_frame>;

		icon_button_type* button_undo_{};
		icon_button_type* button_redo_{};
		icon_button_type* button_done_{};
		icon_button_type* button_to_parent_{};


		static void set_botton_enable_(icon_button_type& which, bool enabled){
			which.set_disabled(!enabled);
			which.scale_color = enabled ? graphic::colors::white : graphic::colors::gray;
		}
	};

	// export
	// template <std::derived_from<elem> T, std::predicate<const file_selector&, const T&> Checker, std::predicate<file_selector&, T&> Yielder>
	// struct file_selector_create_info{
	// 	T& requester;
	// 	Checker checker;
	// 	Yielder yielder;
	// 	bool add_file_create_button{};
	// };
	//
	// export
	// template <std::derived_from<elem> T, std::predicate<const file_selector&, const T&> Checker, std::predicate<file_selector&, T&> Yielder>
	// file_selector& create_file_selector(file_selector_create_info<T, Checker, Yielder>&& create_info){
	// 	struct selector : file_selector{
	// 		T* owner;
	// 		std::decay_t<Checker> checker;
	// 		std::decay_t<Yielder> yielder;
	//
	// 		[[nodiscard]] selector(scene* scene, group* group, file_selector_create_info<T, Checker, Yielder>& create_info)
	// 			: file_selector(scene, group), owner(&create_info.requester), checker(std::forward<Checker>(create_info.checker)), yielder(std::forward<Yielder>(create_info.yielder)){
	//
	// 			auto close_b = menu->end_line().emplace<button<icon_frame>>(theme::icons::close);
	// 			close_b->set_style(theme::styles::no_edge);
	// 			close_b->set_button_callback(button_tags::general, [this]{
	// 				dialog_notify_drop();
	// 			});
	//
	// 			auto confirm_b = menu->end_line().emplace<button<icon_frame>>(theme::icons::check);
	// 			confirm_b->set_style(theme::styles::no_edge);
	// 			confirm_b->set_button_callback(button_tags::general, [this]{
	// 				yield_path();
	// 			});
	// 			confirm_b->checkers.setDisableProv([this]{
	// 				return !check_path();
	// 			});
	//
	// 			if(create_info.add_file_create_button){
	// 				add_file_create_button();
	// 			}
	//
	// 		}
	//
	// 		void yield_path() override{
	// 			if(std::invoke(yielder, *this, *owner)){
	// 				dialog_notify_drop();
	// 			}
	// 		}
	//
	// 		bool check_path() const override{
	// 			return std::invoke(checker, *this, *owner);
	// 		}
	//
	// 		// esc_flag on_esc() override{
	// 		// 	dialog_notify_drop();
	// 		// 	return esc_flag::intercept;
	// 		// }
	// 	};
	//
	//
	// 	scene& scene = *create_info.requester.get_scene();
	//
	// 	file_selector& selector = scene.dialog_manager.emplace<struct selector>({}, create_info);
	// 	selector.prop().fill_parent = {true, true};
	// 	return selector;
	// }
}
