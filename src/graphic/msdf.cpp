module;

#include <nanosvg/nanosvg.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_STROKER_H
#include FT_OUTLINE_H

#define MSDFGEN_USE_CPP11
#include <msdfgen/msdfgen.h>
#include <msdfgen/msdfgen-ext.h>


#include <clipper2/clipper.h>

#include <mo_yanxi/adapted_attributes.hpp>

module mo_yanxi.graphic.msdf;

import mo_yanxi.font;
import std;

namespace{
namespace clipper2 = Clipper2Lib;

// 统一常量
constexpr double f266_to_double = 1.0 / 64.0;
constexpr float f266_to_float = 1.0f / 64.0f;
constexpr float double_to_f266 = 64.0f;
}

namespace temp_convert{
FORCE_INLINE clipper2::Point64 ft_vec_to_point64(const FT_Vector* v) noexcept{
	return clipper2::Point64(v->x, v->y);
}

FORCE_INLINE FT_Vector mid_point(const FT_Vector& a, const FT_Vector& b) noexcept{
	return FT_Vector{(a.x + b.x) / 2, (a.y + b.y) / 2};
}

FORCE_INLINE msdfgen::Point2 ft_vec_to_point2(const FT_Vector* v) noexcept{
	return msdfgen::Point2(static_cast<float>(v->x) * f266_to_float, static_cast<float>(v->y) * f266_to_float);
}

FORCE_INLINE msdfgen::Point2 point64_to_msdf(const clipper2::Point64& p) noexcept{
	return msdfgen::Point2(
		static_cast<double>(p.x) * f266_to_double,
		static_cast<double>(p.y) * f266_to_double
	);
}

FORCE_INLINE constexpr FT_Pos float_to_ft(float x) noexcept{
	return static_cast<FT_Pos>(x * double_to_f266);
}

FORCE_INLINE constexpr float ft_to_float(FT_Pos x) noexcept{
	return static_cast<float>(x) * f266_to_float;
}
}

