export module mo_yanxi.gui.style.tree.manager;

import std;
import mo_yanxi.heterogeneous;
import mo_yanxi.type_register;
import mo_yanxi.gui.alloc;
export import mo_yanxi.gui.style.tree;
export import mo_yanxi.gui.style.variant;

namespace mo_yanxi::gui::style {

namespace detail {

template <typename T>
struct is_target_known_node_ptr : std::false_type {};

template <typename T>
struct is_target_known_node_ptr<target_known_node_ptr<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_target_known_node_ptr_v = is_target_known_node_ptr<std::remove_cvref_t<T>>::value;

template <typename T>
concept style_tree_node_composition = requires {
	typename node_trait<std::remove_cvref_t<T>>::target_type;
} && !std::is_void_v<typename node_trait<std::remove_cvref_t<T>>::target_type>
	&& !is_target_known_node_ptr_v<T>;

template <typename Target>
[[nodiscard]] target_known_node_ptr<Target> wrap_tree_ptr(style_tree_type_erased_ptr ptr) noexcept {
	return target_known_node_ptr<Target>{std::move(ptr)};
}

template <typename Target>
[[nodiscard]] style_tree_type_erased_ptr unwrap_tree_ptr(target_known_node_ptr<Target> ptr) noexcept {
	return std::move(ptr.ptr);
}

template <typename T>
using named_style_map_allocator = mr::unvs_allocator<std::pair<const std::string, T>>;

}

export
struct style_tree_family {
private:
	std::vector<style_tree_type_erased_ptr> styles;

public:
	[[nodiscard]] style_tree_family() = default;

	explicit style_tree_family(style_tree_type_erased_ptr&& default_val) {
		if(!default_val) {
			throw std::invalid_argument{"style tree family index [0] cannot be null"};
		}
		styles.push_back(std::move(default_val));
	}

	[[nodiscard]] style_tree_type_erased_ptr get(std::size_t index) const noexcept {
		if(index < styles.size() && styles[index]) {
			return styles[index];
		}
		return styles.empty() ? style_tree_type_erased_ptr{} : styles[0];
	}

	[[nodiscard]] style_tree_type_erased_ptr get(family_variant variant) const noexcept {
		return get(std::to_underlying(variant));
	}

	void set(std::size_t index, style_tree_type_erased_ptr new_style) {
		if(index == 0 && !new_style) {
			throw std::invalid_argument{"style tree family index [0] cannot be null"};
		}
		if(index >= styles.size()) {
			styles.resize(index + 1);
		}
		styles[index] = std::move(new_style);
	}

	void set(family_variant variant, style_tree_type_erased_ptr new_style) {
		set(std::to_underlying(variant), std::move(new_style));
	}

	template <typename E, typename Ty>
		requires (std::is_enum_v<E> && std::convertible_to<std::underlying_type_t<E>, std::size_t>)
	void set(E variant, target_known_node_ptr<Ty>&& new_style) {
		this->set(std::to_underlying(variant), detail::unwrap_tree_ptr(std::move(new_style)));
	}

	[[nodiscard]] bool empty() const noexcept {
		return styles.empty();
	}

	[[nodiscard]] std::size_t size() const noexcept {
		return styles.size();
	}
};

export template <detail::style_tree_node_composition Node>
[[nodiscard]] style_tree_family make_style_tree_family(Node&& default_style) {
	auto ptr = style::make_tree_node_ptr(std::forward<Node>(default_style));
	return style_tree_family{detail::unwrap_tree_ptr(std::move(ptr))};
}

export template <typename Target>
[[nodiscard]] style_tree_family make_style_tree_family(target_known_node_ptr<Target> default_style) {
	return style_tree_family{detail::unwrap_tree_ptr(std::move(default_style))};
}

using style_tree_map_allocator = detail::named_style_map_allocator<style_tree_family>;

export struct style_tree_collection {
	string_hash_map<style_tree_family, style_tree_map_allocator> map;
	style_tree_family default_family;

	explicit style_tree_collection(style_tree_family default_val, const style_tree_map_allocator& alloc = {})
		: map(alloc),
		  default_family(std::move(default_val)) {
	}

	void set_default(style_tree_type_erased_ptr new_default) {
		if(!new_default) {
			throw std::invalid_argument{"new default style tree (index [0]) cannot be null"};
		}
		default_family.set(0, std::move(new_default));
	}

