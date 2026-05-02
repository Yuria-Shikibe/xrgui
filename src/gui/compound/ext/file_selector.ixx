module;

#include <mo_yanxi/enum_operator_gen.hpp>

export module mo_yanxi.gui.compound.file_selector;

import std;

export import mo_yanxi.gui.infrastructure;
import mo_yanxi.gui.elem.sequence;
import mo_yanxi.gui.elem.overflow_sequence;
import mo_yanxi.gui.elem.image_frame;
import mo_yanxi.gui.elem.label;
import mo_yanxi.gui.elem.text_edit;
import mo_yanxi.gui.elem.head_body_elem;
import mo_yanxi.gui.elem.button;

import mo_yanxi.history_stack;
import mo_yanxi.gui.assets.manager;

import mo_yanxi.react_flow.common;

namespace mo_yanxi::gui::cpd{
constexpr std::string_view extension_of(const std::string_view path) noexcept{
	const auto pos = path.rfind('.');
	if(pos == std::string_view::npos) return {};
	return path.substr(pos);
}

export enum struct file_sort_category : std::uint8_t{
	alpha, time, size
};

export enum struct file_sort_method : std::uint8_t{
	none,
	descend = 1 << 0,
};

BITMASK_OPS(export, file_sort_method);

struct file_path_sorter{
private:
	using target_type = std::filesystem::path;

	template <bool flip>
	struct file_three_way_comparator{
		static auto operator()(const target_type& lhs, const target_type& rhs) noexcept{
			std::error_code ec_l, ec_r;
			bool is_dir_l = std::filesystem::is_directory(lhs, ec_l);
			bool is_dir_r = std::filesystem::is_directory(rhs, ec_r);

			if(is_dir_l){
				if(is_dir_r){
					return lhs <=> rhs;
				}
				return flip ? std::strong_ordering::greater : std::strong_ordering::less;
			}
			if(is_dir_r){
				return flip ? std::strong_ordering::less : std::strong_ordering::greater;
			}
			return lhs <=> rhs;
		}
	};

public:
	file_sort_category type{};
	file_sort_method method{};

	[[nodiscard]] bool operator()(const target_type& lhs, const target_type& rhs) const noexcept{
		using namespace std::filesystem;
		bool descend = (method & file_sort_method::descend) != file_sort_method::none;
		std::error_code ec1, ec2;

		switch(type){
		case file_sort_category::alpha : return descend
			                                        ? std::is_gt(file_three_way_comparator<true>{}(lhs, rhs))
			                                        : std::is_lt(file_three_way_comparator<false>{}(lhs, rhs));
		case file_sort_category::time :{
			auto t1 = last_write_time(lhs, ec1);
			auto t2 = last_write_time(rhs, ec2);

			if(ec1) t1 = decltype(t1)::min();
			if(ec2) t2 = decltype(t2)::min();
			return descend ? std::ranges::greater{}(t1, t2) : std::ranges::less{}(t1, t2);
		}
		case file_sort_category::size :{
			auto s1 = file_size(lhs, ec1);
			auto s2 = file_size(rhs, ec2);

			if(ec1) s1 = 0;
			if(ec2) s2 = 0;
			return descend ? std::ranges::greater{}(s1, s2) : std::ranges::less{}(s1, s2);
		}
		default : return std::is_gt(file_three_way_comparator<false>{}(lhs, rhs));
		}
	}
};

export class file_selector : public head_body{
	using path = std::filesystem::path;

private:
	std::span<const path> get_selected_paths() const noexcept{
		return selected.paths;
	}

	react_flow::node_holder_pinned<react_flow::provider_member<&file_selector::get_selected_paths>> prov_path_{};

protected:
	struct file_entry;

	struct entry_selection{
		std::vector<path> paths;
		std::vector<file_entry*> sources;

		void clear(){
			paths.clear();
			sources.clear();
		}

		void push_back(const path& p, file_entry* src){
			paths.push_back(p);
			sources.push_back(src);
		}

		void erase(std::size_t index){
			paths.erase(paths.begin() + index);
			sources.erase(sources.begin() + index);
		}

