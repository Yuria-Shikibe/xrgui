export module mo_yanxi.behavior.tree.manager;

import std;
import mo_yanxi.heterogeneous;
import mo_yanxi.type_register;
export import mo_yanxi.behavior.tree;

namespace mo_yanxi::behavior {

export
template <typename Domain, typename T>
struct is_typed_node_ptr : std::false_type {};

template <typename Domain, typename Target>
struct is_typed_node_ptr<Domain, typed_node_ptr<Domain, Target>> : std::true_type {};

export
template <typename Domain, typename T>
inline constexpr bool is_typed_node_ptr_v = is_typed_node_ptr<Domain, std::remove_cvref_t<T>>::value;

export
template <typename Domain, typename T>
concept tree_node_composition = requires {
	typename node_trait<std::remove_cvref_t<T>>::target_type;
} && !std::is_void_v<typename node_trait<std::remove_cvref_t<T>>::target_type>
	&& !is_typed_node_ptr_v<Domain, T>;

export
template <typename Domain, typename Target>
[[nodiscard]] typed_node_ptr<Domain, Target> wrap_tree_ptr(tree_type_erased_ptr<Domain> ptr) noexcept {
	return typed_node_ptr<Domain, Target>{std::move(ptr)};
}

export
template <typename Domain, typename Target>
[[nodiscard]] tree_type_erased_ptr<Domain> unwrap_tree_ptr(typed_node_ptr<Domain, Target> ptr) noexcept {
	return std::move(ptr.ptr);
}

export
template <typename Domain>
struct tree_family {
	using erased_ptr = tree_type_erased_ptr<Domain>;

private:
	std::vector<erased_ptr> entries;

public:
	[[nodiscard]] tree_family() = default;

	explicit tree_family(erased_ptr&& default_value) {
		if(!default_value) {
			throw std::invalid_argument{"behavior tree family index [0] cannot be null"};
		}
		entries.push_back(std::move(default_value));
	}

	[[nodiscard]] erased_ptr get(std::size_t index) const noexcept {
		if(index < entries.size() && entries[index]) {
			return entries[index];
		}
		return entries.empty() ? erased_ptr{} : entries[0];
	}

	void set(std::size_t index, erased_ptr new_tree) {
		if(index == 0 && !new_tree) {
			throw std::invalid_argument{"behavior tree family index [0] cannot be null"};
		}
		if(index >= entries.size()) {
			entries.resize(index + 1);
		}
		entries[index] = std::move(new_tree);
	}

	[[nodiscard]] bool empty() const noexcept {
		return entries.empty();
	}

	[[nodiscard]] std::size_t size() const noexcept {
		return entries.size();
	}
};

export
template <typename Domain, tree_node_composition<Domain> Node>
[[nodiscard]] tree_family<Domain> make_tree_family(Node&& default_tree) {
	auto ptr = behavior::make_tree_node_ptr<Domain>(std::forward<Node>(default_tree));
	return tree_family<Domain>{behavior::unwrap_tree_ptr<Domain>(std::move(ptr))};
}

export
template <typename Domain, typename Target>
[[nodiscard]] tree_family<Domain> make_tree_family(typed_node_ptr<Domain, Target> default_tree) {
	return tree_family<Domain>{behavior::unwrap_tree_ptr<Domain>(std::move(default_tree))};
}

export
template <typename Domain, typename Family, typename MapAllocator>
struct tree_collection {
	using domain_type = Domain;
	using family_type = Family;
	using map_allocator_type = MapAllocator;
	using erased_ptr = tree_type_erased_ptr<Domain>;

	string_hash_map<family_type, map_allocator_type> map;
	family_type default_family;

	explicit tree_collection(family_type default_value, const map_allocator_type& alloc = {})
		: map(alloc),
		  default_family(std::move(default_value)) {
	}

	void set_default(erased_ptr new_default) {
		if(!new_default) {
			throw std::invalid_argument{"new default behavior tree index [0] cannot be null"};
		}
		default_family.set(0, std::move(new_default));
	}

	void set_default_family(family_type new_family) {
		default_family = std::move(new_family);
	}

	[[nodiscard]] const family_type& get_default() const noexcept {
		return default_family;
	}

	[[nodiscard]] family_type& get_default() noexcept {
		return default_family;
	}

