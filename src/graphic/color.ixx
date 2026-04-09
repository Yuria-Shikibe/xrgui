module;

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.graphic.color;

import std;
import mo_yanxi.math;
export import mo_yanxi.math.vector4;

namespace mo_yanxi::graphic{
//TODO clean up

/**
 * \brief  32Bits for 4 u byte[0, 255]
 * \code
 * 00000000__00000000__00000000__00000000
 * r value^  g value^  b value^  a value^
 *       24        16         8         0
 * \endcode
 *
 * TODO HDR support
 */

export
struct hsv{
	float h, s, v;

	constexpr friend hsv lerp(const hsv& a, const hsv& b, float t) noexcept {
		float new_s = a.s + t * (b.s - a.s);
		float new_v = a.v + t * (b.v - a.v);

		float delta_h = b.h - a.h;

		if (delta_h > 0.5f) {
			delta_h -= 1.0f;
		}
		else if (delta_h < -0.5f) {
			delta_h += 1.0f;
		}

		float new_h = a.h + t * delta_h;

		new_h = new_h - math::floor(new_h);

		return {new_h, new_s, new_v};
	}
};

export
struct color : math::vector4<float>{
public:
	static constexpr float max_luma_scale = 3.f;
	static constexpr float light_color_range = 2550.f;

	static constexpr auto max_val = std::numeric_limits<std::uint8_t>::max();
	static constexpr float max_val_f = std::numeric_limits<std::uint8_t>::max();
	static constexpr std::uint32_t r_Offset = 24;
	static constexpr std::uint32_t g_Offset = 16;
	static constexpr std::uint32_t b_Offset = 8;
	static constexpr std::uint32_t a_Offset = 0;
	static constexpr std::uint32_t a_Mask = 0x00'00'00'ff;
	static constexpr std::uint32_t b_Mask = 0x00'00'ff'00;
	static constexpr std::uint32_t g_Mask = 0x00'ff'00'00;
	static constexpr std::uint32_t r_Mask = 0xff'00'00'00;

	using rgba8_bits = std::uint32_t;

public:
	FORCE_INLINE constexpr color& set_light(float lumaScl = max_luma_scale) noexcept{
		return mul_rgb(lumaScl);
	}

	[[nodiscard]] FORCE_INLINE constexpr color to_light_by_luma(float target) const noexcept{
		auto luma = r * 0.299 + g * 0.587 + b * 0.114;
		if(luma < std::numeric_limits<float>::epsilon()){
			return {};
		}
		return copy().mul_rgb(target / luma);
	}

	[[nodiscard]] FORCE_INLINE constexpr color to_light(float lumaScl = max_luma_scale) const noexcept{
		return copy().set_light(lumaScl);
	}

	[[nodiscard]] FORCE_INLINE constexpr color to_neutralize_light(float factor = .65f) const noexcept{
		return copy().mul_rgb(factor);
	}

	[[nodiscard]] FORCE_INLINE constexpr color to_neutralize_light_def() const noexcept{
		return to_neutralize_light(.65f);
	}


	FORCE_INLINE friend constexpr std::size_t hash_value(const color& obj){
		return obj.hash_value();
	}

	FORCE_INLINE friend std::ostream& operator<<(std::ostream& os, const color& obj){
		return os << obj.to_string();
	}

	auto make_transparent(this auto self) noexcept {
		self.a = 0;
		return self;
	}

	constexpr friend bool operator==(const color& lhs, const color& rhs) noexcept = default;

	static constexpr auto string_to_rgba(const std::string_view hexStr) noexcept{
		std::array<std::uint8_t, 4> rgba{};
		for(const auto& [index, v1] : hexStr
		    | std::views::slide(2)
		    | std::views::stride(2)
		    | std::views::take(4)
		    | std::views::enumerate){
			std::from_chars(v1.data(), v1.data() + v1.size(), rgba[index], 16);
		}

		if(hexStr.size() <= 6){
			rgba[3] = std::numeric_limits<std::uint8_t>::max();
		}

		return rgba;
	}