		[[nodiscard]] std::size_t size() const{
			return paths.size();
		}
	};

	struct file_entry : head_body{
		file_selector* selector;
		path path;

		file_selector& get_file_selector() const noexcept;
		[[nodiscard]] file_entry(scene& scene, elem* parent, file_selector& selector, file_selector::path&& entry_path);
		events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override;
		bool set_toggled(bool isToggled) override;
	};

	sequence* menu{};
	sequence* entries{};
	overflow_sequence* directory_trace_{};
	text_edit* edit_current_directory_{};
	text_edit* edit_search_{};

	path current{};
	history_stack<path> history{};

	std::unordered_set<path> cared_suffix_{};

	bool multiple_selection{};
	file_entry* shift_anchor{};
	entry_selection selected{};

	file_sort_category sort_category_{};
	file_sort_method sort_method_{};

	using icon_button_type = button<icon_frame>;

	icon_button_type* button_undo_{};
	icon_button_type* button_redo_{};
	icon_button_type* button_done_{};
	icon_button_type* button_to_parent_{};

public:
	[[nodiscard]] file_selector(scene& scene, elem* parent);
	file_selector(const file_selector& other) = delete;
	file_selector(file_selector&& other) noexcept = delete;
	file_selector& operator=(const file_selector& other) = delete;
	file_selector& operator=(file_selector&& other) noexcept = delete;


	[[nodiscard]] file_sort_category get_sort_category() const noexcept{ return sort_category_; }
	void set_sort_category(const file_sort_category sort_type);

	[[nodiscard]] file_sort_method get_sort_method() const noexcept{ return sort_method_; }
	void set_sort_method(const file_sort_method sort_type);

	[[nodiscard]] const path& get_current_directory() const noexcept{ return current; }
	[[nodiscard]] bool is_under_root_dir() const noexcept{ return history.current().empty(); }

	auto& get_prov() noexcept{ return prov_path_.node; }


	void refresh();
	void set_multiple_selection(bool allow);
	void clear_history();
	void undo();
	void redo();
	void set_cared_suffix(const std::initializer_list<std::string_view> suffix);
	void visit_parent_directory();
	void visit_root_directory() noexcept;
	void visit_directory(path&& p);
	void visit_directory(const path& where);

protected:

	[[nodiscard]] bool is_suffix_met(const path& p) const;
	[[nodiscard]] bool cared_file(const path& p) const noexcept;
	[[nodiscard]] bool is_suffix_met_at_create(const path& file_name) const;
	[[nodiscard]] bool is_file_preferred(const path& file_name) const;

	bool try_add_visit_history(path&& where) noexcept;
	bool try_add_visit_history(const path& where);
	void goto_dir_unchecked(const path& p);
	void pop_visited_and_resume();
	void set_current_path(path&& current_path) noexcept;
	void set_current_path(const path& current_path) noexcept;
	bool create_file(const path& file_name);


	void clear_selected_ui_state();
	void reset_selection();
	void add_to_selection(file_entry* entry);
	void remove_from_selection(std::size_t idx);

	void handle_file_selection(file_entry* entry, input_handle::mode mode);
	void toggle_file_selection(file_entry* entry);

	template <std::ranges::input_range Paths>
	void transform_list_to_entry_elements(Paths&& unsorted_paths){
		std::ranges::sort(unsorted_paths, file_path_sorter{sort_category_, sort_method_});

		for(auto*& src : selected.sources){
			src = nullptr;
		}
		shift_anchor = nullptr;
		entries->clear();

		for(auto&& p : std::forward<Paths>(unsorted_paths)){
			auto hdl = entries->emplace_back<file_entry>(*this, std::move(p));
			auto it = std::ranges::find(selected.paths, hdl->path);
			if(it != selected.paths.end()){
				auto idx = std::distance(selected.paths.begin(), it);
				selected.sources[idx] = &(hdl.elem());
				hdl->set_toggled(true);
			}
		}
	}


	void build_ui_() noexcept;
	void build_file_entries_();
	void build_trace_() noexcept;

	static void set_botton_enable_(icon_button_type& which, bool enabled);
};
}
