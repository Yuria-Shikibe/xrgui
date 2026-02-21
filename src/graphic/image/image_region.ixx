module;

#include <vulkan/vulkan.h>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.graphic.image_region;

export import mo_yanxi.math.vector2;
export import mo_yanxi.math.rect_ortho;
export import mo_yanxi.concepts;
import std;

//TODO vk buffer cache, secondary commandbuffer...

namespace mo_yanxi::graphic{
export
struct uniformed_rect_uv{
	math::vec2 vert00;
	math::vec2 vert11;

	[[nodiscard]] FORCE_INLINE constexpr math::vec2 v00() const noexcept{
		return vert00;
	}

	[[nodiscard]] FORCE_INLINE constexpr math::vec2 v11() const noexcept{
		return vert11;
	}

	[[nodiscard]] FORCE_INLINE constexpr math::vec2 v01() const noexcept{
		return {vert00.x, vert11.y};
	}

	[[nodiscard]] FORCE_INLINE constexpr math::vec2 v10() const noexcept{
		return {vert11.x, vert00.y};
	}

	constexpr void rotate(const int times) noexcept{
		switch(times & 0b11){
		case 0 : break;
		case 1 : *this = {v01(), v10()};
			break;
		case 2 : *this = {v11(), v00()};
			break;
		case 3 : *this = {v10(), v01()};
			break;
		default : std::unreachable();
		}
	}

	template <std::derived_from<uniformed_rect_uv> Ty>
	constexpr math::u32point2 get_sized_v00(this const Ty& self, const math::u32size2 size) noexcept{
		const math::vec2 p = self.v00() * size.as<float>();
		return p.round<std::uint32_t>();
	}

	template <std::derived_from<uniformed_rect_uv> Ty>
	constexpr math::u32point2 get_sized_v11(this const Ty& self, const math::u32size2 size) noexcept{
		const math::vec2 p = self.v11() * size.as<float>();
		return p.round<std::uint32_t>();
	}

	template <std::derived_from<uniformed_rect_uv> Ty>
	constexpr math::u32point2 get_sized_v10(this const Ty& self, const math::u32size2 size) noexcept{
		const math::vec2 p = self.v10() * size.as<float>();
		return p.round<std::uint32_t>();
	}

	template <std::derived_from<uniformed_rect_uv> Ty>
	constexpr math::u32point2 get_sized_v01(this const Ty& self, const math::u32size2 size) noexcept{
		const math::vec2 p = self.v01() * size.as<float>();
		return p.round<std::uint32_t>();
	}

	template <std::derived_from<uniformed_rect_uv> Ty, typename T>
	[[nodiscard]] constexpr math::rect_ortho<T>
	proj_region(this const Ty& self, const math::rect_ortho<T>& rect) noexcept{
		auto sz = rect.extent();

		auto v00 = self.get_sized_v00(sz);
		auto v11 = self.get_sized_v11(sz);
		return math::rect_ortho<T>{tags::from_vertex, v00 + rect.src, v11 + rect.src};
	}

	template <std::derived_from<uniformed_rect_uv> Ty, typename N>
	constexpr void fetch_into(this Ty& self, const math::vector2<N> bound_size,
		const math::rect_ortho<N>& region) noexcept{
		self.vert00 = region.vert_00().template as<float>() / bound_size.template as<float>();
		self.vert11 = region.vert_11().template as<float>() / bound_size.template as<float>();
	}


	template <std::derived_from<uniformed_rect_uv> Ty>
	constexpr void shrink(this Ty& self, const math::u32size2 bound_size, const math::vec2 shrink) noexcept{
		auto unitLen = ~bound_size.as<float>() * shrink;
		unitLen.min((self.vert11 - self.vert00) / 2.f);

		self.vert00 += unitLen;
		self.vert11 -= unitLen;
	}

	template <std::derived_from<uniformed_rect_uv> Ty>
	constexpr void shrink(this Ty& self, const math::u32size2 bound_size, const float shrink) noexcept{
		self.shrink(bound_size, math::vec2{shrink, shrink});
	}

	constexpr void flip_y() noexcept{
		std::swap(vert00.y, vert11.y);
	}

	constexpr void flip_x() noexcept{
		std::swap(vert00.x, vert11.x);
	}
};

export
struct uniformed_uv : uniformed_rect_uv{
	math::vec2 vert01;
	math::vec2 vert10;