namespace ft_decompose_impl{
using namespace temp_convert;

struct decompose_context{
	clipper2::Paths64* all_paths;
	clipper2::Path64* active_path;
	clipper2::Point64 last_point;
};

constexpr unsigned CONIC_RECURSION_LIMIT = 3;
constexpr unsigned CUBIC_RECURSION_LIMIT = 4;

template <unsigned Level, typename Iter>
FORCE_INLINE void flatten_conic_recursive(const FT_Vector& p0, const FT_Vector& p1, const FT_Vector& p2, Iter& it) noexcept {
    if constexpr (Level == 0) {
        *it = ft_vec_to_point64(&p2);
        ++it;
    } else {
        const FT_Vector p01 = mid_point(p0, p1);
        const FT_Vector p12 = mid_point(p1, p2);
        const FT_Vector p012 = mid_point(p01, p12);

        // 递归调用下一层 (Level - 1)
        ft_decompose_impl::flatten_conic_recursive<Level - 1>(p0, p01, p012, it);
        ft_decompose_impl::flatten_conic_recursive<Level - 1>(p012, p12, p2, it);
    }
}

template <unsigned Level, typename Iter>
FORCE_INLINE void flatten_cubic_recursive(const FT_Vector& p0, const FT_Vector& p1,
	const FT_Vector& p2, const FT_Vector& p3, Iter& it) noexcept{
	if constexpr(Level == 0){
		*it = ft_vec_to_point64(&p3);
        ++it;
    } else {
        const FT_Vector p01 = mid_point(p0, p1);
        const FT_Vector p12 = mid_point(p1, p2);
        const FT_Vector p23 = mid_point(p2, p3);

        const FT_Vector p012 = mid_point(p01, p12);
        const FT_Vector p123 = mid_point(p12, p23);

        const FT_Vector p0123 = mid_point(p012, p123);

        ft_decompose_impl::flatten_cubic_recursive<Level - 1>(p0, p01, p012, p0123, it);
        ft_decompose_impl::flatten_cubic_recursive<Level - 1>(p0123, p123, p23, p3, it);
    }
}

// ==========================================
// 外部调用接口
// ==========================================

void flatten_conic(const FT_Vector& p0, const FT_Vector& p1, const FT_Vector& p2,
                   clipper2::Path64& path) {
    // 编译期计算点数：2^N
    constexpr int COUNT = 1 << CONIC_RECURSION_LIMIT;

    // 栈上分配，零开销
    std::array<clipper2::Point64, COUNT> buffer;

    auto it = buffer.begin();
    // 启动编译期递归展开
    flatten_conic_recursive<CONIC_RECURSION_LIMIT>(p0, p1, p2, it);

    // 批量拷入 path，利用 vector 的 insert 优化
    path.insert(path.end(), buffer.begin(), buffer.end());
}

void flatten_cubic(const FT_Vector& p0, const FT_Vector& p1,
                   const FT_Vector& p2, const FT_Vector& p3,
                   clipper2::Path64& path) {
    // 编译期计算点数：2^N
    constexpr int COUNT = 1 << CUBIC_RECURSION_LIMIT;

    std::array<clipper2::Point64, COUNT> buffer;

    auto it = buffer.begin();
    flatten_cubic_recursive<CUBIC_RECURSION_LIMIT>(p0, p1, p2, p3, it);

    path.insert(path.end(), buffer.begin(), buffer.end());
}

clipper2::Paths64 convert_and_fix_outline(FT_Outline& outline){
	clipper2::Paths64 raw_paths;
	raw_paths.reserve(outline.n_contours);

	decompose_context ctx;
	ctx.all_paths = &raw_paths;
	ctx.active_path = nullptr;

	static constexpr FT_Outline_Funcs funcs{
			.move_to = [](const FT_Vector* to, void* user){
				auto* ctx = static_cast<decompose_context*>(user);
				ctx->all_paths->push_back(clipper2::Path64());
				ctx->active_path = &ctx->all_paths->back();

				const clipper2::Point64 p = ft_vec_to_point64(to);
				ctx->active_path->push_back(p);
				ctx->last_point = p;
				return 0;
			},
			.line_to = [](const FT_Vector* to, void* user){
				auto* ctx = static_cast<decompose_context*>(user);
				const clipper2::Point64 p = ft_vec_to_point64(to);
				if(ctx->active_path->empty() || p != ctx->last_point){
					ctx->active_path->push_back(p);
					ctx->last_point = p;
				}
				return 0;
			},
			.conic_to = [](const FT_Vector* control, const FT_Vector* to, void* user){
				auto* ctx = static_cast<decompose_context*>(user);
				if(ctx->active_path->empty()) return 0;

				const clipper2::Point64 last_p64 = ctx->active_path->back();
				const FT_Vector p0 = {static_cast<FT_Pos>(last_p64.x), static_cast<FT_Pos>(last_p64.y)};

				flatten_conic(p0, *control, *to, *ctx->active_path);
				ctx->last_point = ft_vec_to_point64(to);
				return 0;
			},
			.cubic_to = [](const FT_Vector* control1, const FT_Vector* control2, const FT_Vector* to, void* user){
				auto* ctx = static_cast<decompose_context*>(user);
				if(ctx->active_path->empty()) return 0;

				const clipper2::Point64 last_p64 = ctx->active_path->back();
				const FT_Vector p0 = {static_cast<FT_Pos>(last_p64.x), static_cast<FT_Pos>(last_p64.y)};

				flatten_cubic(p0, *control1, *control2, *to, *ctx->active_path);
				ctx->last_point = ft_vec_to_point64(to);
				return 0;
			},
		};

	if(FT_Outline_Decompose(&outline, &funcs, &ctx)){
		return clipper2::Paths64();
	}

	return clipper2::Union(raw_paths, clipper2::FillRule::NonZero);
}

void convert_paths_to_shape(const clipper2::Paths64& paths, msdfgen::Shape& shape) {
	for (const auto& path : paths) {
		const size_t size = path.size();
		// 忽略无法构成面积的路径
		if (size < 3) continue;

		auto& contour = shape.addContour();

		const auto p_start = point64_to_msdf(path.front());
		auto p_curr = p_start;

		for (std::size_t i = 1; i < size; ++i) {
			const auto p_next = point64_to_msdf(path[i]);
			contour.addEdge(new msdfgen::LinearSegment(p_curr, p_next));
			p_curr = p_next;
		}

		contour.addEdge(new msdfgen::LinearSegment(p_curr, p_start));
	}
}

}

