module;


export module mo_yanxi.gui.assets.manager;

import mo_yanxi.gui.alloc;
export import mo_yanxi.gui.image_regions;
export import mo_yanxi.resource_manager;

import mo_yanxi.graphic.image_atlas;
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

namespace round_square{
static_assert(sizeof(resource_id) == sizeof(std::uint64_t));

export
[[nodiscard]] constexpr std::string_view page_name() noexcept{
	return "__round_square_shape";
}

export
enum struct mode : std::uint8_t{
	solid = 1,
	stroke = 2
};

export
struct fixed2{
	std::uint16_t raw{};

	[[nodiscard]] constexpr float value() const noexcept{
		return static_cast<float>(raw) / 4.f;
	}

	friend constexpr bool operator==(const fixed2&, const fixed2&) noexcept = default;
};

export
struct key{
	mode shape_mode{};
	fixed2 radius{};
	fixed2 attribute{};

	friend constexpr bool operator==(const key&, const key&) noexcept = default;
};

inline constexpr unsigned mode_shift = 56;
inline constexpr unsigned radius_shift = 40;
inline constexpr unsigned attribute_shift = 24;
inline constexpr resource_id field_mask = 0xFFFF;
inline constexpr resource_id reserved_mask = (resource_id{1} << attribute_shift) - 1;

export
[[nodiscard]] fixed2 make_fixed2(float value);

export
[[nodiscard]] constexpr resource_id to_id(const key value) noexcept{
	return (static_cast<resource_id>(value.shape_mode) << mode_shift)
		| (static_cast<resource_id>(value.radius.raw) << radius_shift)
		| (static_cast<resource_id>(value.attribute.raw) << attribute_shift);
}

export
[[nodiscard]] key from_id(resource_id id);

export
[[nodiscard]] key base_key(float radius = 8.f);

export
[[nodiscard]] key border_key(float radius = 8.f, float width = 2.f);

export
[[nodiscard]] const image_nine_region& get(key value);

export
[[nodiscard]] const image_nine_region& get(resource_id id);

export
[[nodiscard]] const image_nine_region& base(float radius = 8.f);

export
[[nodiscard]] const image_nine_region& border(float radius = 8.f, float width = 2.f);

export
[[nodiscard]] const image_nine_region& thin_border(float radius = 8.f);

export
void bind_image_page(graphic::image_page& page) noexcept;

export
void clear() noexcept;
}

export
struct resource_manager{
private:
    mr::heap heap_{};
	image_resource_manager manager_;
	assets_page* round_square_page_{};
	graphic::image_page* round_square_atlas_page_{};
	std::unordered_map<resource_id, image_nine_region> round_square_cache_{};
	std::mutex round_square_mutex_{};

public:
    [[nodiscard]] inline explicit resource_manager(const mr::arena_id_t arena_id)
        : heap_(arena_id, 1), manager_(image_resource_allocator{heap_.get()}) {
		round_square_page_ = std::addressof(manager_.create_page(round_square::page_name()));
    }

    template <typename T>
        requires (std::constructible_from<std::string, T&&>)
    assets_page& create_page(T&& page_name) {
        return manager_.create_page(std::forward<T>(page_name));
    }

	[[nodiscard]] inline std::optional<constant_image_region_borrow> operator[](std::string_view page_name, std::string_view alias) const noexcept {
		return manager_[page_name, alias];
    }

	[[nodiscard]] inline std::optional<constant_image_region_borrow> operator[](std::string_view page_name, resource_id id) const noexcept {
		return manager_[page_name, id];
    }

    [[nodiscard]] inline std::optional<constant_image_region_borrow> operator[](std::string_view full_name) const noexcept {
        return manager_[full_name];
    }

	void bind_round_square_image_page(graphic::image_page& page) noexcept;

	void clear_round_square() noexcept;

	[[nodiscard]] const image_nine_region& get_round_square(round_square::key value);

	[[nodiscard]] const image_nine_region& get_round_square(resource_id id);
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

	[[nodiscard]] inline U(){}

	inline ~U(){}
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