	void set_default_family(style_tree_family new_family) {
		default_family = std::move(new_family);
	}

	[[nodiscard]] const style_tree_family& get_default() const noexcept {
		return default_family;
	}

	[[nodiscard]] style_tree_family& get_default() noexcept {
		return default_family;
	}

	[[nodiscard]] style_tree_type_erased_ptr resolve(std::string_view key, std::size_t index = 0) const noexcept {
		if(auto itr = map.find(key); itr != map.end()) {
			return itr->second.get(index);
		}
		return default_family.get(index);
	}

	[[nodiscard]] style_tree_type_erased_ptr resolve(std::string_view key, family_variant variant) const noexcept {
		return resolve(key, std::to_underlying(variant));
	}
};

export template <typename Target>
struct style_tree_slice {
private:
	style_tree_collection* collection;

	template <typename KeyType>
	struct slice_proxy {
		style_tree_collection* m_collection;
		KeyType m_key;

		struct variant_proxy {
			const slice_proxy* parent;
			std::size_t index;

			[[nodiscard]] operator target_known_node_ptr<Target>() const {
				if(auto itr = parent->m_collection->map.find(parent->m_key); itr != parent->m_collection->map.end()) {
					return detail::wrap_tree_ptr<Target>(itr->second.get(index));
				}
				return {};
			}

			variant_proxy& operator=(target_known_node_ptr<Target> value) {
				if(auto itr = parent->m_collection->map.find(parent->m_key); itr != parent->m_collection->map.end()) {
					itr->second.set(index, detail::unwrap_tree_ptr(std::move(value)));
				} else {
					if(index == 0) {
						parent->m_collection->map.insert_or_assign(parent->m_key,
						                                        style_tree_family{detail::unwrap_tree_ptr(std::move(value))});
					} else {
						throw std::invalid_argument{
							"must assign to index [0] before assigning to other indices for a new style tree key"};
					}
				}
				return *this;
			}

			template <detail::style_tree_node_composition Node>
				requires std::derived_from<typename node_trait<std::remove_cvref_t<Node>>::target_type, Target>
			variant_proxy& operator=(Node&& value) {
				return (*this) = style::make_tree_node_ptr(std::forward<Node>(value));
			}
		};

		[[nodiscard]] operator target_known_node_ptr<Target>() const {
			return variant_proxy{this, 0};
		}

		slice_proxy& operator=(target_known_node_ptr<Target> value) {
			variant_proxy{this, 0} = std::move(value);
			return *this;
		}

		template <detail::style_tree_node_composition Node>
			requires std::derived_from<typename node_trait<std::remove_cvref_t<Node>>::target_type, Target>
		slice_proxy& operator=(Node&& value) {
			variant_proxy{this, 0} = std::forward<Node>(value);
			return *this;
		}

		slice_proxy& operator=(style_tree_family family) {
			m_collection->map.insert_or_assign(m_key, std::move(family));
			return *this;
		}

		[[nodiscard]] variant_proxy operator[](std::size_t index) const {
			return {this, index};
		}

		[[nodiscard]] variant_proxy operator[](family_variant variant) const {
			return {this, std::to_underlying(variant)};
		}
	};

public:
	[[nodiscard]] explicit(false) style_tree_slice(style_tree_collection& coll)
		: collection(&coll) {
	}

	[[nodiscard]] target_known_node_ptr<Target> default_style(std::size_t index = 0) const noexcept {
		return detail::wrap_tree_ptr<Target>(collection->get_default().get(index));
	}

	[[nodiscard]] target_known_node_ptr<Target> default_style(family_variant variant) const noexcept {
		return default_style(std::to_underlying(variant));
	}

	[[nodiscard]] target_known_node_ptr<Target> get_or_default(std::string_view key, std::size_t index = 0) const noexcept {
		return detail::wrap_tree_ptr<Target>(collection->resolve(key, index));
	}

	[[nodiscard]] target_known_node_ptr<Target> get_or_default(std::string_view key, family_variant variant) const noexcept {
		return get_or_default(key, std::to_underlying(variant));
	}

	void set_default(target_known_node_ptr<Target> new_default) const {
		collection->set_default(detail::unwrap_tree_ptr(std::move(new_default)));
	}