	[[nodiscard]] erased_ptr resolve(std::string_view key, std::size_t index = 0) const noexcept {
		if(auto itr = map.find(key); itr != map.end()) {
			return itr->second.get(index);
		}
		return default_family.get(index);
	}
};

export
template <typename Domain, typename Collection, typename Target>
struct tree_slice {
	using collection_type = Collection;
	using family_type = typename collection_type::family_type;
	using erased_ptr = tree_type_erased_ptr<Domain>;

private:
	collection_type* collection;

	template <typename KeyType>
	struct slice_proxy {
		collection_type* m_collection;
		KeyType m_key;

		struct variant_proxy {
			const slice_proxy* parent;
			std::size_t index;

			[[nodiscard]] operator typed_node_ptr<Domain, Target>() const {
				if(auto itr = parent->m_collection->map.find(parent->m_key); itr != parent->m_collection->map.end()) {
					return behavior::wrap_tree_ptr<Domain, Target>(itr->second.get(index));
				}
				return {};
			}

			variant_proxy& operator=(typed_node_ptr<Domain, Target> value) {
				if(auto itr = parent->m_collection->map.find(parent->m_key); itr != parent->m_collection->map.end()) {
					itr->second.set(index, behavior::unwrap_tree_ptr<Domain>(std::move(value)));
				} else {
					if(index != 0) {
						throw std::invalid_argument{
							"must assign to index [0] before assigning to other indices for a new behavior tree key"};
					}
					parent->m_collection->map.insert_or_assign(
						parent->m_key,
						family_type{behavior::unwrap_tree_ptr<Domain>(std::move(value))});
				}
				return *this;
			}

			template <tree_node_composition<Domain> Node>
				requires std::derived_from<typename node_trait<std::remove_cvref_t<Node>>::target_type, Target>
			variant_proxy& operator=(Node&& value) {
				return this->operator=(behavior::make_tree_node_ptr<Domain>(std::forward<Node>(value)));
			}
		};

		[[nodiscard]] operator typed_node_ptr<Domain, Target>() const {
			return variant_proxy{this, 0};
		}

		slice_proxy& operator=(typed_node_ptr<Domain, Target> value) {
			variant_proxy{this, 0} = std::move(value);
			return *this;
		}

		template <tree_node_composition<Domain> Node>
			requires std::derived_from<typename node_trait<std::remove_cvref_t<Node>>::target_type, Target>
		slice_proxy& operator=(Node&& value) {
			variant_proxy{this, 0} = std::forward<Node>(value);
			return *this;
		}

		slice_proxy& operator=(family_type family) {
			m_collection->map.insert_or_assign(m_key, std::move(family));
			return *this;
		}

		[[nodiscard]] variant_proxy operator[](std::size_t index) const {
			return {this, index};
		}
	};

public:
	[[nodiscard]] explicit(false) tree_slice(collection_type& coll)
		: collection(&coll) {
	}

	[[nodiscard]] typed_node_ptr<Domain, Target> default_tree(std::size_t index = 0) const noexcept {
		return behavior::wrap_tree_ptr<Domain, Target>(collection->get_default().get(index));
	}

	[[nodiscard]] typed_node_ptr<Domain, Target> get_or_default(std::string_view key,
	                                                            std::size_t index = 0) const noexcept {
		return behavior::wrap_tree_ptr<Domain, Target>(collection->resolve(key, index));
	}

	void set_default(typed_node_ptr<Domain, Target> new_default) const {
		collection->set_default(behavior::unwrap_tree_ptr<Domain>(std::move(new_default)));
	}

	template <tree_node_composition<Domain> Node>
		requires std::derived_from<typename node_trait<std::remove_cvref_t<Node>>::target_type, Target>
	void set_default(Node&& new_default) const {
		this->set_default(behavior::make_tree_node_ptr<Domain>(std::forward<Node>(new_default)));
	}

	[[nodiscard]] typed_node_ptr<Domain, Target> at(std::string_view key, std::size_t index = 0) const {
		if(auto itr = collection->map.find(key); itr != collection->map.end()) {
			return behavior::wrap_tree_ptr<Domain, Target>(itr->second.get(index));
		}
		throw std::out_of_range{"behavior tree key not found"};
	}

	template <typename Key>
	[[nodiscard]] slice_proxy<std::decay_t<Key>> operator[](Key&& key) const {
		return {collection, std::forward<Key>(key)};
	}

	template <typename Key>
	auto insert_or_assign(Key&& key, typed_node_ptr<Domain, Target> tree) const {
		return collection->map.insert_or_assign(
			std::forward<Key>(key),
			family_type{behavior::unwrap_tree_ptr<Domain>(std::move(tree))});
	}

