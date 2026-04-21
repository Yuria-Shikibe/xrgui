module;


export module mo_yanxi.gui.assets.manager;

import mo_yanxi.gui.alloc;
export import mo_yanxi.gui.image_regions;
import mo_yanxi.graphic.image_region;
import mo_yanxi.graphic.image_region.borrow;
import mo_yanxi.algo;

import <gtl/phmap.hpp>;

import std;

namespace mo_yanxi::gui::assets {

export using resource_id = std::size_t;

struct duplicated_error : std::invalid_argument {
    [[nodiscard]] explicit duplicated_error(const std::string& _Message)
        : invalid_argument(_Message) {}

    [[nodiscard]] explicit duplicated_error(const char* _Message)
        : invalid_argument(_Message) {}
};

struct string_hash {
    using is_transparent = void;
    static std::size_t operator()(std::string_view sv) noexcept {
        return std::hash<std::string_view>{}(sv);
    }
};

struct string_equal {
    using is_transparent = void;
    static constexpr bool operator()(std::string_view lhs, std::string_view rhs) noexcept {
        return lhs == rhs;
    }
};

struct assets_page {
    // 【优化点 1】全部使用极致性能的 flat_hash_map
    using alias_map = gtl::parallel_flat_hash_map<
        std::string, resource_id,
        string_hash, string_equal,
        mr::heap_allocator<std::pair<const std::string, resource_id>>>;

    struct image_entry {
        constant_image_region_borrow image_borrow;
        mr::heap_vector<std::string> references_to_this;

        // image_entry 现在支持移动语义，完美适配 flat_hash_map 的 rehash 扩容机制
        [[nodiscard]] explicit image_entry(
            const mr::heap_allocator<std::string>& allocator, const constant_image_region_borrow& image_region)
            : image_borrow(image_region), references_to_this(allocator) {}
    };

private:
	constexpr static auto initial_free_id = std::rotr(1uz, 1);
    mutable std::atomic<resource_id> last_id_{std::rotr(1uz, 1)};

    // 【优化点 2】抛弃 node_hash_map，彻底拥抱内存连续的 flat_hash_map
    using region_map = gtl::parallel_flat_hash_map<
        resource_id, image_entry,
        std::hash<resource_id>, std::equal_to<resource_id>,
        mr::heap_allocator<std::pair<const resource_id, image_entry>>>;

    region_map image_regions_;
    alias_map alias_map_;

public:
	[[nodiscard]] resource_id acquire_free_id() const noexcept{
		return last_id_.fetch_add(1, std::memory_order_relaxed);
	}

    [[nodiscard]] explicit assets_page(
        const mr::heap_allocator<assets_page>& allocator) :
        image_regions_(allocator),
        alias_map_(allocator) {}

    // 【优化点 3】按值返回 image_region_borrow，摆脱对容器内存地址稳定性的依赖
    constant_image_region_borrow insert(resource_id id, const constant_image_region_borrow& region) {
        auto [itr, suc] = image_regions_.try_emplace(
            id,
            mr::heap_allocator<std::string>{image_regions_.get_allocator()},
            region
        );
        if (!suc) throw duplicated_error{"Image ID Duplicated"};
        return itr->second.image_borrow;
    }

    template <typename T>
        requires (std::is_scoped_enum_v<T> && std::convertible_to<std::underlying_type_t<T>, resource_id>)
    constant_image_region_borrow insert(T id, const constant_image_region_borrow& region) {
        return this->insert(std::to_underlying(id), region);
    }

    bool erase(resource_id id) noexcept {
        image_regions_.if_contains(id, [&](const region_map::value_type& entry) {
            for (const auto& alias : entry.second.references_to_this) {
                alias_map_.erase(alias);
            }
        });
        return image_regions_.erase(id) > 0;
    }

	template <typename T>
		requires (std::constructible_from<std::string, T&&>)
	void add_alias(resource_id id, T&& string_prov) {
		std::string new_alias(std::forward<T>(string_prov));
		auto [itr, suc] = alias_map_.try_emplace(new_alias, id);
		if (!suc) throw duplicated_error{"Image Alias Duplicated"};

		// kv 类型为: std::pair<const resource_id, image_entry>&
		bool id_exists = image_regions_.modify_if(id, [&](region_map::value_type& kv) {
			kv.second.references_to_this.push_back(new_alias);
		});

		if (!id_exists) {
			alias_map_.erase(new_alias);
			throw std::invalid_argument{"Image ID does not exist"};
		}
	}

