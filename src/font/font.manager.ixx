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

struct font_manager{
private:
	graphic::image_page* fontPage{};
	string_hash_map<font_face_group_meta> fontFaces{};
	font_index_hash_map face_to_index{};

	// std::mutex mutex_{};

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
	[[nodiscard]] glyph get_glyph_exact(font_face_group_meta& group, const font_face_view& view,
		font_face_handle& handle, const glyph_identity key){
		const auto [mtx, gen, ext] = handle.obtain(key.index, key.size);

		if(!gen.face){
			return glyph{mtx};
		}

		auto index = std::addressof(handle) - std::to_address(view.begin());
		auto& face_main_thread_only = group.main_group_at(index);

		auto id = get_face_id(face_main_thread_only);
		auto name = format(id, key.index, key.size);
		if(const auto prev = page().find(name)){
			if(auto borrow = prev->make_universal_borrow<glyph_texture_region>()){
				return glyph{std::move(*borrow), mtx};
			}
		}

		graphic::sdf_load load{
				gen.crop(msdfgen::GlyphIndex(key.index), face_main_thread_only.msdfHdl), ext
			};

		const auto aloc = page().register_named_region(
			std::move(name),
			graphic::image_load_description{std::move(load)});
		return glyph{*aloc.region.make_universal_borrow<glyph_texture_region>(), mtx};
	}

	font_face_group_meta& register_face(std::string_view keyName, const char* fontName){
		return fontFaces.try_emplace(keyName, fontName).first->second;
	}

	[[nodiscard]] font_face_group_meta* find_face(std::string_view keyName) noexcept{
		return fontFaces.try_find(keyName);
	}

	[[nodiscard]] const font_face_group_meta* find_face(std::string_view keyName) const noexcept{
		return fontFaces.try_find(keyName);
	}
};
}
