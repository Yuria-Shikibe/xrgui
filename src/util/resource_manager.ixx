module;

#include <gtl/phmap.hpp>

export module mo_yanxi.resource_manager;

import mo_yanxi.algo;
import std;

namespace mo_yanxi::resource{

export using resource_id = std::size_t;

export
struct duplicated_error : std::invalid_argument{
	[[nodiscard]] explicit duplicated_error(const std::string& message)
		: invalid_argument(message){
	}

	[[nodiscard]] explicit duplicated_error(const char* message)
		: invalid_argument(message){
	}
};

export
struct string_hash{
	using is_transparent = void;

	[[nodiscard]] std::size_t operator()(std::string_view value) const noexcept{
		return std::hash<std::string_view>{}(value);
	}
};

export
struct string_equal{
	using is_transparent = void;

	[[nodiscard]] constexpr bool operator()(std::string_view lhs, std::string_view rhs) const noexcept{
		return lhs == rhs;
	}
};

template <typename Allocator, typename T>
using rebound_allocator = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;

export
template <std::copyable Handle, std::copyable Allocator = std::allocator<std::byte>>
struct basic_assets_page{
	using handle_type = Handle;
	using allocator_type = Allocator;
	using resource_id_type = resource_id;
	using string_vector_allocator = rebound_allocator<allocator_type, std::string>;

	using alias_map = gtl::parallel_flat_hash_map<
		std::string, resource_id,
		string_hash, string_equal,
		rebound_allocator<allocator_type, std::pair<const std::string, resource_id>>>;

	struct entry{
		handle_type handle;
		std::vector<std::string, string_vector_allocator> references_to_this;

		[[nodiscard]] explicit entry(
			const string_vector_allocator& allocator,
			const handle_type& resource_handle)
			: handle(resource_handle), references_to_this(allocator){
		}
	};

private:
	constexpr static resource_id initial_free_id = std::rotr(resource_id{1}, 1);
	mutable std::atomic<resource_id> last_id_{initial_free_id};

	using resource_map = gtl::parallel_flat_hash_map<
		resource_id, entry,
		std::hash<resource_id>, std::equal_to<resource_id>,
		rebound_allocator<allocator_type, std::pair<const resource_id, entry>>>;

	resource_map resources_;
	alias_map alias_map_;

	[[nodiscard]] string_vector_allocator string_allocator_() const{
		return string_vector_allocator{resources_.get_allocator()};
	}

public:
	[[nodiscard]] resource_id acquire_free_id() const noexcept{
		return last_id_.fetch_add(1, std::memory_order_relaxed);
	}

	[[nodiscard]] explicit basic_assets_page(const allocator_type& allocator = allocator_type{})
		: resources_(typename resource_map::allocator_type{allocator}),
		  alias_map_(typename alias_map::allocator_type{allocator}){
	}

	handle_type insert(resource_id id, const handle_type& handle){
		auto [itr, inserted] = resources_.try_emplace(
			id,
			string_allocator_(),
			handle
		);
		if(!inserted){
			throw duplicated_error{"Resource ID Duplicated"};
		}
		return itr->second.handle;
	}

	template <typename T>
		requires (std::is_scoped_enum_v<T> && std::convertible_to<std::underlying_type_t<T>, resource_id>)
	handle_type insert(T id, const handle_type& handle){
		return this->insert(std::to_underlying(id), handle);
	}

	bool erase(resource_id id) noexcept{
		resources_.if_contains(id, [&](const typename resource_map::value_type& entry){
			for(const auto& alias : entry.second.references_to_this){
				alias_map_.erase(alias);
			}
		});
		return resources_.erase(id) > 0;
	}

	template <typename T>
		requires (std::constructible_from<std::string, T&&>)
	void add_alias(resource_id id, T&& string_prov){
		std::string new_alias(std::forward<T>(string_prov));
		auto [itr, inserted] = alias_map_.try_emplace(new_alias, id);
		if(!inserted){
			throw duplicated_error{"Resource Alias Duplicated"};
		}

		bool id_exists = resources_.modify_if(id, [&](typename resource_map::value_type& kv){
			kv.second.references_to_this.push_back(new_alias);
		});

		if(!id_exists){
			alias_map_.erase(new_alias);
			throw std::invalid_argument{"Resource ID does not exist"};
		}
	}