namespace msdf_decompose_impl{
using namespace temp_convert;

struct msdf_context{
	msdfgen::Shape* shape;
	msdfgen::Contour* current_contour;
	msdfgen::Point2 last_point;
};

int move_to(const FT_Vector* to, void* user){
	auto* ctx = static_cast<msdf_context*>(user);
	ctx->current_contour = &ctx->shape->addContour();
	ctx->last_point = ft_vec_to_point2(to);
	return 0;
}

int line_to(const FT_Vector* to, void* user){
	auto* ctx = static_cast<msdf_context*>(user);
	const auto p = ft_vec_to_point2(to);
	ctx->current_contour->addEdge(new msdfgen::LinearSegment(ctx->last_point, p));
	ctx->last_point = p;
	return 0;
}

int conic_to(const FT_Vector* control, const FT_Vector* to, void* user){
	auto* ctx = static_cast<msdf_context*>(user);
	const auto p = ft_vec_to_point2(to);
	const auto c = ft_vec_to_point2(control);
	ctx->current_contour->addEdge(new msdfgen::QuadraticSegment(ctx->last_point, c, p));
	ctx->last_point = p;
	return 0;
}

int cubic_to(const FT_Vector* control1, const FT_Vector* control2, const FT_Vector* to, void* user){
	auto* ctx = static_cast<msdf_context*>(user);
	const auto p = ft_vec_to_point2(to);
	const auto c1 = ft_vec_to_point2(control1);
	const auto c2 = ft_vec_to_point2(control2);
	ctx->current_contour->addEdge(new msdfgen::CubicSegment(ctx->last_point, c1, c2, p));
	ctx->last_point = p;
	return 0;
}
}