	constexpr void flip_y() = delete;
	constexpr void flip_x() = delete;

	[[nodiscard]] FORCE_INLINE constexpr math::vec2 v10() const noexcept{
		return vert10;
	}

	[[nodiscard]] FORCE_INLINE constexpr math::vec2 v01() const noexcept{
		return vert01;
	}

	template <std::derived_from<uniformed_rect_uv> Ty, typename N>
	constexpr void fetch_into(this Ty& self, const math::vector2<N> bound_size,
		const math::rect_ortho<N>& region) noexcept{
		self.fetch_into(bound_size, region);
		self.vert10 = region.vert_10().template as<float>() / bound_size.template as<float>();
		self.vert01 = region.vert_01().template as<float>() / bound_size.template as<float>();
	}

	template <std::derived_from<uniformed_rect_uv> Ty>
	constexpr void shrink(this Ty& self, const math::u32size2 bound_size, const math::vec2 shrink) noexcept{
		self.shrink(self, bound_size, shrink);

		auto unitLen = ~bound_size.as<float>() * shrink;
		unitLen.min((self.vert11 - self.vert00) / 2.f);

		self.vert10 += unitLen.copy().flip_x();
		self.vert01 += unitLen.copy().flip_y();
	}
};

export
template <std::derived_from<uniformed_rect_uv> Ty>
struct size_awared_uv : Ty{
	/**
	 * @brief Size of the whole image, not the region.
	 * To get the size of the region, call {@link get_region().size() @endlink }
	 */
	math::u32size2 size;


	[[nodiscard]] constexpr math::u32point2 get_sized_v00() const noexcept{
		return this->Ty::get_sized_v00(size);
	}

	[[nodiscard]] constexpr math::u32point2 get_sized_v11() const noexcept{
		return this->Ty::get_sized_v11(size);
	}

	[[nodiscard]] constexpr math::u32point2 get_sized_v10() const noexcept{
		return this->Ty::get_sized_v10(size);
	}

	[[nodiscard]] constexpr math::u32point2 get_sized_v01() const noexcept{
		return this->Ty::get_sized_v01(size);
	}

	using Ty::get_sized_v00;
	using Ty::get_sized_v10;
	using Ty::get_sized_v11;
	using Ty::get_sized_v01;

	[[nodiscard]] constexpr math::urect get_region() const noexcept{
		return this->Ty::proj_region(math::urect{tags::unchecked, tags::from_vertex, {}, size});
	}

	template <typename T = std::uint32_t>
	[[nodiscard]] constexpr math::vector2<T> get_region_size() const noexcept{
		auto sz = get_region().extent();
		if constexpr(std::floating_point<T>){
			return sz.template as<T>();
		} else{
			return sz.template round<T>();
		}
	}

	template <typename N>
	constexpr void fetch_into(const math::vector2<N> bound_size, const math::rect_ortho<N>& region) noexcept{
		this->Ty::fetch_into(bound_size, region);
		size = bound_size.template as<typename decltype(size)::value_type>();
	}

	using Ty::shrink;

	constexpr void shrink(const float shrink) noexcept{
		this->Ty::shrink(size, shrink);
	}
};


export using image_descriptor_index = std::uint32_t;

export
struct sized_image{
	math::usize2 size;
	image_descriptor_index index;
	VkImageView view;
};

export
template <std::derived_from<uniformed_rect_uv> Ty>
struct combined_image_region{
	Ty uv;
	image_descriptor_index index;
	VkImageView view;


	constexpr explicit(false) operator VkImageView() const noexcept{
		return view;
	}

	template <typename Uv>
		requires (std::constructible_from<Uv, Ty>)
	constexpr explicit(!std::convertible_to<Ty, Uv>) operator combined_image_region<Uv>() const noexcept{
		return combined_image_region<Uv>{static_cast<Uv>(uv), index, view};
	}

	constexpr explicit(false) operator sized_image() const noexcept requires (spec_of<Ty, size_awared_uv>){
		return sized_image{uv.size, index, view};
	}
};

// static_assert(std::is_trivial_v<combined_image_region<uniformed_uv>>);

export using image_rect_region = combined_image_region<uniformed_rect_uv>;
}
