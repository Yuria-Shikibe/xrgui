module;

#include <cassert>

#ifdef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
//no sense the compiler cannot find std::hash<sv>
#include <string_view>
#else
#include <msdfgen/msdfgen-ext.h>
#endif


export module mo_yanxi.font.manager;

export import mo_yanxi.font;

import mo_yanxi.graphic.image_region.borrow;
import mo_yanxi.graphic.image_region;
import mo_yanxi.graphic.image_atlas;

import mo_yanxi.math.vector2;

import mo_yanxi.heterogeneous;
import mo_yanxi.heterogeneous.open_addr_hash;
import std;
import mo_yanxi.cache; // [cite: 34] 引入 LRU Cache

#ifdef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
//no sense the compiler cannot find std::hash<sv>
import <msdfgen/msdfgen-ext.h>;
#endif

export namespace mo_yanxi::font{
using glyph_texture_region = graphic::combined_image_region<graphic::uniformed_rect_uv>;
using glyph_borrow = graphic::universal_borrowed_image_region<glyph_texture_region,
	referenced_object_atomic_nonpropagation>;
struct glyph : glyph_borrow{
private:
	glyph_metrics metrics_{};

public:
	[[nodiscard]] glyph() = default;

	[[nodiscard]] glyph(glyph_borrow&& borrow, const glyph_metrics& metrics)
		: universal_borrowed_image_region{std::move(borrow)}, metrics_{metrics}{
		assert(borrow->view != nullptr);
		assert(this->operator*().view != nullptr);
	}

	[[nodiscard]] explicit(false) glyph(const glyph_metrics& metrics)
		: universal_borrowed_image_region{}, metrics_{metrics}{
	}

	[[nodiscard]] const glyph_metrics& metrics() const noexcept{
		return metrics_;
	}
};

struct concat_string_view{
	std::string_view family_name{};
	std::int32_t style_index{};
};

struct concat_string{
	std::string family_name{};
	std::int32_t style_index{};

	[[nodiscard]] concat_string() = default;

	[[nodiscard]] explicit(false) concat_string(const concat_string_view& view)
		: family_name(view.family_name),
		style_index(view.style_index){
	}
};
struct concat_string_hasher{
	using is_transparent = void;
	static constexpr std::hash<std::string_view> hasher{};
	static constexpr std::hash<std::int32_t> idx_hasher{};
	static std::size_t operator()(const concat_string& str) noexcept{
		const auto h1 = hasher(str.family_name);
		const auto h2 = idx_hasher(str.style_index);
		return h1 ^ (h2 << 31);
	}

	static std::size_t operator()(const concat_string_view& str) noexcept{
		const auto h1 = hasher(str.family_name);
		const auto h2 = idx_hasher(str.style_index);
		return h1 ^ (h2 << 31);
	}
};

struct concat_string_eq{
	using is_transparent = void;
	using is_direct = void;

	static bool operator()(const concat_string& lhs) noexcept{
		return lhs.family_name.empty();
	}

	static bool operator()(const concat_string_view& lhs) noexcept{
		return lhs.family_name.empty();
	}

	static bool operator()(const concat_string& lhs, const concat_string& rhs) noexcept{
		return lhs.family_name == rhs.family_name && lhs.style_index == rhs.style_index;
	}

	static bool operator()(const concat_string_view& lhs, const concat_string& rhs) noexcept{
		return lhs.family_name == rhs.family_name && lhs.style_index == rhs.style_index;
	}

	static bool operator()(const concat_string& lhs, const concat_string_view& rhs) noexcept{
		return lhs.family_name == rhs.family_name && lhs.style_index == rhs.style_index;
	}

	static bool operator()(const concat_string_view& lhs, const concat_string_view& rhs) noexcept{
		return lhs.family_name == rhs.family_name && lhs.style_index == rhs.style_index;
	}
};
using face_id = unsigned;
using font_index_hash_map =
fixed_open_addr_hash_map<
	concat_string,
	face_id,
	concat_string_view,
	std::in_place,
	concat_string_hasher,
	concat_string_eq>;

// 组配方：一个名字对应一串Meta（共享资源）
struct group_recipe{
	std::vector<const font_face_meta*> metas;
};

struct font_manager{
private:


	graphic::image_page* fontPage{};

	string_hash_map<font_face_meta> metas{};
	// 所有配方表 (名称 -> 元数据列表)
	string_hash_map<group_recipe> recipes{};
	group_recipe* default_recipe{};

	font_index_hash_map face_to_index{};

    // --- Thread Storage & Caching ---

    // 类型定义：持久化存储
    using handle_vector_t = std::vector<font_face_handle>;
    using recipe_map_t = std::unordered_map<const group_recipe*, handle_vector_t>;
    using global_storage_t = std::unordered_map<std::thread::id, recipe_map_t>;

    // 成员变量：实际持有 handle 内存的存储区 (按线程ID分片)
    mutable std::mutex storage_mtx_;
    global_storage_t handle_storage_;

    // 类型定义：LRU 缓存 (Key: Recipe指针, Value: 指向 vector 内部的 Span)
    // 缓存大小设为 64，足以覆盖绝大多数 UI 场景下的常用字体变体
    using tls_lru_cache_t = mo_yanxi::lru_cache<const group_recipe*, std::span<font_face_handle>, 8>;