	template <detail::style_tree_node_composition Node>
		requires std::derived_from<typename node_trait<std::remove_cvref_t<Node>>::target_type, Target>
	void set_default(Node&& new_default) const {
		this->set_default(style::make_tree_node_ptr(std::forward<Node>(new_default)));
	}

	[[nodiscard]] target_known_node_ptr<Target> at(std::string_view key, std::size_t index = 0) const {
		if(auto itr = collection->map.find(key); itr != collection->map.end()) {
			return detail::wrap_tree_ptr<Target>(itr->second.get(index));
		}
		throw std::out_of_range{"style tree key not found"};
	}

	[[nodiscard]] target_known_node_ptr<Target> at(std::string_view key, family_variant variant) const {
		return at(key, std::to_underlying(variant));
	}

	template <typename Key>
	[[nodiscard]] slice_proxy<std::decay_t<Key>> operator[](Key&& key) const {
		return {collection, std::forward<Key>(key)};
	}

	template <typename Key>
	auto insert_or_assign(Key&& key, target_known_node_ptr<Target> style) const {
		return collection->map.insert_or_assign(std::forward<Key>(key),
		                                       style_tree_family{detail::unwrap_tree_ptr(std::move(style))});
	}

	template <typename Key>
	auto insert_or_assign(Key&& key, style_tree_family family) const {
		return collection->map.insert_or_assign(std::forward<Key>(key), std::move(family));
	}

	template <typename Key, detail::style_tree_node_composition Node>
		requires std::derived_from<typename node_trait<std::remove_cvref_t<Node>>::target_type, Target>
	auto insert_or_assign(Key&& key, Node&& style) const {
		return this->insert_or_assign(std::forward<Key>(key), style::make_tree_node_ptr(std::forward<Node>(style)));
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

using style_tree_manager_map_allocator = mr::unvs_allocator<std::pair<const type_identity_index, style_tree_collection>>;

export struct style_tree_manager {
private:
	std::unordered_map<type_identity_index, style_tree_collection, std::hash<type_identity_index>, std::equal_to<>,
	                   style_tree_manager_map_allocator> style_map;

public:
	[[nodiscard]] explicit style_tree_manager(const style_tree_manager_map_allocator& alloc = {})
		: style_map(alloc) {
	}

	void reserve(std::size_t size) {
		style_map.reserve(size);
	}

	template <typename Target>
	auto register_style(target_known_node_ptr<Target> default_style) {
		if(!default_style.ptr) {
			throw std::invalid_argument{"default style tree cannot be null"};
		}

		style_tree_map_allocator coll_alloc{style_map.get_allocator()};
		return style_map.insert_or_assign(mo_yanxi::unstable_type_identity_of<Target>(),
		                                 style_tree_collection{style_tree_family{detail::unwrap_tree_ptr(std::move(default_style))},
		                                                       coll_alloc});
	}

	template <detail::style_tree_node_composition Node>
	auto register_style(Node&& default_style) {
		using target_type = typename node_trait<std::remove_cvref_t<Node>>::target_type;
		return register_style<target_type>(make_tree_node_ptr(std::forward<Node>(default_style)));
	}

	template <typename Target>
	[[nodiscard]] std::optional<style_tree_slice<Target>> get_slice() noexcept {
		if(auto itr = style_map.find(mo_yanxi::unstable_type_identity_of<Target>()); itr != style_map.end()) {
			return style_tree_slice<Target>{itr->second};
		}
		return std::nullopt;
	}

	template <typename Target>
	[[nodiscard]] target_known_node_ptr<Target> get_default(std::size_t index = 0) noexcept {
		return detail::wrap_tree_ptr<Target>(style_map.at(mo_yanxi::unstable_type_identity_of<Target>()).get_default().get(index));
	}

	template <typename Target>
	[[nodiscard]] target_known_node_ptr<Target> get_default(family_variant variant) noexcept {
		return get_default<Target>(std::to_underlying(variant));
	}

	template <typename Target>
	[[nodiscard]] bool contains() const noexcept {
		return style_map.contains(mo_yanxi::unstable_type_identity_of<Target>());
	}

	template <typename Target>
	bool erase() noexcept {
		return style_map.erase(mo_yanxi::unstable_type_identity_of<Target>()) > 0;
	}

	void clear() noexcept {
		style_map.clear();
	}

	[[nodiscard]] bool empty() const noexcept {
		return style_map.empty();
	}
};

}