	bool erase_alias(std::string_view alias) noexcept{
		resource_id target_id;
		bool found = false;

		alias_map_.if_contains(alias, [&](const typename alias_map::value_type& kv){
			target_id = kv.second;
			found = true;
		});

		if(!found){
			return false;
		}
		alias_map_.erase(alias);

		resources_.modify_if(target_id, [&](typename resource_map::value_type& kv){
			algo::erase_unique_unstable(kv.second.references_to_this, std::string(alias));
		});

		return true;
	}

	std::optional<resource_id> get_relevant_id(std::string_view alias) const noexcept{
		std::optional<resource_id> result;
		alias_map_.if_contains(alias, [&](const typename alias_map::value_type& kv){
			result = kv.second;
		});
		return result;
	}

	[[nodiscard]] std::optional<handle_type> operator[](resource_id id) const noexcept{
		std::optional<handle_type> result;
		resources_.if_contains(id, [&](const typename resource_map::value_type& kv){
			result = kv.second.handle;
		});
		return result;
	}

	[[nodiscard]] std::optional<handle_type> operator[](std::string_view alias) const noexcept{
		if(auto id_opt = get_relevant_id(alias)){
			return this->operator[](*id_opt);
		}
		return std::nullopt;
	}

	template <typename T>
		requires (std::is_scoped_enum_v<T> && std::convertible_to<std::underlying_type_t<T>, resource_id>)
	[[nodiscard]] std::optional<handle_type> operator[](T id) const noexcept{
		return this->operator[](std::to_underlying(id));
	}

	handle_type insert_or_assign(resource_id id, const handle_type& handle){
		bool updated = resources_.modify_if(id, [&](typename resource_map::value_type& kv){
			kv.second.handle = handle;
		});

		if(!updated){
			auto [itr, inserted] = resources_.try_emplace(
				id,
				string_allocator_(),
				handle
			);

			if(!inserted){
				resources_.modify_if(id, [&](typename resource_map::value_type& kv){
					kv.second.handle = handle;
				});
			}
		}

		return handle;
	}
};

export
template <std::copyable Handle, std::copyable Allocator = std::allocator<std::byte>>
struct basic_resource_manager{
	using handle_type = Handle;
	using allocator_type = Allocator;
	using page_type = basic_assets_page<handle_type, allocator_type>;

private:
	using pages_map_type = gtl::parallel_node_hash_map<
		std::string, page_type,
		string_hash, string_equal,
		rebound_allocator<allocator_type, std::pair<const std::string, page_type>>>;

	pages_map_type pages_;

	[[nodiscard]] allocator_type allocator_() const{
		return allocator_type{pages_.get_allocator()};
	}

public:
	[[nodiscard]] explicit basic_resource_manager(const allocator_type& allocator = allocator_type{})
		: pages_(typename pages_map_type::allocator_type{allocator}){
	}

	template <typename T>
		requires (std::constructible_from<std::string, T&&>)
	page_type& create_page(T&& page_name){
		std::string name{std::forward<T>(page_name)};
		if(std::string_view{name}.contains(':')){
			throw std::invalid_argument{"page_name should not contain ':'"};
		}

		auto [itr, inserted] = pages_.try_emplace(
			std::move(name),
			allocator_()
		);

		if(!inserted){
			throw duplicated_error{"Resource Page Duplicated"};
		}

		return itr->second;
	}

	[[nodiscard]] std::optional<handle_type> operator[](
		std::string_view page_name,
		std::string_view alias) const noexcept{
		std::optional<handle_type> result;
		pages_.if_contains(page_name, [&](const typename pages_map_type::value_type& kv){
			result = kv.second[alias];
		});
		return result;
	}

	[[nodiscard]] std::optional<handle_type> operator[](
		std::string_view page_name,
		resource_id id) const noexcept{
		std::optional<handle_type> result;
		pages_.if_contains(page_name, [&](const typename pages_map_type::value_type& kv){
			result = kv.second[id];
		});
		return result;
	}

	[[nodiscard]] std::optional<handle_type> operator[](std::string_view full_name) const noexcept{
		auto pos = full_name.find(':');
		if(pos == std::string_view::npos){
			return std::nullopt;
		}
		return (*this)[full_name.substr(0, pos), full_name.substr(pos + 1)];
	}
};

}
