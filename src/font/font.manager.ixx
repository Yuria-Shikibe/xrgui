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
using glyph_borrow = graphic::universal_borrowed_constant_image_region<glyph_texture_region, referenced_object_atomic_lazy>;

export
struct glyph : glyph_borrow{
private:
	glyph_metrics metrics_{};

public:
	[[nodiscard]] glyph() = default;

	[[nodiscard]] glyph(glyph_borrow&& borrow, const glyph_metrics& metrics)
		: universal_borrowed_constant_image_region{std::move(borrow)}, metrics_{metrics}{
		assert(borrow->view != nullptr);
		assert(this->operator*().view != nullptr);
	}

	[[nodiscard]] explicit(false) glyph(const glyph_metrics& metrics)
		: universal_borrowed_constant_image_region{}, metrics_{metrics}{
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

export struct family_style_key {
	const font_family* family;
	font_style style;

	constexpr bool operator==(const family_style_key&) const noexcept = default;
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
		return (std::hash<const void*>{}(key.source) << 1) ^ std::hash<mo_yanxi::font::glyph_identity>{}(key.desc);
	}
};
template<>
struct std::hash<mo_yanxi::font::family_style_key>{
	std::size_t operator()(const mo_yanxi::font::family_style_key& key) const noexcept{
		return (std::hash<const void*>{}(key.family)) ^ (std::hash<mo_yanxi::font::font_style>{}(key.style) << 1);
	}
};

namespace mo_yanxi::font{

struct font_global_cache{
	using tls_lru_cache_t = lru_cache<family_style_key, std::span<font_face_handle>, 8>;
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
    using recipe_map_t = std::unordered_map<family_style_key, handle_vector_t>;
    using global_storage_t = std::unordered_map<std::thread::id, recipe_map_t>;

    mutable std::mutex storage_mtx_;
    global_storage_t handle_storage_;

    using tls_lru_cache_t = lru_cache<family_style_key, std::span<font_face_handle>, 8>;

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


	font_family& register_family(std::string_view familyName, std::span<const font_face_meta* const> metas, font_style style = font_style::normal) {
		font_family* target = families.try_find(familyName);
		if (!target) {
			target = &families.insert_or_assign(std::string(familyName), font_family{}).first->second;
		}
		target->set_style(style, metas);
		return *target;
	}

	font_family& register_family(std::string_view familyName, std::initializer_list<const font_face_meta*> metas, font_style style = font_style::normal) {
		return register_family(familyName, std::span{metas}, style);
	}

	font_family& register_family(std::string_view familyName, const font_face_meta& meta, font_style style = font_style::normal) {
		const font_face_meta* ptr = &meta;
		return register_family(familyName, std::span{&ptr, 1}, style);
	}

