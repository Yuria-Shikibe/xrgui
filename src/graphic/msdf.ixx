module;

#define MSDFGEN_USE_CPP11
#include <msdfgen/msdfgen.h>
#include <msdfgen/msdfgen-ext.h>

export module mo_yanxi.graphic.msdf;

export import mo_yanxi.bitmap;

import mo_yanxi.math;
import std;

namespace mo_yanxi::graphic::msdf{
export constexpr inline int sdf_image_boarder = 4;
export constexpr inline double sdf_image_range = 4.;

export msdfgen::FreetypeHandle* HACK_get_ft_library_from(const void* address_of_ft_lib) noexcept{
	return static_cast<msdfgen::FreetypeHandle*>(const_cast<void*>(address_of_ft_lib));
}

export msdfgen::FontHandle* HACK_get_face_from(void* address_of_ft_face) noexcept{
	return static_cast<msdfgen::FontHandle*>(address_of_ft_face);
}

export
struct svg_info{
	msdfgen::Shape shape{};
	math::vec2 size{};
};

export
[[nodiscard]] bitmap load_shape(
	const svg_info& shape,
	unsigned w, unsigned h,
	double range = sdf_image_range,
	int boarder = sdf_image_boarder);


export
[[nodiscard]] bitmap load_glyph(
	msdfgen::FontHandle* face,
	msdfgen::GlyphIndex code,
	unsigned target_w,
	unsigned target_h,
	int boarder,
	double font_w,
	double font_h,
	double range = sdf_image_range
);

enum struct msdf_generator_state{
	done,
	path,
	memory,
};

export
struct msdf_generator{
private:
	std::string path{};
	mutable svg_info shape{};
	mutable msdf_generator_state state_{};

public:
	double range = sdf_image_range;
	int boarder = sdf_image_boarder;

	[[nodiscard]] const svg_info& get_shape() const;

	math::vec2 get_extent() const noexcept;

	[[nodiscard]] msdf_generator() = default;

	[[nodiscard]] msdf_generator(svg_info&& shape, double range = sdf_image_range, int boarder = sdf_image_boarder);

	[[nodiscard]] msdf_generator(std::string str, bool is_memory_data = false, double range = sdf_image_range, int boarder = sdf_image_boarder);

	bitmap operator ()(const unsigned w, const unsigned h, const unsigned mip_lv) const{
		auto scl = 1u << mip_lv;
		auto b = boarder / scl;
		if(b * 2 >= w || b * 2 >= h){
			b = 0;
		}
		return load_shape(get_shape(), w - b * 2, h - b * 2, math::max(range / static_cast<double>(scl), 0.25), b);
	}

	bitmap operator ()(const unsigned w, const unsigned h) const{
		return load_shape(get_shape(), w, h, range, boarder);
	}
};

struct msdf_glyph_generator_base{
	//no lock to this face sine image loads from the same thread
	msdfgen::FontHandle* face{};
	unsigned font_w{};
	unsigned font_h{};
	double range = 0.4;
	unsigned boarder = sdf_image_boarder;
};

export
struct msdf_glyph_generator_crop : msdf_glyph_generator_base{
	msdfgen::GlyphIndex code{};

	[[nodiscard]] msdf_glyph_generator_crop(const msdf_glyph_generator_base& base, msdfgen::GlyphIndex code)
	: msdf_glyph_generator_base{base}, code(code){
	}

	bitmap operator()(const unsigned w, const unsigned h, const unsigned mip_lv) const{
		auto scl = 1 << mip_lv;
		auto b = boarder / scl;
		if(b * 2 >= w || b * 2 >= h){
			b = 0;
		}
		return load_glyph(face, code, w - b * 2, h - b * 2, b,
			static_cast<double>(font_w) / static_cast<double>(scl),
			static_cast<double>(font_h) / static_cast<double>(scl), range);
	};
};

export
struct msdf_glyph_generator : msdf_glyph_generator_base{
	[[nodiscard]] msdf_glyph_generator_crop crop(msdfgen::GlyphIndex code) const noexcept{
		return {*this, code};
	}
};

constexpr unsigned boarder_size = 128;
constexpr double boarder_range = 4;

export
svg_info create_boarder(double radius = 15., double width = 2., double k = .7f);

export
svg_info create_solid_boarder(double radius = 15., double k = .7f);

/**
 * @brief 创建超椭圆 (更平滑的胶囊/圆角矩形)
 * @param width 总宽度
 * @param height 总高度
 * @param exponent 指数 n。n=2 是椭圆，n>2 趋向矩形。
 * 对于像胶囊一样但更平滑的形状，推荐 3.0 到 5.0 之间。
 * 如果想要非常接近矩形但转角极其平滑，尝试 4.0 或更高。
 * @return svg_info
 */
export
svg_info create_capsule_smooth(double width, double height, double exponent = 3.5);

/**
	* @brief 创建标准胶囊形 (两头半圆，中间矩形)
	* @param r 半圆半径 (radius)
	* @param w 中间矩形的宽度 (rect width)，即两个圆心的距离
	* @return svg_info
*/
export
svg_info create_capsule(double r, double w);


}