	// 静态 Thread Local LRU Cache
    // 只缓存 View (Span)，不持有所有权
	static tls_lru_cache_t& get_thread_local_lru(){
		thread_local tls_lru_cache_t cache;
		return cache;
	}

	[[nodiscard]] static std::string format(const unsigned idx, const char_code code, const glyph_size_type size){
		return std::format("{}.{:#X}|{},{}", idx, std::bit_cast<int>(code), size.x, size.y);
	}


public:
	[[nodiscard]] font_manager() = default;

	[[nodiscard]] explicit font_manager(graphic::image_page& page) :
		fontPage(&page){
	}

	void set_page(graphic::image_page& f_page){
		if(fontPage == &f_page) return;
		fontPage = &f_page;
		// fontFaces.clear();
	}

	[[nodiscard]] graphic::image_page& page() const noexcept{
		assert(fontPage != nullptr);
		return *fontPage;
	}

private:
	[[nodiscard]] face_id get_face_id(const font_face_handle& ff){
		const concat_string_view sv{ff.get_family_name(), ff.get_face_index()};
		if(auto itr = face_to_index.find(sv); itr != face_to_index.end()){
			return itr->second;
		}

		auto idx = face_to_index.size();
		return face_to_index.try_emplace(sv, idx).first->second;
	}

public:
	[[nodiscard]] glyph get_glyph_exact(
		font_face_handle& handle, const glyph_identity key){
		const auto [mtx, gen, ext] = handle.obtain(key.index, key.size);

		if(!gen.face){
			return glyph{mtx};
		}

		auto id = get_face_id(handle);
		auto name = format(id, key.index, key.size);
		if(const auto prev = page().find(name)){
			if(auto borrow = prev->make_universal_borrow<glyph_texture_region>()){
				return glyph{std::move(*borrow), mtx};
			}
		}

		graphic::sdf_load load{
				gen.crop(msdfgen::GlyphIndex(key.index), handle.get_source()->get_loader_msdf_handle()), ext
			};
		const auto aloc = page().register_named_region(
			std::move(name),
			graphic::image_load_description{std::move(load)});
		return glyph{*aloc.region.make_universal_borrow<glyph_texture_region>(), mtx};
	}


	group_recipe& register_family(std::string_view familyName, std::vector<const font_face_meta*> metas){
		return recipes.insert_or_assign(std::string(familyName), group_recipe{std::move(metas)}).first->second;
	}

	group_recipe& register_family(std::string_view familyName, std::span<const font_face_meta*> metas){
		return recipes.insert_or_assign(std::string(familyName), group_recipe{std::vector{std::from_range, metas}}).first->second;
	}

	group_recipe& register_family(std::string_view familyName, const font_face_meta& meta){
		std::vector<std::shared_ptr<font_face_meta>> vec;
		return register_family(familyName, {&meta});
	}

	group_recipe* find_family(std::string_view familyName) noexcept{
		return recipes.try_find(familyName);
	}

	[[nodiscard]] group_recipe* get_default_recipe() const noexcept{
		return default_recipe;
	}

	void set_default_recipe(group_recipe* const default_recipe) noexcept{
		this->default_recipe = default_recipe;
	}

	template <typename Path>
		requires (std::constructible_from<font_face_meta, const Path&>)
	const font_face_meta& register_meta(std::string_view familyName, const Path& path){
		return register_meta(familyName, font_face_meta{path});
	}

	const font_face_meta& register_meta(std::string_view familyName, font_face_meta meta){
		return metas.insert_or_assign(familyName, std::move(meta)).first->second;
	}

	// 获取 View (核心方法)
	// 1. 查 LRU 缓存
    // 2. 查 Manager 的 Global Storage (加锁)
    // 3. 更新 LRU 并返回
	[[nodiscard]] font_face_view use_family(const group_recipe* identity){
		if(!identity){
			return {};
		}

        // 1. 尝试在线程局部 LRU 缓存中查找 (Fast Path)
		auto& tls_lru = get_thread_local_lru();
		if(auto* span_ptr = tls_lru.get(identity)) {
			return font_face_view{*span_ptr};
		}

        // 2. 缓存未命中，访问全局存储 (Slow Path)
        std::span<font_face_handle> result_span;
        {
            const auto current_tid = std::this_thread::get_id();
            std::lock_guard lock(storage_mtx_); // 保护 handle_storage_

            // 定位到当前线程的 Map
            auto& thread_recipe_map = handle_storage_[current_tid];

            auto it = thread_recipe_map.find(identity);
            if(it != thread_recipe_map.end()) {
                // 已经存在于存储中，直接构造 Span
                result_span = std::span{it->second};
            } else {
                // 3. 首次使用该配方，实例化 Handles
                std::vector<font_face_handle> new_handles;
                new_handles.reserve(identity->metas.size());
                for(const auto& meta_ptr : identity->metas){
                    if(meta_ptr){
                        new_handles.emplace_back(meta_ptr->data(), meta_ptr);
                    }
                }

                // 存入全局 Map
                auto& inserted_vec = thread_recipe_map.emplace(identity, std::move(new_handles)).first->second;
                result_span = std::span{inserted_vec};
            }
        }

		// 4. 回填入 TLS LRU 并返回 View
        // 注意：Global Storage 中的 vector 地址稳定（只要不删除元素），因此 span 安全
		tls_lru.put(identity, result_span);
		return font_face_view{result_span};
	}
};
}