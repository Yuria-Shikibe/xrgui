module;

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.graphic.gradient;

export import mo_yanxi.graphic.color;
export import mo_yanxi.math;
export import mo_yanxi.math.interpolation;

import std;

namespace mo_yanxi::graphic{
	export using interp = math::interp::trivial_interp;

	export
	template <typename T>
	struct mix_func{
		FORCE_INLINE constexpr static T operator()(const T& src, const T& dst, float interp) noexcept{
			return src * (1 - interp) + dst * interp;
		}
	};

	export
	template <>
	struct mix_func<float>{
		FORCE_INLINE constexpr static auto operator()(const float src, const float dst, float interp) noexcept{
			return math::lerp(src, dst, interp);
		}
	};


	export
	template <typename T>
	struct gradient{
		T src;
		T dst;
		interp interp{};

		constexpr T operator[](float prog) const noexcept{
			return mix_func<T>::operator()(src, dst, interp(prog));
		}
	};

	export
	struct color_gradient : gradient<graphic::color>{
		[[nodiscard]] constexpr color_gradient() noexcept = default;

		[[nodiscard]] constexpr explicit(false) color_gradient(const graphic::color color) noexcept : gradient{color, color}{
		}

		[[nodiscard]] constexpr explicit(false) color_gradient(
			const graphic::color src, const graphic::color dst,
			const graphic::interp interp = {}) noexcept : gradient{
				src, dst, interp
			}{
		}
	};

	export
	template <typename T>
	struct trivial_gradient{
		T src;
		T dst;

		constexpr T operator[](float prog) const noexcept{
			return mix_func<T>::operator()(src, dst, prog);
		}
	};
}
