//
// Created by Matrix on 2026/3/7.
//

export module mo_yanxi.gui.style.manager;

import std;
import mo_yanxi.heterogeneous;
import mo_yanxi.type_register;
import mo_yanxi.gui.style.interface;
import mo_yanxi.gui.alloc;

namespace mo_yanxi::gui::style {

enum struct family_variant{
	general,
	general_static,
	base_only,
	edge_only,

	emphasize,
	accepted,
	warning,
	invalid,

};

using style_map_allocator = mr::heap_allocator<std::pair<const std::string, referenced_ptr<style_drawer_base>>>;

struct style_collection {
    mr::heap_umap<std::string, referenced_ptr<style_drawer_base>, transparent::string_hasher, transparent::string_equal_to> map;
    referenced_ptr<style_drawer_base> default_style;

    explicit style_collection(
        referenced_ptr<style_drawer_base> default_val,
        const style_map_allocator& alloc = mr::get_default_heap_allocator<std::pair<const std::string, referenced_ptr<style_drawer_base>>>()
    ) : map(alloc), default_style(std::move(default_val)) {
        if (!default_style) {
            throw std::invalid_argument{"default_style cannot be null"};
        }
    }

    void set_default(referenced_ptr<style_drawer_base> new_default) {
        if (!new_default) {
            throw std::invalid_argument{"new_default cannot be null"};
        }
        default_style = std::move(new_default);
    }

    [[nodiscard]] referenced_ptr<style_drawer_base> get_default() const noexcept {
        return default_style;
    }
};

export
template <typename T>
struct style_manager_slice {
private:
    style_collection* collection;

    // 内部代理类：用于完美支持 slice["key"] = value 的直觉赋值语法
    template <typename KeyType>
    struct slice_proxy {
        style_collection* m_collection;
        KeyType m_key;

        // 读操作：隐式转换为 referenced_ptr<T>
        [[nodiscard]] operator referenced_ptr<T>() const {
            if (const auto itr = m_collection->map.find(m_key); itr != m_collection->map.end()) {
                return referenced_ptr<T>{static_cast<T*>(itr->second.get())};
            }
            return nullptr;
        }

        // 写操作：支持直接赋值
        slice_proxy& operator=(referenced_ptr<T> value) {
            m_collection->map.insert_or_assign(m_key, std::move(value));
            return *this;
        }
    };

public:
    [[nodiscard]] explicit(false) style_manager_slice(style_collection& coll)
        : collection(&coll) {}

    // 默认样式访问接口（带类型安全检查）
    [[nodiscard]] referenced_ptr<T> default_style() const noexcept {
        return referenced_ptr<T>{static_cast<T*>(collection->get_default().get())};
    }

	template <typename Key>
    [[nodiscard]] referenced_ptr<T> get_or_default(Key&& key) const noexcept {
    	if (const auto itr = collection->map.find(key); itr != collection->map.end()) {
    		return referenced_ptr<T>{static_cast<T*>(itr->second.get())};
        }
    	return default_style();
    }

    void set_default(referenced_ptr<T> new_default) const {
        collection->set_default(referenced_ptr{static_cast<style_drawer_base*>(new_default.get())});
    }

    template <typename Key>
    [[nodiscard]] referenced_ptr<T> at(Key&& key) const {
        if (const auto itr = collection->map.find(key); itr != collection->map.end()) {
            return referenced_ptr<T>{static_cast<T*>(itr->second.get())};
        }
        throw std::out_of_range{"key not found"};
    }

    template <typename Key>
    [[nodiscard]] slice_proxy<std::decay_t<Key>> operator[](Key&& key) const {
        return {collection, std::forward<Key>(key)};
    }


    template <typename Key, typename... Args>
        requires(std::constructible_from<referenced_ptr<T>, Args&&...>)
    auto insert_or_assign(Key&& key, Args&&... args) const {
        return collection->map.insert_or_assign(std::forward<Key>(key), std::forward<Args>(args)...);
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

using manager_map_allocator = mr::heap_allocator<std::pair<const type_identity_index, style_collection>>;

export
struct style_manager {
private:
    mr::heap_umap<type_identity_index, style_collection> style_map;

public:
    [[nodiscard]] explicit style_manager(
        const manager_map_allocator& alloc = mr::get_default_heap_allocator()
    ) : style_map(alloc) {}

	void reserve(std::size_t size){
		style_map.reserve(size);
    }

    // 2. 注册函数：要求传入非空默认值
    template <typename T_Explicit>
    auto register_style(referenced_ptr<std::type_identity_t<T_Explicit>> default_style) {
        if (!default_style) {
            throw std::invalid_argument{"default style cannot be null"};
        }

        // 传递当前 map 的 allocator 给子集合，保持内存分配区域的一致性
        style_map_allocator coll_alloc{style_map.get_allocator()};

        return style_map.insert_or_assign(
            mo_yanxi::unstable_type_identity_of<T_Explicit>(),
            style_collection{referenced_ptr<style_drawer_base>{std::move(default_style)}, coll_alloc}
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
    [[nodiscard]] referenced_ptr<T> get_default() noexcept {
    	return referenced_ptr{static_cast<T&>(*style_map.at(mo_yanxi::unstable_type_identity_of<T>()).get_default())};
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