	static constexpr rgba8_bits string_to_rgba_bits(const std::string_view hexStr) noexcept{
		auto value = std::bit_cast<rgba8_bits>(string_to_rgba(hexStr));
		if constexpr(std::endian::native == std::endian::little){
			value = std::byteswap(value);
		}

		return value;
	}

	static constexpr color from_string(const std::string_view hexStr){
		const auto rgba = string_to_rgba(hexStr);

		color color{
				static_cast<float>(rgba[0]) / static_cast<float>(std::numeric_limits<std::uint8_t>::max()),
				static_cast<float>(rgba[1]) / static_cast<float>(std::numeric_limits<std::uint8_t>::max()),
				static_cast<float>(rgba[2]) / static_cast<float>(std::numeric_limits<std::uint8_t>::max()),
				static_cast<float>(rgba[3]) / static_cast<float>(std::numeric_limits<std::uint8_t>::max()),
			};

		return color;
	}

	FORCE_INLINE static constexpr rgba8_bits rgb888(const float r, const float g, const float b) noexcept{
		return static_cast<rgba8_bits>(r * max_val) << 16 | static_cast<rgba8_bits>(g * max_val) << 8 | static_cast<
			rgba8_bits>(b * max_val);
	}

	FORCE_INLINE static constexpr rgba8_bits rgba8888(const float r, const float g, const float b,
		const float a) noexcept{
		return
			((static_cast<rgba8_bits>(r * max_val) << r_Offset) & r_Mask) |
			((static_cast<rgba8_bits>(g * max_val) << g_Offset) & g_Mask) |
			((static_cast<rgba8_bits>(b * max_val) << b_Offset) & b_Mask) |
			((static_cast<rgba8_bits>(a * max_val) << a_Offset) & a_Mask);
	}

	[[nodiscard]] FORCE_INLINE constexpr rgba8_bits to_rgb888() const noexcept{
		return rgb888(r, g, b);
	}

	[[nodiscard]] FORCE_INLINE constexpr rgba8_bits to_rgba8888() const noexcept{
		return rgba8888(r, g, b, a);
	}

	[[nodiscard]] FORCE_INLINE constexpr rgba8_bits to_rgba() const noexcept{
		return to_rgba8888();
	}

	FORCE_INLINE static constexpr color from_rgb888(rgba8_bits value, float a = 1) noexcept{
		color c{};
		value <<= 8u;
		c.r = static_cast<float>(value >> r_Offset & max_val) / max_val_f;
		c.g = static_cast<float>(value >> g_Offset & max_val) / max_val_f;
		c.b = static_cast<float>(value >> b_Offset & max_val) / max_val_f;
		c.a = a;
		return c;
	}

	FORCE_INLINE static constexpr color from_rgba8888(const rgba8_bits value) noexcept{
		color c{};
		c.r = static_cast<float>(value >> r_Offset & max_val) / max_val_f;
		c.g = static_cast<float>(value >> g_Offset & max_val) / max_val_f;
		c.b = static_cast<float>(value >> b_Offset & max_val) / max_val_f;
		c.a = static_cast<float>(value >> a_Offset & max_val) / max_val_f;
		return c;
	}

	[[nodiscard]] FORCE_INLINE constexpr float diff(const color& other) const noexcept{
		const auto lhsv = to_hsv();
		const auto rhsv = other.to_hsv();

		// 色相是环形的 (0.0 与 1.0 首尾相连)，需要计算环形最短距离
		const float h_diff = math::abs(lhsv.h - rhsv.h);
		const float h_dist = math::min(h_diff, 1.0f - h_diff);

		return h_dist + math::abs(lhsv.s - rhsv.s) + math::abs(lhsv.v - rhsv.v);
	}

	FORCE_INLINE constexpr color& set(const color& color) noexcept{
		return this->operator=(color);
	}

	//TODO operator overload?


	FORCE_INLINE constexpr color& mul(const color& color) noexcept{
		this->r *= color.r;
		this->g *= color.g;
		this->b *= color.b;
		this->a *= color.a;
		return *this;
	}


	FORCE_INLINE constexpr color& mul_rgb(const float value) noexcept{
		this->r *= value;
		this->g *= value;
		this->b *= value;
		return *this;
	}


