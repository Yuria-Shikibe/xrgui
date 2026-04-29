//

//

export module mo_yanxi.gui.style.manager;

import std;
import mo_yanxi.heterogeneous;
import mo_yanxi.type_register;
import mo_yanxi.gui.style.interface;
import mo_yanxi.gui.alloc;
export import mo_yanxi.gui.style.variant;

namespace mo_yanxi::gui::style {

export struct style_family {
    std::vector<referenced_ptr<style_drawer_base>> styles;


    explicit style_family(referenced_ptr<style_drawer_base>&& default_val) {
        if (!default_val) {
            throw std::invalid_argument{"style family index [0] cannot be null"};
        }
        styles.push_back(std::move(default_val));
    }


    [[nodiscard]] referenced_ptr<style_drawer_base> get(std::size_t index) const noexcept {
        if (index < styles.size() && styles[index]) {
            return styles[index];
        }
        return styles[0];
    }

	template <typename T> requires std::is_enum_v<T>
    [[nodiscard]] referenced_ptr<style_drawer_base> get(T variant) const noexcept {
        return this->get(std::to_underlying(variant));
    }

    void set(std::size_t index, referenced_ptr<style_drawer_base> new_style) {
        if (index == 0 && !new_style) {
            throw std::invalid_argument{"style family index [0] cannot be null"};
        }
        if (index >= styles.size()) {
            styles.resize(index + 1);
        }
        styles[index] = std::move(new_style);
    }

	template <typename T> requires std::is_enum_v<T>
    void set(T variant, referenced_ptr<style_drawer_base> new_style) {
        this->set(std::to_underlying(variant), std::move(new_style));
    }
};

using style_map_allocator = mr::unvs_allocator<std::pair<const std::string, style_family>>;

struct style_collection {
    std::unordered_map<std::string, style_family, transparent::string_hasher, transparent::string_equal_to, style_map_allocator> map;
    style_family default_family;

    explicit style_collection(
        style_family default_val,
        const style_map_allocator& alloc = {}
    ) : map(alloc), default_family(std::move(default_val)) {}

    void set_default(referenced_ptr<style_drawer_base> new_default) {
        if (!new_default) {
            throw std::invalid_argument{"new_default (index 0) cannot be null"};
        }
        default_family.set(0, std::move(new_default));
    }

    void set_default_family(style_family new_family) {
        default_family = std::move(new_family);
    }

    [[nodiscard]] const style_family& get_default() const noexcept {
        return default_family;
    }

    [[nodiscard]] style_family& get_default() noexcept {
        return default_family;
    }
};

export
template <typename T>
struct style_manager_slice {
private:
    style_collection* collection;


    template <typename KeyType>
    struct slice_proxy {
        style_collection* m_collection;
        KeyType m_key;

        struct variant_proxy {
            const slice_proxy* parent;
            std::size_t index;

            [[nodiscard]] operator referenced_ptr<T>() const {
                if (const auto itr = parent->m_collection->map.find(parent->m_key); itr != parent->m_collection->map.end()) {
                    return referenced_ptr<T>{static_cast<T*>(itr->second.get(index).get())};
                }
                return nullptr;
            }

            variant_proxy& operator=(referenced_ptr<T> value) {
                if (const auto itr = parent->m_collection->map.find(parent->m_key); itr != parent->m_collection->map.end()) {
                    itr->second.set(index, referenced_ptr<style_drawer_base>{static_cast<style_drawer_base*>(value.get())});
                } else {
                    if (index == 0) {
                        parent->m_collection->map.insert_or_assign(
                            parent->m_key,
                            style_family{referenced_ptr<style_drawer_base>{static_cast<style_drawer_base*>(value.get())}}
                        );
                    } else {
                        throw std::invalid_argument{"must assign to index [0] before assigning to other indices for a new key"};
                    }
                }
                return *this;
            }
        };


        [[nodiscard]] operator referenced_ptr<T>() const {
            return variant_proxy{this, 0};
        }


        slice_proxy& operator=(referenced_ptr<T> value) {
            variant_proxy{this, 0} = std::move(value);
            return *this;
        }

        slice_proxy& operator=(style_family family) {
            m_collection->map.insert_or_assign(m_key, std::move(family));
            return *this;
        }

        [[nodiscard]] variant_proxy operator[](std::size_t index) const {
            return {this, index};
        }

    	template <typename ITy> requires std::is_enum_v<ITy>
        [[nodiscard]] variant_proxy operator[](ITy variant) const {
            return {this, std::to_underlying(variant)};
        }
    };

public:
    [[nodiscard]] explicit(false) style_manager_slice(style_collection& coll)
        : collection(&coll) {}


    [[nodiscard]] referenced_ptr<T> default_style(std::size_t index = 0) const noexcept {
        return referenced_ptr<T>{static_cast<T*>(collection->get_default().get(index).get())};
    }

