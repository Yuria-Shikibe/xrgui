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
import mo_yanxi.cache;
import mo_yanxi.static_string;

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

struct glyph_size_identity{
	char head[2]{'f', 't'};
	char extent;
	std::array<char, 8> handle_ptr;
	std::array<char, 4> code;

	constexpr glyph_size_identity(const void* ptr, glyph_index_t index, glyph_size_type size) : extent(
		((size.x / 32) << 4) | (size.y / 32)
	), handle_ptr(std::bit_cast<std::array<char, 8>>(ptr)), code(std::bit_cast<std::array<char, 4>>(index)){

	}
};

struct font_manager{
	using family_name_t = static_string<23>;

private:
	graphic::image_page* fontPage{};

	string_hash_map<font_face_meta> metas{};

	basic_string_hash_map<family_name_t, font_family> families{};

	font_family* default_family{};

    using handle_vector_t = std::vector<font_face_handle>;
    using recipe_map_t = std::unordered_map<const font_family*, handle_vector_t>;
    using global_storage_t = std::unordered_map<std::thread::id, recipe_map_t>;

    mutable std::mutex storage_mtx_;
    global_storage_t handle_storage_;

    using tls_lru_cache_t = lru_cache<const font_family*, std::span<font_face_handle>, 8>;
	static tls_lru_cache_t& get_thread_local_lru(){
		thread_local tls_lru_cache_t cache;
		return cache;
	}

	[[nodiscard]] static std::string format(glyph_size_identity identity){
		//This function assumes that glyph size is snapped to 0/64/128 and fit the string under SSO.
		std::string str;
		str.resize_and_overwrite(
			sizeof(glyph_size_identity), //fit into sso
			[&](char* buf, std::size_t n){
				std::memcpy(buf, &identity, sizeof(glyph_size_identity));
				return sizeof(glyph_size_identity);
			}
		);
		return str;
	}


public:
	[[nodiscard]] font_manager() = default;

	[[nodiscard]] explicit font_manager(graphic::image_page& page) :
		fontPage(&page){
	}

	void set_page(graphic::image_page& f_page){
		if(fontPage == &f_page) return;
		fontPage = &f_page;
	}

	[[nodiscard]] graphic::image_page& page() const noexcept{
		assert(fontPage != nullptr);
		return *fontPage;
	}


public:
	[[nodiscard]] glyph get_glyph_exact(
		font_face_handle& handle, const glyph_identity key){
		const auto [mtx, gen, ext] = handle.obtain(key.index, key.size);

		if(!gen.face){
			return glyph{mtx};
		}

		auto name = format({handle.get_source(), key.index, key.size});
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


	font_family& register_family(std::string_view familyName, std::vector<const font_face_meta*> metas){
		return families.insert_or_assign(std::string(familyName), font_family{std::move(metas)}).first->second;
	}

	font_family& register_family(std::string_view familyName, std::span<const font_face_meta*> metas){
		return families.insert_or_assign(std::string(familyName), font_family{std::vector{std::from_range, metas}}).first->second;
	}

	font_family& register_family(std::string_view familyName, const font_face_meta& meta){
		std::vector<std::shared_ptr<font_face_meta>> vec;
		return register_family(familyName, {&meta});
	}

	font_family* find_family(std::string_view familyName) noexcept{
		return families.try_find(familyName);
	}

	font_family* find_family(family_name_t familyName) noexcept{
		return families.try_find(familyName);
	}

	[[nodiscard]] font_family* get_default_recipe() const noexcept{
		return default_family;
	}

	void set_default_recipe(font_family* const default_recipe) noexcept{
		this->default_family = default_recipe;
	}

	template <typename Path>
		requires (std::constructible_from<font_face_meta, const Path&>)
	const font_face_meta& register_meta(std::string_view familyName, const Path& path){
		return register_meta(familyName, font_face_meta{path});
	}

	const font_face_meta& register_meta(std::string_view familyName, font_face_meta meta){
		return metas.insert_or_assign(familyName, std::move(meta)).first->second;
	}

	[[nodiscard]] font_face_view use_family(const font_family* identity){
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

			auto& thread_recipe_map = [&] -> auto& {
				std::lock_guard lock(storage_mtx_);
				return handle_storage_[current_tid];
			}();

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

		tls_lru.put(identity, result_span);
		return font_face_view{result_span};
	}

	void UNCHECKED_clear_this_thread_font_face_cache() noexcept {
		get_thread_local_lru().clear();

		const auto current_tid = std::this_thread::get_id();

		std::lock_guard lock(storage_mtx_);
		handle_storage_.erase(current_tid);
	}
};
}