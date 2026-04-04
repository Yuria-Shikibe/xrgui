//
// Created by Matrix on 2024/10/7.
//

export module mo_yanxi.gui.style.palette;

import std;
import mo_yanxi.graphic.image_region;
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

	[[nodiscard]] graphic::color on_instance(const elem& element) const{
		graphic::color color;
		if(element.cursor_state().pressed){
			color = on_press;
		} else if(element.cursor_state().focused){
			color = on_focus;
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
		.on_focus = theme_color.copy().shift_value(0.15f).shift_saturation(-0.05f),

		// on_press: 亮感最高，进一步提升明度并适当降低饱和度模拟高光按压
		.on_press = theme_color.copy().shift_value(0.25f).shift_saturation(-0.10f),

		// disable: 比 general 更灰且暗，大幅抽离饱和度并压低明度
		.disable = theme_color.copy().shift_saturation(-0.60f).shift_value(-0.30f),

		// toggled: 亮度介于 on_focus 和 on_press 之间
		.toggled = theme_color.copy().shift_value(0.20f).shift_saturation(-0.08f),

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

// ==========================================
// 莫兰迪 / 马卡龙色系预设定义
// ==========================================

constexpr graphic::color c = graphic::color::from_string("F2F4F7");

export namespace pal{
	inline constexpr palette dark = make_palette(
		graphic::colors::dark_gray.create_lerp(graphic::colors::black, .75f),
		graphic::colors::dark_gray.create_lerp(graphic::colors::black, .35f),
		graphic::colors::dark_gray,
		graphic::colors::dark_gray.create_lerp(graphic::colors::black, .95f));

	inline constexpr component_palette white{
		.background = make_palette("F2F4F7", "F9FAFB", "FFFFFF", "E4E7EC"),
		.border = make_palette("D0D5DD", "E4E7EC", "F2F4F7", "98A2B3")
	};

	// 2. 豆沙绿 (清新不刺眼的微灰绿色)
	inline constexpr component_palette green{
			.background = make_palette("E3EAE5", "EDF2EF", "F6F9F7", "CED6D1"),
			.border = make_palette("A7BBAE", "C2D1C8", "DDE5E0", "8C9A91")
		};

	// 3. 灰雾蓝 (宁静的低保饱和度蓝)
	inline constexpr component_palette blue{
			.background = make_palette("8EA1F5", "ECF0F6", "F5F8FA", "CCD4DF"),
			.border = make_palette("A5BCF5", "BCC9D9", "D6E0EC", "8997AB")
		};

	// 4. 奶酪黄 (偏暖调的奶油色，不荧光)
	inline constexpr component_palette yellow{
			.background = make_palette("F5EFE1", "FAF6EE", "FDFBF8", "DFD8C9"),
			.border = make_palette("D4C4A1", "E5D9BF", "F1EBD8", "B8AB8C")
		};

	// 5. 蜜桃橙 (带有粉调的低灰度橙色)
	inline constexpr component_palette orange{
			.background = make_palette("F4E6E0", "F9EFEA", "FDF8F5", "DECBC4"),
			.border = make_palette("D5AEA2", "E5C8BF", "F0E0DB", "B8958B")
		};

	// 6. 枯木粉红 (莫兰迪红，内敛柔和)
	inline constexpr component_palette red{
			.background = make_palette("F2E1E1", "F8EDED", "FDF7F7", "DAC4C4"),
			.border = make_palette("D4A7A7", "E6C2C2", "F2DCDC", "B88D8D")
		};

	// 7. 丁香紫 (带一点灰度的浅紫色)
	inline constexpr component_palette purple{
			.background = make_palette("EAE3ED", "F1EDF3", "F8F6F9", "D0C8D4"),
			.border = make_palette("BCA7C4", "D1C2D8", "E5DBEB", "A08AAB")
		};

	inline constexpr palette general_palette{
			graphic::colors::light_gray,
			graphic::colors::white,
			graphic::colors::aqua,
			graphic::colors::gray,
			graphic::colors::white,
		};
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

//
// struct round_style : style_drawer<elem>{
// 	align::spacing boarder{default_boarder};
// 	palette_with<graphic::image_nine_region> base{};
// 	palette_with<graphic::image_nine_region> edge{};
// 	palette_with<graphic::image_nine_region> back{};
//
// 	float disabledOpacity{.5f};
//
// 	void draw(const elem& element, math::frect region, float opacityScl) const override;
//
// 	[[nodiscard]] float content_opacity(const elem& element) const override;
//
// 	bool apply_to(elem& element) const override;
// };
}
}