	bool erase_alias(std::string_view alias) noexcept {
		resource_id target_id;
		bool found = false;

		// kv 类型为: const std::pair<const std::string, resource_id>&
		alias_map_.if_contains(alias, [&](const alias_map::value_type& kv) {
			target_id = kv.second;
			found = true;
		});

		if (!found) return false;
		alias_map_.erase(alias);

		// kv 类型为: std::pair<const resource_id, image_entry>&
		image_regions_.modify_if(target_id, [&](region_map::value_type& kv) {
			algo::erase_unique_unstable(kv.second.references_to_this, std::string(alias));
		});

		return true;
	}

	std::optional<resource_id> get_relevant_id(std::string_view alias) const noexcept {
		std::optional<resource_id> result;
		alias_map_.if_contains(alias, [&](const alias_map::value_type& kv) {
			result = kv.second;
		});
		return result;
	}

	[[nodiscard]] std::optional<constant_image_region_borrow> operator[](resource_id id) const noexcept {
		std::optional<constant_image_region_borrow> result;
		image_regions_.if_contains(id, [&](const region_map::value_type& kv) {
			result = kv.second.image_borrow;
		});
		return result;
	}

	[[nodiscard]] std::optional<constant_image_region_borrow> operator[](std::string_view alias) const noexcept {
		if (auto id_opt = get_relevant_id(alias)) {
			return this->operator[](id_opt.value());
		}
		return std::nullopt;
	}

    template <typename T>
        requires (std::is_scoped_enum_v<T> && std::convertible_to<std::underlying_type_t<T>, resource_id>)
    [[nodiscard]] std::optional<constant_image_region_borrow> operator[](T id) const noexcept {
        return this->operator[](std::to_underlying(id));
    }

	constant_image_region_borrow insert_or_assign(resource_id id, const constant_image_region_borrow& region) {
		bool updated = image_regions_.modify_if(id, [&](region_map::value_type& kv) {
			kv.second.image_borrow = region;
		});

		if (!updated) {
			auto [itr, inserted] = image_regions_.try_emplace(
				id,
				mr::heap_allocator<std::string>{image_regions_.get_allocator()},
				region
			);

			if (!inserted) {
				image_regions_.modify_if(id, [&](region_map::value_type& kv) {
					kv.second.image_borrow = region;
				});
			}
		}

		return region;
	}
};

export
struct resource_manager {
private:
    mr::heap heap_{};

    // 因为 assets_page 含有 atomic 和 内部锁，是不可移动对象(Immovable)。
    // 所以在管理器这一层，继续使用 node_hash_map 是最佳实践，无需引入额外指针。
    using pages_map_type = gtl::parallel_node_hash_map<
        std::string, assets_page,
        string_hash, string_equal,
        mr::heap_allocator<std::pair<const std::string, assets_page>>>;

    pages_map_type pages_{};

public:
    [[nodiscard]] explicit resource_manager(const mr::arena_id_t arena_id)
        : heap_(arena_id, 1), pages_(mr::heap_allocator<std::pair<const std::string, assets_page>>{heap_.get()}) {
    }

    template <typename T>
        requires (std::constructible_from<std::string, T&&> && std::constructible_from<std::string_view, const T&>)
    assets_page& create_page(T&& page_name) {
        std::string_view str{page_name};
        if (str.contains(':')) {
            throw std::invalid_argument{"page_name should not contains ':'"};
        }

        auto [itr, suc] = pages_.try_emplace(
            std::forward<T>(page_name),
            mr::heap_allocator<assets_page>{pages_.get_allocator()}
        );

        if (!suc) {
            throw duplicated_error{"Image Page Duplicated"};
        }

        return itr->second;
    }

	[[nodiscard]] std::optional<constant_image_region_borrow> operator[](std::string_view page_name, std::string_view alias) const noexcept {
		std::optional<constant_image_region_borrow> result;
		// kv 类型为: const std::pair<const std::string, assets_page>&
		pages_.if_contains(page_name, [&](const pages_map_type::value_type& kv) {
			result = kv.second[alias];
		});
		return result;
    }

	[[nodiscard]] std::optional<constant_image_region_borrow> operator[](std::string_view page_name, resource_id id) const noexcept {
		std::optional<constant_image_region_borrow> result;
		pages_.if_contains(page_name, [&](const pages_map_type::value_type& kv) {
			result = kv.second[id];
		});
		return result;
    }

    [[nodiscard]] std::optional<constant_image_region_borrow> operator[](std::string_view full_name) const noexcept {
        auto pos = full_name.find(':');
        if (pos == std::string_view::npos) return std::nullopt;
        return (*this)[full_name.substr(0, pos), full_name.substr(pos + 1)];
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