	FORCE_INLINE constexpr color& mul_rgba(const float value) noexcept{
		this->r *= value;
		this->g *= value;
		this->b *= value;
		this->a *= value;
		return *this;
	}


	FORCE_INLINE constexpr color& add(const color& color) noexcept{
		this->r += color.r;
		this->g += color.g;
		this->b += color.b;
		return *this;
	}


	FORCE_INLINE constexpr color& sub(const color& color) noexcept{
		this->r -= color.r;
		this->g -= color.g;
		this->b -= color.b;
		return *this;
	}


	FORCE_INLINE constexpr color& set(const float tr, const float tg, const float tb, const float ta) noexcept{
		this->r = tr;
		this->g = tg;
		this->b = tb;
		this->a = ta;
		return *this;
	}


	FORCE_INLINE constexpr color& set(const float tr, const float tg, const float tb) noexcept{
		this->r = tr;
		this->g = tg;
		this->b = tb;
		return *this;
	}

	FORCE_INLINE constexpr color& set(const rgba8_bits rgba) noexcept{
		return *this = from_rgba8888(rgba);
	}

	[[nodiscard]] FORCE_INLINE constexpr float sum() const noexcept{
		return r + g + b;
	}


	FORCE_INLINE constexpr color& add(const float tr, const float tg, const float tb, const float ta) noexcept{
		this->r += tr;
		this->g += tg;
		this->b += tb;
		this->a += ta;
		return *this;
	}


	FORCE_INLINE constexpr color& add(const float tr, const float tg, const float tb) noexcept{
		this->r += tr;
		this->g += tg;
		this->b += tb;
		return *this;
	}


	FORCE_INLINE constexpr color& sub(const float tr, const float tg, const float tb, const float ta) noexcept{
		this->r -= tr;
		this->g -= tg;
		this->b -= tb;
		this->a -= ta;
		return *this;
	}


	FORCE_INLINE constexpr color& sub(const float tr, const float tg, const float tb) noexcept{
		this->r -= tr;
		this->g -= tg;
		this->b -= tb;
		return *this;
	}

	FORCE_INLINE constexpr color& inv_rgb(){
		r = 1 - r;
		g = 1 - g;
		b = 1 - b;
		return *this;
	}

	FORCE_INLINE constexpr color& setR(const float tr) noexcept{
		this->r -= tr;
		return *this;
	}

	FORCE_INLINE constexpr color& setG(const float tg) noexcept{
		this->g -= tg;
		return *this;
	}

	FORCE_INLINE constexpr color& setB(const float tb) noexcept{
		this->b -= tb;
		return *this;
	}

	FORCE_INLINE constexpr color& set_a(const float ta) noexcept{
		this->a = ta;
		return *this;
	}

	FORCE_INLINE constexpr color copy_set_a(this color self, const float ta) noexcept{
		self.a = ta;
		return self;
	}

	FORCE_INLINE constexpr color& mul_a(const float ta) noexcept{
		this->a *= ta;
		return *this;
	}


	FORCE_INLINE constexpr color& mul(const float tr, const float tg, const float tb, const float ta) noexcept{
		this->r *= tr;
		this->g *= tg;
		this->b *= tb;
		this->a *= ta;
		return *this;
	}


	FORCE_INLINE constexpr color& mul(const float val) noexcept{
		this->r *= val;
		this->g *= val;
		this->b *= val;
		this->a *= val;
		return *this;
	}

	using vector4::lerp;

	//
	// FORCE_INLINE constexpr color& lerp(const color& target, const float t) noexcept{
	// 	math::lerp_inplace(r, target.r, t);
	// 	math::lerp_inplace(g, target.g, t);
	// 	math::lerp_inplace(b, target.b, t);
	// 	math::lerp_inplace(a, target.a, t);
	// 	return *this;
	// }

	FORCE_INLINE constexpr color& lerpRGB(const color& target, const float t) noexcept{
		math::lerp_inplace(r, target.r, t);
		math::lerp_inplace(g, target.g, t);
		math::lerp_inplace(b, target.b, t);
		return *this;
	}