msdfgen::Shape process_nsvg_with_freetype(
	NSVGimage* svgimage,
	msdfgen::Shape& final_shape,
	FT_Library ft_lib = mo_yanxi::font::get_ft_lib()){
	if(!ft_lib || !svgimage) return final_shape;

	FT_Stroker stroker;
	if(FT_Stroker_New(ft_lib, &stroker) != 0){
		return final_shape;
	}

	std::vector<FT_Vector> points;
	std::vector<unsigned char> tags;
	std::vector<unsigned short> contours;

	using namespace temp_convert;

	try{
		for(const NSVGshape* nsvg_shape = svgimage->shapes; nsvg_shape; nsvg_shape = nsvg_shape->next){
			const bool has_fill = (nsvg_shape->fill.type != NSVG_PAINT_NONE);
			const bool has_stroke = (nsvg_shape->stroke.type != NSVG_PAINT_NONE) && (nsvg_shape->strokeWidth > 0.0f);

			if(has_fill){
				msdfgen::Contour& contour = final_shape.addContour();
				for(const NSVGpath* path = nsvg_shape->paths; path != nullptr; path = path->next){
					for(const auto& [p1, p2, p3, p4] :
					    std::span{path->pts, path->npts * 2uz}
					    | std::views::adjacent<2> | std::views::stride(2) | std::views::transform([](auto&& p){
						    auto [x, y] = p;
						    return mo_yanxi::math::vec2{x, y};
					    })
					    | std::views::adjacent<4>
					    | std::views::stride(3)){
						msdfgen::Point2 p1_(p1.x, p1.y);
						msdfgen::Point2 p2_(p2.x, p2.y);
						msdfgen::Point2 p3_(p3.x, p3.y);
						msdfgen::Point2 p4_(p4.x, p4.y);

						contour.addEdge(new msdfgen::CubicSegment(p1_, p2_, p3_, p4_));
					}
				}
			}

			if(has_stroke){
				FT_Stroker_LineCap ft_cap = FT_STROKER_LINECAP_BUTT;
				if(nsvg_shape->strokeLineCap == NSVG_CAP_ROUND) ft_cap = FT_STROKER_LINECAP_ROUND;
				else if(nsvg_shape->strokeLineCap == NSVG_CAP_SQUARE) ft_cap = FT_STROKER_LINECAP_SQUARE;

				FT_Stroker_LineJoin ft_join = FT_STROKER_LINEJOIN_MITER;
				if(nsvg_shape->strokeLineJoin == NSVG_JOIN_ROUND) ft_join = FT_STROKER_LINEJOIN_ROUND;
				else if(nsvg_shape->strokeLineJoin == NSVG_JOIN_BEVEL) ft_join = FT_STROKER_LINEJOIN_BEVEL;

				FT_Stroker_Set(stroker, float_to_ft(nsvg_shape->strokeWidth / 2.0f), ft_cap, ft_join,
					float_to_ft(nsvg_shape->miterLimit));

				for(const NSVGpath* path = nsvg_shape->paths; path != nullptr; path = path->next){
					if(path->npts < 4) continue;

					FT_Vector v_start = {float_to_ft(path->pts[0]), float_to_ft(path->pts[1])};
					FT_Stroker_BeginSubPath(stroker, &v_start, !path->closed);

					for(const auto& [p1, p2, p3] :
					    std::span{path->pts, path->npts * 2uz}
					    | std::views::drop(2)
					    | std::views::adjacent<2>
					    | std::views::stride(2)
					    | std::views::transform([](auto&& p){
						    auto [x, y] = p;
						    return mo_yanxi::math::vec2{x, y};
					    })
					    | std::views::adjacent<3>
					    | std::views::stride(3)){
						if(mo_yanxi::math::zero((p2 - p1).cross(p3 - p1))){
							FT_Vector p3_{float_to_ft(p3.x), float_to_ft(p3.y)};
							FT_Stroker_LineTo(stroker, &p3_);
						} else{
							FT_Vector p1_{float_to_ft(p1.x), float_to_ft(p1.y)};
							FT_Vector p2_{float_to_ft(p2.x), float_to_ft(p2.y)};
							FT_Vector p3_{float_to_ft(p3.x), float_to_ft(p3.y)};
							FT_Stroker_CubicTo(stroker, &p1_, &p2_, &p3_);
						}
					}
					FT_Stroker_EndSubPath(stroker);
				}

				FT_UInt num_points = 0;
				FT_UInt num_contours = 0;
				FT_Stroker_GetCounts(stroker, &num_points, &num_contours);

				if(num_points == 0 || num_contours == 0) continue;

				points.resize(num_points);
				tags.resize(num_points);
				contours.resize(num_contours);

				FT_Outline outline{
						.points = points.data(),
						.tags = tags.data(),
						.contours = contours.data(),
						.flags = FT_OUTLINE_HIGH_PRECISION | FT_OUTLINE_EVEN_ODD_FILL//(nsvg_shape->fillRule == NSVG_FILLRULE_EVENODD ? FT_OUTLINE_EVEN_ODD_FILL : 0)
					};

				FT_Stroker_Export(stroker, &outline);

				if(points.size() > 64){
					static constexpr FT_Outline_Funcs funcs{
						msdf_decompose_impl::move_to,
						msdf_decompose_impl::line_to,
						msdf_decompose_impl::conic_to,
						msdf_decompose_impl::cubic_to
					};

					msdf_decompose_impl::msdf_context ctx{&final_shape};

					FT_Outline_Decompose(&outline, &funcs, &ctx);
				}else{
					auto fixed_paths = ft_decompose_impl::convert_and_fix_outline(outline);
					ft_decompose_impl::convert_paths_to_shape(fixed_paths, final_shape);
				}


			}
		}
	} catch(...){
		FT_Stroker_Done(stroker);
		throw;
	}
	FT_Stroker_Done(stroker);
	return final_shape;
}

