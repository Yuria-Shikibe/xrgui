export module mo_yanxi.gui.style.config;

export import mo_yanxi.graphic.color;
export import align;

import std;

namespace mo_yanxi::gui::style{

export
struct palette{
	graphic::color regular{};
	graphic::color on_focus{};
	graphic::color on_press{};

	graphic::color disabled{};
	graphic::color toggled{};

	[[nodiscard]] constexpr palette copy() const noexcept{
		return *this;
	}

	constexpr palette& mul_alpha(const float alpha) noexcept{
		regular.mul_a(alpha);
		on_focus.mul_a(alpha);
		on_press.mul_a(alpha);

		disabled.mul_a(alpha);
		toggled.mul_a(alpha);

		return *this;
	}

	constexpr palette& mul_rgb(const float alpha) noexcept{
		regular.mul_rgb(alpha);
		on_focus.mul_rgb(alpha);
		on_press.mul_rgb(alpha);

		disabled.mul_rgb(alpha);
		toggled.mul_rgb(alpha);

		return *this;
	}
};

export constexpr inline palette general_palette{
		graphic::colors::light_gray,
		graphic::colors::white,
		graphic::colors::aqua,
		graphic::colors::gray,
		graphic::colors::white,
	};

template <typename T>
struct palette_with : public T{
	palette pal{};

	using T::operator=;

	[[nodiscard]] palette_with() = default;

	[[nodiscard]] palette_with(const T& val, const palette& pal)
		requires (std::is_copy_constructible_v<T>)
	: T{val}, pal{pal}{
	}
};
}