	[[nodiscard]] FORCE_INLINE constexpr color create_lerp(const color& target, const float t) const noexcept{
		return {
				math::lerp(r, target.r, t),
				math::lerp(g, target.g, t),
				math::lerp(b, target.b, t),
				math::lerp(a, target.a, t)
			};
	}

	FORCE_INLINE constexpr color& lerp(const float tr, const float tg, const float tb, const float ta,
		const float t) noexcept{
		this->r += t * (tr - this->r);
		this->g += t * (tg - this->g);
		this->b += t * (tb - this->b);
		this->a += t * (ta - this->a);
		return *this;
	}

	FORCE_INLINE constexpr color& mul_self_alpha() noexcept{
		r *= a;
		g *= a;
		b *= a;
		return *this;
	}

	FORCE_INLINE constexpr color& write(color& to) const noexcept{
		return to.set(*this);
	}

	//TODO independent hsv type?
	using hsv_t = std::array<float, 3>;

	[[nodiscard]] FORCE_INLINE constexpr float hue() const noexcept{
		return to_hsv().h;
	}

	[[nodiscard]] FORCE_INLINE constexpr float saturation() const noexcept{
		return to_hsv().s;
	}

	[[nodiscard]] FORCE_INLINE constexpr float value() const noexcept{
		return to_hsv().v;
	}


	FORCE_INLINE constexpr color& shift_hue(const float amount) noexcept{
		auto TMP_HSV = to_hsv();
		TMP_HSV.h += amount;
		from_hsv(TMP_HSV);
		return *this;
	}

	FORCE_INLINE constexpr color& shift_saturation(const float amount) noexcept{
		auto TMP_HSV = to_hsv();
		TMP_HSV.s += amount;
		from_hsv(TMP_HSV);
		return *this;
	}

	FORCE_INLINE constexpr color& shift_value(const float amount) noexcept{
		auto TMP_HSV = to_hsv();
		TMP_HSV.v += amount;
		from_hsv(TMP_HSV);
		return *this;
	}

	[[nodiscard]] FORCE_INLINE constexpr std::size_t hash_value() const noexcept{
		return to_rgba8888();
	}

	[[nodiscard]] FORCE_INLINE constexpr rgba8_bits abgr() const noexcept{
		return static_cast<rgba8_bits>(255 * a) << 24 | static_cast<rgba8_bits>(255 * b) << 16 | static_cast<
			rgba8_bits>(255 * g) << 8 | static_cast<rgba8_bits>(255 * r);
	}

	[[nodiscard]] FORCE_INLINE std::string to_string() const{
		return std::format("{:02X}{:02X}{:02X}{:02X}", static_cast<rgba8_bits>(max_val * r),
			static_cast<rgba8_bits>(max_val * g), static_cast<rgba8_bits>(max_val * b),
			static_cast<rgba8_bits>(max_val * a));;
	}

