module;

#include <cassert>
#include <freetype/freetype.h>

export module mo_yanxi.font.manager;

export import mo_yanxi.font;

import mo_yanxi.graphic.image_region.borrow;
import mo_yanxi.graphic.image_region;
import mo_yanxi.graphic.image_atlas;

import mo_yanxi.math.vector2;

import mo_yanxi.heterogeneous;
import mo_yanxi.heterogeneous.open_addr_hash;
import std;

export namespace mo_yanxi::font{

	struct glyph : graphic::universal_borrowed_image_region<graphic::combined_image_region<graphic::uniformed_rect_uv>, referenced_object_atomic_nonpropagation>{
	private:
		glyph_metrics metrics_{};

	public:
		[[nodiscard]] glyph() = default;

		[[nodiscard]] glyph(const glyph_metrics& metrics, graphic::allocated_image_region& region)
			: universal_borrowed_image_region{region}, metrics_{metrics}{
			assert(region.view != nullptr);
			assert(this->operator*().view != nullptr);
		}

		[[nodiscard]] explicit(false) glyph(const glyph_metrics& metrics)
			: universal_borrowed_image_region{}, metrics_{metrics}{}

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
		string_hash_map<font_face> fontFaces{};
		font_index_hash_map face_to_index{};

		std::mutex mutex_{};

		[[nodiscard]] static std::string format(const unsigned idx, const char_code code, const glyph_size_type size){
			return std::format("{}.{:#X}|{},{}", idx, std::bit_cast<int>(code), size.x, size.y);
		}

	public:
		[[nodiscard]] font_manager() = default;

		[[nodiscard]] explicit font_manager(graphic::image_page& page) :
			fontPage(&page)
		{}

		void set_page(graphic::image_page& f_page){
			if(fontPage == &f_page)return;
			fontPage = &f_page;
			fontFaces.clear();
		}

		[[nodiscard]] graphic::image_page& page() const noexcept{
			assert(fontPage != nullptr);
			return *fontPage;
		}

	private:
		[[nodiscard]] face_id get_face_id(const font_face& ff){
			std::lock_guard _{mutex_};

			const concat_string_view sv{ff.face().get_family_name(), ff.face().get_face_index()};
			if(auto itr = face_to_index.find(sv); itr != face_to_index.end()){
				return itr->second;
			}

			auto idx = face_to_index.size();
			return face_to_index.try_emplace(sv, idx).first->second;
		}
	public:

		[[nodiscard]] glyph get_glyph_exact(font_face& ff, const glyph_identity key){

			const auto [ptr, mtx, gen, ext] = ff.obtain(key.code, key.size);

			if(!gen.face ||is_space(key.code)){
				return glyph{mtx};
			}

			auto id = get_face_id(ff);
			auto name = format(id, key.code, key.size);
			if(const auto prev = page().find(name)){
				return glyph{mtx, *prev};
			}

			graphic::sdf_load load;
			{
				auto _ = ptr->get_msdf_lock();
				load = graphic::sdf_load{
					gen.crop(key.code), ext
				};
			}

			const auto aloc = page().register_named_region(
				std::move(name),
				graphic::image_load_description{std::move(load)});
			assert(aloc.region.view != nullptr);
			return glyph{mtx, aloc.region};
		}

		font_face& register_face(std::string_view keyName, std::string_view fontName){
			return fontFaces.try_emplace(keyName, fontName).first->second;
		}

		[[nodiscard]] font_face* find_face(std::string_view keyName) noexcept{
			return fontFaces.try_find(keyName);
		}

		[[nodiscard]] const font_face* find_face(std::string_view keyName) const noexcept{
			return fontFaces.try_find(keyName);
		}

	};
}
