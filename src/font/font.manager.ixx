module;

#include <cassert>
#include <freetype/fttypes.h>


export module mo_yanxi.font.manager;

import std;

export import mo_yanxi.font;

import mo_yanxi.graphic.image_region.borrow;
import mo_yanxi.graphic.image_region;
import mo_yanxi.graphic.image_atlas;

import mo_yanxi.math.vector2;

import mo_yanxi.heterogeneous;
import mo_yanxi.heterogeneous.open_addr_hash;
import mo_yanxi.cache;
import mo_yanxi.cache.map;
import mo_yanxi.static_string;

namespace mo_yanxi::font{
export
using glyph_texture_region = graphic::combined_image_region<graphic::uniformed_rect_uv>;
export
using glyph_borrow = graphic::universal_borrowed_image_region<glyph_texture_region, referenced_object_atomic>;

export
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

struct font_metrics{
	struct key{
		const font_face_handle* source;
		glyph_identity desc;

		constexpr bool operator==(const key&) const noexcept = default;
	};

	struct val{
		glyph_metrics metrics;
		math::vector2<FT_UShort> ppem;

		bool drawable() const noexcept{
			return ppem.x != 0 && ppem.y != 0;
		}
	};
};

}

template<>
struct std::hash<mo_yanxi::font::font_metrics::key>{
	std::size_t operator()(const mo_yanxi::font::font_metrics::key& key) const noexcept{
		return (std::hash<const void*>{}(key.source) << 31) ^ std::hash<mo_yanxi::font::glyph_identity>{}(key.desc);
	}
};

namespace mo_yanxi::font{

struct font_global_cache{
	using tls_lru_cache_t = lru_cache<const font_family*, std::span<font_face_handle>, 8>;
	tls_lru_cache_t family;
	mapped_lru_cache<font_metrics::key, font_metrics::val> metrics;

	auto get_metrics(const font_metrics::key key){
		if(!metrics.capacity())metrics = mapped_lru_cache<font_metrics::key, font_metrics::val>{4096};
		return metrics.get_ptr(key);
	}

	auto put_metrics(const font_metrics::key key, const font_metrics::val& val){
		return metrics.put(key, val);
	}
};

thread_local font_global_cache cache;

export
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
		return cache.family;
	}

	[[nodiscard]] static std::string_view format(glyph_size_identity& identity/*use lr to make sure no dangling*/){
		//This function assumes that glyph size is snapped to 0/64/128 and fit the string under SSO.

		return std::string_view{reinterpret_cast<const char *>(&identity), sizeof(identity)};
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

		auto mtr_cache = cache.get_metrics({&handle, key});
		acquire_result acq_rst;
		if(mtr_cache){
			acq_rst.metrics = mtr_cache->metrics;
			acq_rst.generator = graphic::msdf::msdf_glyph_generator{
				mtr_cache->drawable() ? handle.get_msdf_handle() : nullptr,
				mtr_cache->ppem.x, mtr_cache->ppem.y
			};
		}else{
			acq_rst = handle.obtain(key.index, key.size);
			cache.put_metrics({&handle, key}, {
					.metrics = acq_rst.metrics,
					.ppem = math::vector2<FT_UShort>(acq_rst.generator.font_h, acq_rst.generator.font_h)
				});
		}

		if(!acq_rst.has_drawable_glyph()){
			return glyph{acq_rst.metrics};
		}

		glyph_size_identity idt{handle.get_source(), key.index, key.size};
		auto name = format(idt);
		if(const auto prev = page().find(name)){
			if(auto borrow = prev->make_universal_borrow<glyph_texture_region>()){
				return glyph{std::move(*borrow), acq_rst.metrics};
			}
		}


		//
		graphic::sdf_load load{
			acq_rst.generator.crop(key.index, handle.get_source()->get_loader_msdf_handle()),
		acq_rst.metrics.size.copy().ceil().as<unsigned>() + (graphic::msdf::sdf_image_boarder * 2)
		};

		const auto aloc = page().register_named_region(
			std::move(name),
			graphic::image_load_description{std::move(load)});
		return glyph{*aloc.region.make_universal_borrow<glyph_texture_region>(), acq_rst.metrics};
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

	[[nodiscard]] font_family* get_default_family() const noexcept{
		return default_family;
	}

	void set_default_family(font_family* const default_recipe) noexcept{
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

export inline font_manager* default_font_manager{};

}