	// ---------------------------------------------------------
	// 优化后的 to_hsv
	// ---------------------------------------------------------
	[[nodiscard]] FORCE_INLINE constexpr hsv to_hsv() const noexcept{
    hsv hsv{0.0f, 0.0f, 0.0f};
    const float maxV = math::max(math::max(r, g), b);
    const float minV = math::min(math::min(r, g), b);
    const float range = maxV - minV;

    hsv.v = maxV;

    // 如果亮度为0，直接返回纯黑
    if(maxV <= 0.0f){
       return hsv;
    }

    hsv.s = range / maxV;

    // 如果没有色差（饱和度为0的纯灰色），色相为0
    if(range <= 0.0f){
       return hsv;
    }

    // 预计算除数常量 (相当于 1.0f / (6.0f * range))
    // 对应原本公式中除以 360 度的归一化处理
    const float inv_range_6 = 1.0f / (6.0f * range);

    if(maxV == r){
       hsv.h = (g - b) * inv_range_6;
       if(hsv.h < 0.0f) hsv.h += 1.0f;
    } else if(maxV == g){
       hsv.h = (b - r) * inv_range_6 + (1.0f / 3.0f); // 1/3 对应 120 度
    } else{
       hsv.h = (r - g) * inv_range_6 + (2.0f / 3.0f); // 2/3 对应 240 度
    }

    return hsv;
}

// ---------------------------------------------------------
// 优化后的 from_hsv (使用 [0, 1] 归一化输入)
// ---------------------------------------------------------
FORCE_INLINE constexpr color& from_hsv(const hsv val) noexcept{
    auto [h, s, v] = val;
    // Fast-path: 饱和度为 0 时直接是灰阶
    if(s <= 0.0f){
       r = v;
       g = v;
       b = v;
       return clamp();
    }

    // 规范化 hue 到 [0, 1) 之间
    float norm_h = math::mod(h, 1.0f);
    if(norm_h < 0.0f) norm_h += 1.0f;

    // 映射到 [0, 6) 用于计算六边形的 6 个区间
    norm_h *= 6.0f;

    const int i = static_cast<int>(norm_h);
    const float f = norm_h - static_cast<float>(i);

    const float p = v * (1.0f - s);
    const float q = v * (1.0f - s * f);
    const float t = v * (1.0f - s * (1.0f - f));

    switch(i){
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default:r = v; g = p; b = q; break; // case 5
    }

    return clamp();
}

// ---------------------------------------------------------
// 极速修改色相 (Hue)
// ---------------------------------------------------------
FORCE_INLINE constexpr color& set_hue(const float target_h) noexcept{
    const float current_v = math::max(math::max(r, g), b);
    const float current_min = math::min(math::min(r, g), b);

    // 纯黑没有色相，直接返回
    if(current_v <= 0.0f) return *this;

    const float current_range = current_v - current_min;
    // 纯灰阶(无饱和度)没有色相，修改色相无意义，直接返回
    if(current_range <= 0.0f) return *this;

    // 仅计算 S 和 V，跳过当前 H 的昂贵计算，直接重新生成 RGB
    // 注意：这里的 target_h 现在预期是 [0, 1] 的值
    const float current_s = current_range / current_v;
    return from_hsv({target_h, current_s, current_v});
}

	// ---------------------------------------------------------
	// 极速修改饱和度 (Saturation)
	// ---------------------------------------------------------
	FORCE_INLINE constexpr color& set_saturation(const float target_s) noexcept{
		const float current_v = math::max(math::max(r, g), b);

		if(current_v <= 0.0f) return *this; // 纯黑没有饱和度

		const float current_min = math::min(math::min(r, g), b);
		const float current_range = current_v - current_min;

		if(current_range <= 0.0f) return *this; // 灰阶无法凭空捏造出颜色倾向

		// 通过线性变换直接在 RGB 空间缩放饱和度，避免了 H 的计算与重分配
		// 推导：scale = target_s / current_s = target_s / (current_range / current_v)
		const float scale = (target_s * current_v) / current_range;

		r = current_v + (r - current_v) * scale;
		g = current_v + (g - current_v) * scale;
		b = current_v + (b - current_v) * scale;

		return clamp();
	}

	// ---------------------------------------------------------
	// 极速修改明度 (Value)
	// ---------------------------------------------------------
	FORCE_INLINE constexpr color& set_value(const float target_v) noexcept{
		const float current_v = math::max(math::max(r, g), b);

		// 如果原本是黑色，则将明度赋予为纯灰阶
		if(current_v <= 0.0f){
			r = target_v;
			g = target_v;
			b = target_v;
			return clamp();
		}

		// 按比例缩放所有通道即可完美保持 H 和 S
		const float scale = target_v / current_v;
		r *= scale;
		g *= scale;
		b *= scale;

		return clamp();
	}

	[[nodiscard]] FORCE_INLINE constexpr bool equals(const color& other) const noexcept{
		static constexpr float tolerance = 0.5f / max_val_f;
		return
			math::equal(r, other.r, tolerance) &&
			math::equal(g, other.g, tolerance) &&
			math::equal(b, other.b, tolerance) &&
			math::equal(a, other.a, tolerance);
	}

