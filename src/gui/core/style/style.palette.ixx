//
// Created by Matrix on 2024/10/7.
//

export module mo_yanxi.gui.style.palette;

import std;
import mo_yanxi.graphic.image_region;
import mo_yanxi.math.interpolation;
export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.graphic.color;
export import align;


export import mo_yanxi.gui.image_regions;

namespace mo_yanxi::gui{
namespace style{
export
enum struct color_blend_mode : std::uint16_t{
	replace,
	multiply,
	additive,
	screen,
	hard_light,
};

export
struct palette{
	graphic::color general{};
	graphic::color on_focus{};
	graphic::color on_press{};

	graphic::color disable{};
	graphic::color toggled{};

	color_blend_mode disable_blend_mode{};
	color_blend_mode toggled_blend_mode{};

	[[nodiscard]] constexpr palette copy() const noexcept{
		return *this;
	}

	constexpr palette& set_cursor_ignored() noexcept {
		on_focus = general;
		on_press = general;
		return *this;
	}

	constexpr palette& mul_alpha(const float alpha) noexcept{
		general.mul_a(alpha);
		on_focus.mul_a(alpha);
		on_press.mul_a(alpha);

		disable.mul_a(alpha);
		toggled.mul_a(alpha);

		return *this;
	}

	constexpr palette& mul_rgb(const float alpha) noexcept{
		general.mul_rgb(alpha);
		on_focus.mul_rgb(alpha);
		on_press.mul_rgb(alpha);

		disable.mul_rgb(alpha);
		toggled.mul_rgb(alpha);

		return *this;
	}

	constexpr palette& lerp(const palette& rhs, float factor) noexcept{
		general.lerp(rhs.general, factor);
		on_focus.lerp(rhs.on_focus, factor);
		on_press.lerp(rhs.on_press, factor);
		disable.lerp(rhs.disable, factor);
		toggled.lerp(rhs.toggled, factor);
		return *this;
	}

	constexpr friend palette lerp(const palette& lhs, const palette& rhs, float factor) noexcept{
		return lhs.copy().lerp(rhs, factor);
	}

