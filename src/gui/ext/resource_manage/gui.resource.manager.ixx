module;


export module mo_yanxi.gui.assets.manager;

import mo_yanxi.gui.alloc;
export import mo_yanxi.gui.image_regions;
export import mo_yanxi.resource_manager;

import std;

namespace mo_yanxi::gui::assets {

export using resource_id = mo_yanxi::resource::resource_id;
export using duplicated_error = mo_yanxi::resource::duplicated_error;

export
template <typename Handle, typename Allocator = std::allocator<std::byte>>
using basic_assets_page = mo_yanxi::resource::basic_assets_page<Handle, Allocator>;

export
template <typename Handle, typename Allocator = std::allocator<std::byte>>
using basic_resource_manager = mo_yanxi::resource::basic_resource_manager<Handle, Allocator>;

using image_resource_allocator = mr::heap_allocator<std::byte>;
using image_resource_manager = basic_resource_manager<constant_image_region_borrow, image_resource_allocator>;

export using assets_page = image_resource_manager::page_type;

export
struct resource_manager{
private:
    mr::heap heap_{};
	image_resource_manager manager_;

public:
    [[nodiscard]] explicit resource_manager(const mr::arena_id_t arena_id)
        : heap_(arena_id, 1), manager_(image_resource_allocator{heap_.get()}) {
    }

    template <typename T>
        requires (std::constructible_from<std::string, T&&>)
    assets_page& create_page(T&& page_name) {
        return manager_.create_page(std::forward<T>(page_name));
    }

	[[nodiscard]] std::optional<constant_image_region_borrow> operator[](std::string_view page_name, std::string_view alias) const noexcept {
		return manager_[page_name, alias];
    }

	[[nodiscard]] std::optional<constant_image_region_borrow> operator[](std::string_view page_name, resource_id id) const noexcept {
		return manager_[page_name, id];
    }

    [[nodiscard]] std::optional<constant_image_region_borrow> operator[](std::string_view full_name) const noexcept {
        return manager_[full_name];
    }

};

} // namespace mo_yanxi::gui::assets



namespace mo_yanxi::gui{


namespace assets::builtin{
export inline constexpr auto page_name_str_array = std::to_array("__builtin_shape");
export inline constexpr std::string_view page_name = {page_name_str_array.data(), page_name_str_array.size() - 1};

export
enum struct shape_id{
	white,

	logo,

	row_separator,
	round_square_edge,
	round_square_edge_thin,
	round_square_base,
	side_bar,
	circle,

	alphabetical_sorting,
	arrow_down,
	arrow_left,
	arrow_left_down,
	arrow_left_up,
	arrow_right,
	arrow_right_down,
	arrow_right_up,
	arrow_up,
	check,
	close,
	code,
	code_brackets,
	components,
	data_server,
	down,
	file,
	file_addition_one,
	file_code_one,
	file_failed_one,
	folder,
	folder_code_one,
	folder_minus,
	folder_plus,
	left,
	loading,
	loading_four,
	minus,
	more,
	plus,
	right,
	row_height,
	search,
	sort_two,
	textarea,
	time,
	to_bottom_one,
	to_top_one,
	up,
};


}


namespace global{

union U{
	assets::resource_manager resource_manager;

	[[nodiscard]] U(){}

	~U(){}
};

extern U u;
export
[[nodiscard]] assets::resource_manager& resource_manager() noexcept;

export
void initialize_assets_manager(mr::arena_id_t arena_id);

export
bool terminate_assets_manager() noexcept;
}

namespace assets::builtin{
export
[[nodiscard]] assets_page& get_page() noexcept;
}
}