	template <std::ranges::random_access_range Rng>
		requires (std::ranges::sized_range<Rng> && std::convertible_to<
			const color&, std::ranges::range_value_t<Rng>>)
	[[nodiscard]] static constexpr color from_lerp_span(float s, const Rng& colors) noexcept{
		s = math::clamp(s);
		const std::size_t size = std::ranges::size(colors);
		const std::size_t bound = size - 1;

		const auto boundf = static_cast<float>(bound);

		const color& ca = std::ranges::begin(colors)[static_cast<std::size_t>(s * boundf)];
		auto nextIdx = static_cast<std::size_t>(s * boundf + 1);

		if(nextIdx == size){
			return ca;
		}

		const color& cb = std::ranges::begin(colors)[nextIdx];

		const float n = s * boundf - static_cast<float>(static_cast<int>(s * boundf));
		const float i = 1.0f - n;
		return color{ca.r * i + cb.r * n, ca.g * i + cb.g * n, ca.b * i + cb.b * n, ca.a * i + cb.a * n};
	}


	[[nodiscard]] static constexpr color from_lerp_span(float s, const auto&... colors) noexcept{
		return color::from_lerp_span(s, std::array{colors...});
	}

	constexpr color& lerp_span(const float s, const auto&... colors) noexcept{
		return this->set(color::from_lerp_span(s, colors...));
	}

	template <std::ranges::random_access_range Rng>
		requires (std::ranges::sized_range<Rng> && std::convertible_to<
			const color&, std::ranges::range_value_t<Rng>>)
	constexpr color& lerp_span(const float s, const Rng& colors) noexcept{
		return this->operator=(color::lerp_span(s, colors));
	}

	[[nodiscard]] FORCE_INLINE constexpr color copy() const noexcept{
		return *this;
	}
};

namespace colors{
export{
	constexpr inline color white{1, 1, 1, 1};
	constexpr inline color light_gray{color::from_rgba8888(0xC8C8C8FF)};
	constexpr inline color gray{color::from_rgba8888(0x777777FF)};
	constexpr inline color dark_gray{color::from_rgba8888(0x3f3f3fff)};
	constexpr inline color black{0, 0, 0, 1};
	constexpr inline color clear{0, 0, 0, 0};
	constexpr inline color black_clear{0, 0, 0, 0};
	constexpr inline color white_clear{1, 1, 1, 0};
	constexpr inline color clear_light{white.to_light().set_a(0)};

	constexpr inline color BLUE{0, 0, 1, 1};
	constexpr inline color NAVY{0, 0, 0.5f, 1};
	constexpr inline color ROYAL{color::from_rgba8888(0x4169e1ff)};
	constexpr inline color SLATE{color::from_rgba8888(0x708090ff)};
	constexpr inline color SKY{color::from_rgba8888(0x87ceebff)};

	constexpr inline color aqua{color::from_rgba8888(0x85A2F3ff)};
	constexpr inline color BLUE_SKY = color::from_lerp_span(0.745f, BLUE, SKY);
	constexpr inline color AQUA_SKY = color::from_lerp_span(0.5f, aqua, SKY);

	constexpr inline color CYAN{0, 1, 1, 1};
	constexpr inline color TEAL{0, 0.5f, 0.5f, 1};

	constexpr inline color GREEN{color::from_rgba8888(0x00ff00ff)};
	constexpr inline color pale_green{color::from_rgba8888(0xa1ecabff)};
	constexpr inline color LIGHT_GREEN{color::from_rgba8888(0X62F06CFF)};
	constexpr inline color ACID{color::from_rgba8888(0x7fff00ff)};
	constexpr inline color LIME{color::from_rgba8888(0x32cd32ff)};
	constexpr inline color FOREST{color::from_rgba8888(0x228b22ff)};
	constexpr inline color OLIVE{color::from_rgba8888(0x6b8e23ff)};

	constexpr inline color YELLOW{color::from_rgba8888(0xffff00ff)};
	constexpr inline color pale_yellow = color::from_rgba8888(0XF0D456FF);
	constexpr inline color GOLD{color::from_rgba8888(0xffd700ff)};
	constexpr inline color GOLDENROD{color::from_rgba8888(0xdaa520ff)};
	constexpr inline color ORANGE{color::from_rgba8888(0xffa500ff)};
	constexpr inline color danger{color::from_rgb888(0XFFAC34)};
	constexpr inline color power{color::from_rgb888(0XF0D167)};

	constexpr inline color BROWN{color::from_rgba8888(0x8b4513ff)};
	constexpr inline color TAN{color::from_rgba8888(0xd2b48cff)};
	constexpr inline color BRICK{color::from_rgba8888(0xb22222ff)};

	constexpr inline color RED{color::from_rgba8888(0xff0000ff)};
	constexpr inline color red_dusted{color::from_rgba8888(0xDE6663ff)};
	constexpr inline color SCARLET{color::from_rgba8888(0xff341cff)};
	constexpr inline color CRIMSON{color::from_rgba8888(0xdc143cff)};
	constexpr inline color CORAL{color::from_rgba8888(0xff7f50ff)};
	constexpr inline color SALMON{color::from_rgba8888(0xfa8072ff)};
	constexpr inline color PINK{color::from_rgba8888(0xff69b4ff)};
	constexpr inline color MAGENTA{1, 0, 1, 1};

	constexpr inline color PURPLE{color::from_rgba8888(0xa020f0ff)};
	constexpr inline color VIOLET{color::from_rgba8888(0xee82eeff)};
	constexpr inline color MAROON{color::from_rgba8888(0xb03060ff)};

	constexpr inline color ENERGY{color::from_rgba8888(0XF0C743FF)};
}

namespace seq{
export constexpr inline std::array gray_green_yellow{light_gray, pale_green, pale_yellow};

template <typename T>
concept color_range = requires{
	requires std::ranges::input_range<T>;
	requires std::convertible_to<std::ranges::range_reference_t<T>, color>;
};

export
template <color_range Rng, std::invocable<std::ranges::range_reference_t<Rng>> Proj>
constexpr auto proj(Rng&& rng,
	Proj proj) noexcept(std::is_nothrow_invocable_v<Proj, std::ranges::range_reference_t<Rng>>){
	return std::ranges::transform(std::forward<Rng>(rng), std::move(proj));
}
}
}
}