	template <typename ITy> requires std::is_enum_v<ITy>
    [[nodiscard]] referenced_ptr<T> default_style(ITy variant) const noexcept {
        return this->default_style(std::to_underlying(variant));
    }

    template <typename Key>
    [[nodiscard]] referenced_ptr<T> get_or_default(Key&& key, std::size_t index = 0) const noexcept {
    	if (const auto itr = collection->map.find(key); itr != collection->map.end()) {
    		return referenced_ptr<T>{static_cast<T*>(itr->second.get(index).get())};
    	}
    	return default_style(index);
    }

    template <typename Key, typename ITy> requires std::is_enum_v<ITy>
    [[nodiscard]] referenced_ptr<T> get_or_default(Key&& key, ITy variant) const noexcept {
        return this->get_or_default(std::forward<Key>(key), std::to_underlying(variant));
    }

    void set_default(referenced_ptr<T> new_default) const {
        collection->set_default(referenced_ptr<style_drawer_base>{static_cast<style_drawer_base*>(new_default.get())});
    }

    template <typename Key>
    [[nodiscard]] referenced_ptr<T> at(Key&& key, std::size_t index = 0) const {
        if (const auto itr = collection->map.find(key); itr != collection->map.end()) {
            return referenced_ptr<T>{static_cast<T*>(itr->second.get(index).get())};
        }
        throw std::out_of_range{"key not found"};
    }

    template <typename Key, typename ITy> requires std::is_enum_v<ITy>
    [[nodiscard]] referenced_ptr<T> at(Key&& key, ITy variant) const {
        return at(std::forward<Key>(key), std::to_underlying(variant));
    }

    template <typename Key>
    [[nodiscard]] slice_proxy<std::decay_t<Key>> operator[](Key&& key) const {
        return {collection, std::forward<Key>(key)};
    }

    template <typename Key>
    auto insert_or_assign(Key&& key, referenced_ptr<T> style) const {
        return collection->map.insert_or_assign(
            std::forward<Key>(key),
            style_family{referenced_ptr<style_drawer_base>{static_cast<style_drawer_base*>(style.get())}}
        );
    }

    template <typename Key>
    auto insert_or_assign(Key&& key, style_family family) const {
        return collection->map.insert_or_assign(std::forward<Key>(key), std::move(family));
    }

    template <typename Key>
    [[nodiscard]] bool contains(Key&& key) const noexcept {
        return collection->map.contains(std::forward<Key>(key));
    }

    template <typename Key>
    bool erase(Key&& key) const noexcept {
        return collection->map.erase(std::forward<Key>(key)) > 0;
    }

    [[nodiscard]] bool empty() const noexcept {
        return collection->map.empty();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return collection->map.size();
    }
};

using manager_map_allocator = mr::unvs_allocator<std::pair<const type_identity_index, style_collection>>;

export
struct style_manager {
private:
    std::unordered_map<type_identity_index, style_collection, std::hash<type_identity_index>, std::equal_to<>, manager_map_allocator> style_map;

public:
    [[nodiscard]] explicit style_manager(
        const manager_map_allocator& alloc = {}
    ) : style_map(alloc) {}

	void reserve(std::size_t size){
		style_map.reserve(size);
	}

    template <typename T_Explicit>
    auto register_style(referenced_ptr<std::type_identity_t<T_Explicit>> default_style) {
        if (!default_style) {
            throw std::invalid_argument{"default style cannot be null"};
        }

        style_map_allocator coll_alloc{style_map.get_allocator()};
        return style_map.insert_or_assign(
            mo_yanxi::unstable_type_identity_of<T_Explicit>(),
            style_collection{
                style_family{referenced_ptr<style_drawer_base>{std::move(default_style)}},
                coll_alloc
            }
        );
    }

    template <typename T>
    [[nodiscard]] std::optional<style_manager_slice<T>> get_slice() noexcept {
        if (auto itr = style_map.find(mo_yanxi::unstable_type_identity_of<T>()); itr != style_map.end()) {
            return style_manager_slice<T>{itr->second};
        }
        return std::nullopt;
    }

    template <typename T>
    [[nodiscard]] referenced_ptr<T> get_default(std::size_t index = 0) noexcept {
    	auto* gptr = style_map.at(mo_yanxi::unstable_type_identity_of<T>()).get_default().get(index).get();
    	return referenced_ptr<T>{static_cast<T*>(gptr)};
    }

    template <typename T>
    [[nodiscard]] referenced_ptr<T> get_default(family_variant variant) noexcept {
        return get_default<T>(static_cast<std::size_t>(variant));
    }

    template <typename T>
    [[nodiscard]] bool contains() const noexcept {
        return style_map.contains(mo_yanxi::unstable_type_identity_of<T>());
    }

    template <typename T>
    bool erase() noexcept {
        return style_map.erase(mo_yanxi::unstable_type_identity_of<T>()) > 0;
    }

    void clear() noexcept {
        style_map.clear();
    }

    [[nodiscard]] bool empty() const noexcept {
        return style_map.empty();
    }
};

}