	template <typename Key>
	auto insert_or_assign(Key&& key, family_type family) const {
		return collection->map.insert_or_assign(std::forward<Key>(key), std::move(family));
	}

	template <typename Key, tree_node_composition<Domain> Node>
		requires std::derived_from<typename node_trait<std::remove_cvref_t<Node>>::target_type, Target>
	auto insert_or_assign(Key&& key, Node&& tree) const {
		return this->insert_or_assign(
			std::forward<Key>(key),
			behavior::make_tree_node_ptr<Domain>(std::forward<Node>(tree)));
	}

	[[nodiscard]] bool contains(std::string_view key) const noexcept {
		return collection->map.contains(key);
	}

	bool erase(std::string_view key) const noexcept {
		return collection->map.erase(key) > 0;
	}

	[[nodiscard]] bool empty() const noexcept {
		return collection->map.empty();
	}

	[[nodiscard]] std::size_t size() const noexcept {
		return collection->map.size();
	}
};

export
template <typename Domain, typename Collection, typename ManagerMapAllocator>
struct tree_manager {
	using domain_type = Domain;
	using collection_type = Collection;
	using family_type = typename collection_type::family_type;
	using manager_map_allocator_type = ManagerMapAllocator;

protected:
	std::unordered_map<type_identity_index, collection_type, std::hash<type_identity_index>, std::equal_to<>,
	                   manager_map_allocator_type> tree_map;

public:
	[[nodiscard]] explicit tree_manager(const manager_map_allocator_type& alloc = {})
		: tree_map(alloc) {
	}

	void reserve(std::size_t size) {
		tree_map.reserve(size);
	}

	template <typename Target>
	auto register_tree(typed_node_ptr<Domain, Target> default_tree) {
		if(!default_tree.ptr) {
			throw std::invalid_argument{"default behavior tree cannot be null"};
		}

		typename collection_type::map_allocator_type coll_alloc{tree_map.get_allocator()};
		return tree_map.insert_or_assign(
			mo_yanxi::unstable_type_identity_of<Target>(),
			collection_type{
				family_type{behavior::unwrap_tree_ptr<Domain>(std::move(default_tree))},
				coll_alloc
			});
	}

	template <tree_node_composition<Domain> Node>
	auto register_tree(Node&& default_tree) {
		using target_type = typename node_trait<std::remove_cvref_t<Node>>::target_type;
		return this->template register_tree<target_type>(
			behavior::make_tree_node_ptr<Domain>(std::forward<Node>(default_tree)));
	}

	template <typename Target>
	[[nodiscard]] std::optional<tree_slice<Domain, collection_type, Target>> get_slice() noexcept {
		if(auto itr = tree_map.find(mo_yanxi::unstable_type_identity_of<Target>()); itr != tree_map.end()) {
			return tree_slice<Domain, collection_type, Target>{itr->second};
		}
		return std::nullopt;
	}

	template <typename Target>
	[[nodiscard]] collection_type* get_collection() noexcept {
		if(auto itr = tree_map.find(mo_yanxi::unstable_type_identity_of<Target>()); itr != tree_map.end()) {
			return std::addressof(itr->second);
		}
		return nullptr;
	}

	template <typename Target>
	[[nodiscard]] const collection_type* get_collection() const noexcept {
		if(auto itr = tree_map.find(mo_yanxi::unstable_type_identity_of<Target>()); itr != tree_map.end()) {
			return std::addressof(itr->second);
		}
		return nullptr;
	}

	template <typename Target>
	[[nodiscard]] typed_node_ptr<Domain, Target> get_default(std::size_t index = 0) noexcept {
		return behavior::wrap_tree_ptr<Domain, Target>(
			tree_map.at(mo_yanxi::unstable_type_identity_of<Target>()).get_default().get(index));
	}

	template <typename Target>
	[[nodiscard]] bool contains() const noexcept {
		return tree_map.contains(mo_yanxi::unstable_type_identity_of<Target>());
	}

	template <typename Target>
	bool erase() noexcept {
		return tree_map.erase(mo_yanxi::unstable_type_identity_of<Target>()) > 0;
	}

	void clear() noexcept {
		tree_map.clear();
	}

	[[nodiscard]] bool empty() const noexcept {
		return tree_map.empty();
	}
};

}