export
template <>
struct ::std::hash<mo_yanxi::graphic::color>{
	size_t operator()(const mo_yanxi::graphic::color& obj) const noexcept{
		return obj.hash_value();
	}
};

export
template <>
struct ::std::formatter<mo_yanxi::graphic::color>{
	bool haveAlpha{false};
	bool haveWrapper{false};
	bool haveHexHead{false};

	constexpr auto parse(std::format_parse_context& context){
		auto it = context.begin();
		if(it == context.end()) return it;

		if(*it == '[' || *it == ']'){
			haveWrapper = true;
			++it;
		}

		if(*it == '#'){
			haveHexHead = true;
			++it;
		}
		if(*it == 'a'){
			haveAlpha = true;
			++it;
		}
		if(it != context.end() && *it != '}') throw std::format_error("Invalid format");

		return it;
	}

	template <typename OutputIt, typename CharT>
	auto format(const mo_yanxi::graphic::color& c, std::basic_format_context<OutputIt, CharT>& context) const{
		using mo_yanxi::graphic::color;
		using ColorBits = color::rgba8_bits;

		const char* prefix1 = haveWrapper ? "[" : "";
		const char* prefix2 = haveHexHead ? "#" : "";
		const char* suffix = haveWrapper ? "]" : "";

		if(haveAlpha){
			return std::format_to(context.out(), "{}{}{:02X}{:02X}{:02X}{:02X}{}",
				prefix1, prefix2,
				static_cast<ColorBits>(color::max_val * c.r),
				static_cast<ColorBits>(color::max_val * c.g),
				static_cast<ColorBits>(color::max_val * c.b),
				static_cast<ColorBits>(color::max_val * c.a),
				suffix);
		} else{
			return std::format_to(context.out(), "{}{}{:02X}{:02X}{:02X}{}",
				prefix1, prefix2,
				static_cast<ColorBits>(color::max_val * c.r),
				static_cast<ColorBits>(color::max_val * c.g),
				static_cast<ColorBits>(color::max_val * c.b),
				suffix);
		}
	}
};