	[[nodiscard]] graphic::color on_instance(const elem& element) const{
		graphic::color color;
		auto& state = element.cursor_state();
		if(state.pressed){
			color = on_press;
		} else if(state.time_focus > 0.f){
			color = general.create_lerp(on_focus, state.get_factor_of(&cursor_states::time_focus) | math::interp::smooth);
		} else{
			color = general;
		}

		constexpr static auto apply_blend = [](graphic::color& dst, graphic::color src, color_blend_mode mode){
			switch(mode){
			case color_blend_mode::replace : dst = src;
				return;
			case color_blend_mode::multiply : dst *= src;
				return;
			case color_blend_mode::additive : dst += src;
				return;
			case color_blend_mode::screen :{
				auto alpha = (src.a + dst.a) * .5f;
				dst = dst + src - dst * src;
				dst.a = alpha;
				return;
			}case color_blend_mode::hard_light :{
				const float out_a = src.a + dst.a * (1.0f - src.a);

				if(out_a == 0.0f){
					dst = {};
					return;
				}

				constexpr static auto hardLightChannel = [](float src, float dst) -> float{
					if(src <= 0.5f){
						return 2.0f * src * dst;
					}
					return 1.0f - 2.0f * (1.0f - src) * (1.0f - dst);
				};

				auto blendWithAlpha = [&](float sc, float dc){
					const auto blended = hardLightChannel(sc, dc);
					const auto val = (src.a * dst.a * blended + sc * src.a * (1.0f - dst.a) + dc * dst.a * (1.0f - src.
						a)) / out_a;
					return std::clamp(val, 0.0f, 1.0f);
				};

				const auto out_r = blendWithAlpha(src.r, dst.r);
				const auto out_g = blendWithAlpha(src.g, dst.g);
				const auto out_b = blendWithAlpha(src.b, dst.b);

				dst = {out_r, out_g, out_b, out_a};
				return;
			}
			default : std::unreachable();
			}
		};

		if(element.is_disabled()){
			apply_blend(color, disable, disable_blend_mode);
		} else if(element.is_toggled()){
			apply_blend(color, toggled, toggled_blend_mode);
		}

		return color.mul_a(element.get_draw_opacity());
	}
};

export
[[nodiscard]] constexpr palette make_theme_palette(
	const graphic::color theme_color,
	const color_blend_mode disable_mode = color_blend_mode::replace,
	const color_blend_mode toggled_mode = color_blend_mode::replace) noexcept {

	return palette{
		// general: 基础主题色
		.general = theme_color,

		// on_focus: 亮感大于 general，通过提升明度并微降饱和度，保持颜色通透不刺眼
		.on_focus = theme_color.copy().shift_value(0.1f).shift_saturation(-0.02f),

		// on_press: 亮感最高，进一步提升明度并适当降低饱和度模拟高光按压
		.on_press = theme_color.copy().shift_value(0.15f).shift_saturation(-0.065f),

		// disable: 比 general 更灰且暗，大幅抽离饱和度并压低明度
		.disable = theme_color.copy().shift_saturation(-0.60f).shift_value(-0.30f),

		// toggled: 亮度介于 on_focus 和 on_press 之间
		.toggled = theme_color.copy().shift_value(0.13f).shift_saturation(-0.05f),

		.disable_blend_mode = disable_mode,
		.toggled_blend_mode = toggled_mode
	};
}

export
struct component_palette{
	palette background;
	palette border;
};

[[nodiscard]] constexpr palette make_palette(const std::string_view gen,
	const std::string_view foc,
	const std::string_view press,
	const std::string_view dis) noexcept{
	return palette{
			.general = graphic::color::from_string(gen),
			.on_focus = graphic::color::from_string(foc),
			.on_press = graphic::color::from_string(press),
			.disable = graphic::color::from_string(dis),
			.toggled = graphic::color::from_string(press), // 默认触发状态同按下状态
			.disable_blend_mode = color_blend_mode::replace,
			.toggled_blend_mode = color_blend_mode::replace
		};
}
[[nodiscard]] constexpr palette make_palette(
	const std::uint32_t gen,
	const std::uint32_t foc,
	const std::uint32_t press,
	const std::uint32_t dis) noexcept{
	return palette{
			.general = graphic::color::from_rgba8888(gen),
			.on_focus = graphic::color::from_rgba8888(foc),
			.on_press = graphic::color::from_rgba8888(press),
			.disable = graphic::color::from_rgba8888(dis),
			.toggled = graphic::color::from_rgba8888(press), // 默认触发状态同按下状态
			.disable_blend_mode = color_blend_mode::replace,
			.toggled_blend_mode = color_blend_mode::replace
		};
}

[[nodiscard]] constexpr palette make_palette(
	const graphic::color gen,
	const graphic::color foc,
	const graphic::color press,
	const graphic::color dis) noexcept{
	return palette{
			.general = gen,
			.on_focus = foc,
			.on_press = press,
			.disable = dis,
			.toggled = press, // 默认触发状态同按下状态
			.disable_blend_mode = color_blend_mode::replace,
			.toggled_blend_mode = color_blend_mode::replace
		};
}


export namespace pal{
inline constexpr palette dark = make_palette(
	graphic::colors::dark_gray.create_lerp(graphic::colors::black, .75f),
	graphic::colors::dark_gray.create_lerp(graphic::colors::black, .4f),
	graphic::colors::dark_gray.create_lerp(graphic::colors::black, .2f),
	graphic::colors::dark_gray.create_lerp(graphic::colors::black, .95f));

inline constexpr palette white = make_palette(0XF2F4F7FF, 0XF9FAFBFF, 0XFFFFFFFF, 0XE4E7ECFF);

inline constexpr palette pastel_white = make_theme_palette(
	graphic::color::from_rgba8888(0xFFFFFFFF)
);

// 2. 基础灰 (Light Gray)
inline constexpr palette pastel_gray = make_theme_palette(
	graphic::color::from_rgba8888(0xF0F0F0FF)
);

// 3. 基础蓝 (Light Blue)
inline constexpr palette pastel_blue = make_theme_palette(
	graphic::color::from_rgba8888(0xD8E4FFFF)
);

// 4. 基础绿 (Light Green)
inline constexpr palette pastel_green = make_theme_palette(
	graphic::color::from_rgba8888(0xD4E8D4FF)
);

// 5. 基础橙 (Light Orange)
inline constexpr palette pastel_orange = make_theme_palette(
	graphic::color::from_rgba8888(0xFFE6CCFF)
);

// 6. 基础黄 (Light Yellow)
inline constexpr palette pastel_yellow = make_theme_palette(
	graphic::color::from_rgba8888(0xFFF2CCFF)
);

// 7. 基础粉红 (Light Pink)
inline constexpr palette pastel_pink = make_theme_palette(
	graphic::color::from_rgba8888(0xF8CECCFF)
);

// 8. 基础紫 (Light Purple)
inline constexpr palette pastel_purple = make_theme_palette(
	graphic::color::from_rgba8888(0xE1D5E7FF)
);
}

export
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
}