namespace mo_yanxi::graphic::msdf{


void write_to_bitmap(bitmap& bitmap, const msdfgen::Bitmap<float, 3>& region){
	for(unsigned y = 0; y < bitmap.height(); ++y){
		for(unsigned x = 0; x < bitmap.width(); ++x){
			auto& bit = bitmap[x, y];
			bit.r = math::round<std::uint8_t>(
				region(x, y)[0] * static_cast<float>(std::numeric_limits<std::uint8_t>::max()));
			bit.g = math::round<std::uint8_t>(
				region(x, y)[1] * static_cast<float>(std::numeric_limits<std::uint8_t>::max()));
			bit.b = math::round<std::uint8_t>(
				region(x, y)[2] * static_cast<float>(std::numeric_limits<std::uint8_t>::max()));
			bit.a = std::numeric_limits<std::uint8_t>::max();
		}
	}
}

void write_to_bitmap(bitmap& bitmap, const msdfgen::Bitmap<float, 1>& region){
	for(unsigned y = 0; y < bitmap.height(); ++y){
		for(unsigned x = 0; x < bitmap.width(); ++x){
			auto& bit = bitmap[x, y];
			bit.r = bit.g = bit.b = math::round<std::uint8_t>(
				region(x, y)[0] * static_cast<float>(std::numeric_limits<std::uint8_t>::max()));
			bit.a = std::numeric_limits<std::uint8_t>::max();
		}
	}
}

bitmap msdf::load_shape(
	const svg_info& shape,
	unsigned w,
	unsigned h,
	double range, int boarder){
	bitmap bitmap = {w + boarder * 2, h + boarder * 2};
	const auto bound = shape.shape.getBounds();
	const double width = shape.size.x;
	const double height = shape.size.y;

	const auto scale = bitmap.extent().sub(boarder * 2, boarder * 2).as<double>().copy().div(width, height);

	const msdfgen::Projection projection(
		msdfgen::Vector2(scale.x, -scale.y),
		msdfgen::Vector2{
			+(static_cast<double>(boarder)) / scale.x,
			-bound.t - bound.b - (static_cast<double>(boarder)) / scale.y
		}
	);

	msdfgen::Bitmap<float, 3> fbitmap(bitmap.width(), bitmap.height());
	msdfgen::generateMSDF(fbitmap, shape.shape, msdfgen::SDFTransformation{projection, msdfgen::Range{range}},
		msdfgen::MSDFGeneratorConfig{
			true, msdfgen::ErrorCorrectionConfig{}
		});

	msdfgen::simulate8bit(fbitmap);
	write_to_bitmap(bitmap, fbitmap);
	return bitmap;
}

bitmap msdf::load_glyph(
	msdfgen::FontHandle* face, msdfgen::GlyphIndex code,
	unsigned target_w, unsigned target_h, int boarder,
	double font_w, double font_h,
	double range){
	msdfgen::Shape shape;
	if(msdfgen::loadGlyph(shape, face, code, msdfgen::FONT_SCALING_EM_NORMALIZED)){
		bitmap bitmap = {target_w + boarder * 2, target_h + boarder * 2};

		shape.orientContours();
		shape.normalize();

		const auto bound = shape.getBounds();
		const math::vector2 scale{font_w, font_h};

		msdfgen::edgeColoringByDistance(shape, 2.5);
		msdfgen::Bitmap<float, 3> msdf(bitmap.width(), bitmap.height());

		const auto offx = -bound.l + static_cast<double>(boarder) / scale.x;
		const auto offy = -bound.t - static_cast<double>(boarder) / scale.y;

		const msdfgen::SDFTransformation t(
			msdfgen::Projection(
				msdfgen::Vector2{scale.x, -scale.y},
				msdfgen::Vector2(offx, offy)
			), msdfgen::Range(range));

		msdfgen::generateMSDF(msdf, shape, t);
		msdfgen::simulate8bit(msdf);

		write_to_bitmap(bitmap, msdf);
		return bitmap;
	}

	return {target_w + boarder * 2, target_h + boarder * 2};
}

svg_info handle_nanosvg(NSVGimage* svg_image){
	if(!svg_image) return {};

	msdfgen::Shape shape;
	try{
		process_nsvg_with_freetype(svg_image, shape);
	} catch(...){
		nsvgDelete(svg_image);
		return {};
	}

	const math::vec2 sz{svg_image->width, svg_image->height};
	nsvgDelete(svg_image);

	// if(!shape.validate()) return {};

	shape.inverseYAxis = true;
	shape.orientContours();
	msdfgen::edgeColoringByDistance(shape, 3);
	shape.normalize();

	return {shape, sz};
}

svg_info svg_to_shape(const char* path){
	NSVGimage* svg_image = nsvgParseFromFile(path, "px", 96.0f);
	return handle_nanosvg(svg_image);
}

svg_info svg_to_shape(const char* data, std::size_t size){
	auto cpy_data = std::string(data, size);
	NSVGimage* svg_image = nsvgParse(cpy_data.data(), "px", 96.0f);
	return handle_nanosvg(svg_image);
}

math::vec2 get_svg_extent(const char* path){
	NSVGimage* svg_image = nsvgParseFromFile(path, "px", 96.0f);
	if(!svg_image)return {};
	return {svg_image->width, svg_image->height};
}

math::vec2 get_svg_extent(const char* data, std::size_t size){
	auto cpy_data = std::string(data, size);
	NSVGimage* svg_image = nsvgParse(cpy_data.data(), "px", 96.0f);
	if(!svg_image)return {};
	return {svg_image->width, svg_image->height};
}


const svg_info& msdf_generator::get_shape() const{
	switch(state_){
	case msdf_generator_state::done: break;
	case msdf_generator_state::path: shape = svg_to_shape(path.c_str()); state_ = msdf_generator_state::done; break;
	case msdf_generator_state::memory: shape = svg_to_shape(path.data(), path.size()); state_ = msdf_generator_state::done; break;
	default: std::unreachable();
	}
	return shape;
}

math::vec2 msdf_generator::get_extent() const noexcept{
	switch(state_){
	case msdf_generator_state::done: return shape.size;
	case msdf_generator_state::path: return get_svg_extent(path.c_str());
	case msdf_generator_state::memory: return get_svg_extent(path.data(), path.size());
	default: std::unreachable();
	}
}

msdf_generator::msdf_generator(svg_info&& shape, double range, int boarder)
: shape(std::move(shape)), range(range), boarder(boarder){
}

msdf_generator::msdf_generator(std::string str, bool is_memory_data, double range, int boarder)
: path(std::move(str)), state_(is_memory_data ? msdf_generator_state::memory : msdf_generator_state::path), range(range), boarder(boarder){

}


void add_contour(msdfgen::Shape& shape, double size, double radius, double k, double margin = 0.f,
	msdfgen::Vector2 offset = {}){
	using namespace msdfgen;

	Contour& outer_contour = shape.addContour();

	Point2 bl_r{radius + margin, margin};
	Point2 br_l{size - radius - margin, margin};
	Point2 br_t{size - margin, margin + radius};
	Point2 tr_b{size - margin, size - radius - margin};
	Point2 tr_l{size - margin - radius, size - margin};
	Point2 tl_r{radius + margin, size - margin};
	Point2 tl_b{margin, size - margin - radius};
	Point2 bl_t{margin, margin + radius};

	bl_r += offset;
	br_l += offset;
	br_t += offset;
	tr_b += offset;
	tr_l += offset;
	tl_r += offset;
	tl_b += offset;
	bl_t += offset;

	const Vector2 handle{k * radius, k * radius};

	outer_contour.addEdge(new LinearSegment(bl_r, br_l));
	outer_contour.addEdge(new CubicSegment(
		br_l,
		br_l + handle * Vector2{1, 0},
		br_t + handle * Vector2{0, -1},
		br_t
	));
	outer_contour.addEdge(new LinearSegment(br_t, tr_b));
	outer_contour.addEdge(new CubicSegment(
		tr_b,
		tr_b + handle * Vector2{0, 1},
		tr_l + handle * Vector2{1, 0},
		tr_l
	));
	outer_contour.addEdge(new LinearSegment(tr_l, tl_r));
	outer_contour.addEdge(new CubicSegment(
		tl_r,
		tl_r + handle * Vector2{-1, 0},
		tl_b + handle * Vector2{0, 1},
		tl_b
	));
	outer_contour.addEdge(new LinearSegment(tl_b, bl_t));
	outer_contour.addEdge(new CubicSegment(
		bl_t,
		bl_t + handle * Vector2{0, -1},
		bl_r + handle * Vector2{-1, 0},
		bl_r
	));
}


svg_info msdf::create_boarder(double radius, double width, double k){
	using namespace msdfgen;

	// 创建形状对象
	Shape shape;
	shape.inverseYAxis = true; // 翻转Y轴坐标（视需求而定）


	add_contour(shape, boarder_size, radius, k);
	add_contour(shape, boarder_size, radius - width, k, width);
	// 验证形状有效性
	if(!shape.validate()) return {};


	shape.orientContours();
	edgeColoringByDistance(shape, 2.);
	shape.normalize();

	return {shape, {boarder_size, boarder_size}};
}

svg_info msdf::create_solid_boarder(double radius, double k){
	using namespace msdfgen;

	// 创建形状对象
	Shape shape;
	shape.inverseYAxis = true; // 翻转Y轴坐标（视需求而定）

	constexpr double strokeWidth = 2.0; // 轮廓线宽

	add_contour(shape, boarder_size, radius, k);
	// add_contour(shape, 64, radius - strokeWidth, k, strokeWidth);
	// 验证形状有效性
	if(!shape.validate()) return {};

	shape.orientContours();
	edgeColoringByDistance(shape, 2.);
	shape.normalize();
	// 清理资源
	return {shape, {boarder_size, boarder_size}};
}

constexpr double k_circle = 0.5522847498;

svg_info create_capsule(double r, double w){
	using namespace msdfgen;
	Shape shape;
	shape.inverseYAxis = false;

	Contour& contour = shape.addContour();
	const double h = r * k_circle;

	const Point2 p_bot_left(r, 0);
	const Point2 p_bot_right(r + w, 0);
	const Point2 p_right_mid(r + w + r, r);
	const Point2 p_top_right(r + w, 2.0 * r);
	const Point2 p_top_left(r, 2.0 * r);
	const Point2 p_left_mid(0, r);

	contour.addEdge(new LinearSegment(p_bot_left, p_bot_right));

	contour.addEdge(new CubicSegment(
		p_bot_right,
		Point2(r + w + h, 0),
		Point2(r + w + r, r - h),
		p_right_mid
	));

	contour.addEdge(new CubicSegment(
		p_right_mid,
		Point2(r + w + r, r + h),
		Point2(r + w + h, 2.0 * r),
		p_top_right
	));

	contour.addEdge(new LinearSegment(p_top_right, p_top_left));

	contour.addEdge(new CubicSegment(
		p_top_left,
		Point2(r - h, 2.0 * r),
		Point2(0, r + h),
		p_left_mid
	));

	contour.addEdge(new CubicSegment(
		p_left_mid,
		Point2(0, r - h),
		Point2(r - h, 0),
		p_bot_left
	));

	if(!shape.validate()) return {};
	shape.orientContours();
	edgeColoringSimple(shape, 3.0);
	shape.normalize();

	return {shape, math::vector2{w + 2.0 * r, 2.0 * r}.as<float>()};
}

svg_info create_capsule_smooth(double width, double height, double exponent){
	using namespace msdfgen;
	Shape shape;
	Contour& contour = shape.addContour();

	const double a = width / 2.0;
	const double b = height / 2.0;
	const double center_x = a;
	const double center_y = b;

	constexpr int segments = 32;
	const double step = (2.0 * std::numbers::pi) / segments;

	std::vector<Point2> points;
	points.reserve(segments + 1);

	auto evaluate = [&](double theta) -> Point2{
		using std::cos;
		using std::sin;
		using std::pow;
		using std::abs;
		using std::copysign;
		const double c = cos(theta);
		const double s = sin(theta);
		const double dx = copysign(1.0, c) * pow(abs(c), 2.0 / exponent) * a;
		const double dy = copysign(1.0, s) * pow(abs(s), 2.0 / exponent) * b;
		return {center_x + dx, center_y + dy};
	};

	for(int i = 0; i < segments; ++i){
		points.push_back(evaluate(i * step));
	}
	points.push_back(points[0]);

	for(size_t i = 0; i < points.size() - 1; ++i){
		contour.addEdge(new LinearSegment(points[i], points[i + 1]));
	}

	if(!shape.validate()) return {};
	shape.orientContours();
	edgeColoringSimple(shape, 3.0);
	shape.normalize();

	return {shape, math::vector2{width, height}.as<float>()};
}
}