	template <std::ranges::input_range PrimaryRange = std::initializer_list<const font_face_meta*>, std::ranges::input_range FallbackRange = std::initializer_list<const font_face_meta*>>
		requires (std::convertible_to<std::ranges::range_reference_t<PrimaryRange>, const font_face_meta*> && std::convertible_to<std::ranges::range_reference_t<FallbackRange>, const font_face_meta*>)
	font_family& register_family(
		std::string_view familyName,
		PrimaryRange&& primary_metas,
		FallbackRange&& fallback_metas){

		font_family* target = families.try_find(familyName);
		if(!target){
			target = &families.insert_or_assign(std::string(familyName), font_family{}).first->second;
		}

		// 为四个变种准备各自的收集容器
		std::vector<const font_face_meta*> normal_chain;
		std::vector<const font_face_meta*> bold_chain;
		std::vector<const font_face_meta*> italic_chain;
		std::vector<const font_face_meta*> bold_italic_chain;

		// 1. 预先提取 Fallback 范围，转为 vector 方便稍后拼接
		std::vector<const font_face_meta*> fallbacks;
		for(const auto* f_meta : fallback_metas){
			if(f_meta) fallbacks.push_back(f_meta);
		}

		// 2. 遍历 primary_metas 检查 style flags 并分类
		for(const auto* p_meta : primary_metas){
			if(!p_meta) continue;

			const bool is_b = p_meta->is_bold();
			const bool is_i = p_meta->is_italic();

			if(is_b && is_i){
				bold_italic_chain.push_back(p_meta);
			} else if(is_b){
				bold_chain.push_back(p_meta);
			} else if(is_i){
				italic_chain.push_back(p_meta);
			} else{
				normal_chain.push_back(p_meta);
			}
		}

		// 3. 辅助 Lambda: 将 fallbacks 拼接到尾部并注册该样式
		auto apply_chain = [&](font_style style, std::vector<const font_face_meta*>& chain){
			// 如果该样式包含主要字体，或是 normal 样式(即使为空也需挂载 fallback 作为兜底)
			if(!chain.empty() || style == font_style::normal){
				chain.insert(chain.end(), fallbacks.begin(), fallbacks.end());
				target->set_style(style, chain);
			}
		};

		// 4. 分别应用到各个 style
		apply_chain(font_style::normal, normal_chain);
		apply_chain(font_style::bold, bold_chain);
		apply_chain(font_style::italic, italic_chain);
		apply_chain(font_style::bold_italic, bold_italic_chain);

		return *target;
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

	[[nodiscard]] styled_font_face_view use_family(const font_family* identity, font_style style = font_style::normal){
		if(!identity || identity->empty()){
			return {};
		}

		// 确定实际要使用的 metas。如果请求的 style 没有配置，强制回退到 normal
		font_style actual_style = style;
		auto target_metas = identity->get_style(actual_style);

		if (target_metas.empty()) {
			actual_style = font_style::normal;
			target_metas = identity->get_style(actual_style);
		}

		if (target_metas.empty()) {
			return {}; // 连 normal 都没有配置，直接返回空
		}

		family_style_key cache_key{identity, actual_style};

		// 1. Thread Local Cache (Fast Path)
		auto& tls_lru = get_thread_local_lru();
		std::span<font_face_handle> result_span;

		if(auto* span_ptr = tls_lru.get(cache_key)) {
			result_span = *span_ptr;
		} else {
			// 2. 访问全局存储 (Slow Path)
			const auto current_tid = std::this_thread::get_id();
			auto& thread_recipe_map = [&] -> auto& {
				std::lock_guard lock(storage_mtx_);
				return handle_storage_[current_tid];
			}();

			auto it = thread_recipe_map.find(cache_key);
			if(it != thread_recipe_map.end()) {
				result_span = std::span{it->second};
			} else {
				// 3. 首次使用该配方的该样式，实例化 Handles
				std::vector<font_face_handle> new_handles;
				new_handles.reserve(target_metas.size());
				for(const auto& meta_ptr : target_metas){
					if(meta_ptr){
						new_handles.emplace_back(meta_ptr->data(), meta_ptr);
					}
				}

				auto& inserted_vec = thread_recipe_map.emplace(cache_key, std::move(new_handles)).first->second;
				result_span = std::span{inserted_vec};
			}
			tls_lru.put(cache_key, result_span);
		}

		font_face_view final_view{result_span};

		bool request_bold = (static_cast<std::uint32_t>(style) & static_cast<std::uint32_t>(font_style::bold)) != 0;
		bool request_italic = (static_cast<std::uint32_t>(style) & static_cast<std::uint32_t>(font_style::italic)) != 0;

		bool satisfy_bold = true;
		bool satisfy_italic = true;

		if (!final_view.empty()) {
			const auto& primary_face = final_view.face();

			// 如果实际使用的是专门注册的粗体/斜体文件，则认为它满足了对应需求
			bool used_specific_bold = (static_cast<std::uint32_t>(actual_style) & static_cast<std::uint32_t>(font_style::bold)) != 0;
			bool used_specific_italic = (static_cast<std::uint32_t>(actual_style) & static_cast<std::uint32_t>(font_style::italic)) != 0;

			if (request_bold && !used_specific_bold && !primary_face.is_bold()) {
				satisfy_bold = false;
			}
			if (request_italic && !used_specific_italic && !primary_face.is_italic()) {
				satisfy_italic = false;
			}
		} else {
			satisfy_bold = false;
			satisfy_italic = false;
		}

		return {final_view, satisfy_bold, satisfy_italic};
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