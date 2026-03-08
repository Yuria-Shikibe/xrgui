// ReSharper disable CppDFANotInitializedField
module;

#include <vulkan/vulkan.h>
#include <mo_yanxi/assume.hpp>
#include <mo_yanxi/adapted_attributes.hpp>

#ifdef __AVX2__
#include <immintrin.h>
#endif


export module mo_yanxi.gui.image_regions;

export import mo_yanxi.graphic.image_region;
export import mo_yanxi.graphic.image_region.borrow;
import mo_yanxi.math.quad;
import mo_yanxi.graphic.grid_generator;
import mo_yanxi.graphic.color;
import mo_yanxi.graphic.image_region;
import align;

import std;
import mo_yanxi.meta_programming;

namespace mo_yanxi::gui{
export using image_native_handle = VkImageView;
export using image_region_type = graphic::combined_image_region<graphic::size_awared_uv<graphic::uniformed_rect_uv>>;
export using image_region_borrow = graphic::universal_borrowed_image_region<image_region_type,
	referenced_object_atomic_nonpropagation>;


export
struct image_row_patch{
	enum uv_idx{
		uv_idx_y0,
		uv_idx_y1,
		uv_idx_x0,
		uv_idx_x1,
		uv_idx_x2,
		uv_idx_x3,
	};

	enum pos_idx{
		pos_idx_x0,
		pos_idx_x1,
		pos_idx_x2,
		pos_idx_x3,
		pos_idx_y0,
		pos_idx_y1,
	};

private:
	math::vec2 extent_{};
	float src_margin{};
	float end_margin{};

	image_region_borrow image_region_{};

	/**
	 * @brief [0]y_src, [1]y_dst, [2]x_src, [3]x_cap_l, [4]x_cap_r, [5]x_dst
	 */
	std::array<float, 6> uv_coords_;

public:
	float margin{};

	[[nodiscard]] image_row_patch() = default;

	constexpr image_row_patch(
		const image_region_borrow& imageRegion,
		const math::urect region,
		const float src_len,
		const float end_len,
		const float external_margin = 0.f
	) : src_margin(src_len), end_margin(end_len), image_region_(imageRegion), margin(external_margin){
		auto cap_inner_len = cap_width();

		auto src_len_ = src_len;
		auto end_len_ = end_len;

		float len = region.width();

		if(cap_inner_len > len){
			src_len_ = len * src_len_ / cap_inner_len;
			end_len_ = len * end_len_ / cap_inner_len;
		}

		extent_ = {len, static_cast<float>(region.height())};

		const math::rect_ortho<double>
			rect_src{
					tags::unchecked, tags::from_extent, region.src.as<double>() + math::vector2<double>{},
					{src_len_, extent_.y}
				};

		const math::rect_ortho<double>
			rect_dst{
					tags::unchecked, tags::from_extent, region.src.as<double>() + math::vector2<double>{len - end_len_},
					{end_len_, extent_.y}
				};

		const auto sz = image_region_->uv.size.as<double>();
		graphic::uniformed_rect_uv uvL, uvR;
		uvL.fetch_into(sz, rect_src);
		uvR.fetch_into(sz, rect_dst);

		uv_coords_[uv_idx_y0] = uvL.v00().y;
		uv_coords_[uv_idx_y1] = uvL.v11().y;

		uv_coords_[uv_idx_x0] = uvL.v00().x;
		uv_coords_[uv_idx_x1] = uvL.v11().x;
		uv_coords_[uv_idx_x2] = uvR.v00().x;
		uv_coords_[uv_idx_x3] = uvR.v11().x;
	}

	[[nodiscard]] image_native_handle get_image_view() const noexcept{
		return image_region_->view;
	}

	[[nodiscard]] FORCE_INLINE constexpr float mid_width() const noexcept{
		return end_margin - src_margin;
	}

	[[nodiscard]] FORCE_INLINE constexpr float cap_width() const noexcept{
		return src_margin + end_margin;
	}

	[[nodiscard]] constexpr std::span<const float, 6> get_uv_span() const noexcept{
		return uv_coords_;
	}

	[[nodiscard]] constexpr std::array<float, 6> get_uvs() const noexcept{
		return uv_coords_;
	}

	[[nodiscard]] math::vec2 extent() const noexcept{
		return extent_;
	}

private:
	// 将原来的具体 X/Y 逻辑抽象为主轴(Major)和副轴(Minor)逻辑，消除重复代码
	[[nodiscard]] std::array<float, 6> calc_sliced_coords(
		const float major_pos, const float major_size,
		const float minor_pos, const float minor_size
	) const noexcept{
		const auto minor_0 = minor_pos;
		const auto minor_1 = minor_pos + minor_size;

		// 缩放基准始终使用副轴的实际渲染大小与源纹理的高度(extent_.y)进行对比
		const auto src_minor_extent = math::abs(extent_.y) > 0.0001f ? extent_.y : 1.0f;
		const auto scale = math::abs(minor_size / src_minor_extent);

		auto cap_inner_len = cap_width();
		auto src_len_ = src_margin;
		auto end_len_ = end_margin;

		if(const auto abs_major = math::abs(major_size); cap_inner_len > abs_major){
			src_len_ = abs_major * src_len_ / cap_inner_len;
			end_len_ = abs_major * end_len_ / cap_inner_len;
		}

		src_len_ *= scale;
		end_len_ *= scale;

		const auto major_0 = major_pos;
		const auto major_3 = major_pos + major_size;

		const auto major_1 = major_0 + math::copysign(src_len_, major_size);
		const auto major_2 = major_3 - math::copysign(end_len_, major_size);

		const auto mg = margin / 2.f;
		const auto m_major = math::copysign(mg, major_size);
		const auto m_minor = math::copysign(mg, minor_size);

		// 返回格式：[major_0, major_1, major_2, major_3, minor_0, minor_1]
		return {
				major_0 - m_major, major_1, major_2, major_3 + m_major,
				minor_0 - m_minor, minor_1 + m_minor
			};
	}

	// 1. 核心底座：纯粹的数学推导，不包含任何业务判断
    [[nodiscard]] constexpr std::array<float, 6> compute_coords_by_scale(
       const float major_pos, const float major_size,
       const float minor_pos, const float minor_size,
       const float scale_major, const float scale_minor
    ) const noexcept {
       // 计算缩放后的 cap 尺寸
       const float scaled_src = src_margin * scale_major;
       const float scaled_end = end_margin * scale_major;

       // margin 的独立缩放
       const float mg = margin / 2.f;
       const float scaled_mg_major = mg * scale_major;
       const float scaled_mg_minor = mg * scale_minor;

       // 提取符号，天然支持传入负数 size 实现镜像翻转绘制
       const float s_major = math::copysign(1.0f, major_size);
       const float s_minor = math::copysign(1.0f, minor_size);

       const float major_0 = major_pos;
       const float major_3 = major_pos + major_size;

       // 推导四个切片节点
       const float major_1 = major_0 + scaled_src * s_major;
       const float major_2 = major_3 - scaled_end * s_major;

       const float m_major = scaled_mg_major * s_major;
       const float m_minor = scaled_mg_minor * s_minor;

       const float minor_0 = minor_pos;
       const float minor_1 = minor_pos + minor_size;

       return {
             major_0 - m_major, major_1, major_2, major_3 + m_major,
             minor_0 - m_minor, minor_1 + m_minor
       };
    }

    // 2. 策略分发层：修复拉伸问题的关键
    [[nodiscard]] constexpr std::array<float, 6> execute_axis_scaled(
        const float major_pos, const float major_size,
        const float minor_pos, const float minor_size
    ) const noexcept {
       const float cap_len = cap_width();
       const float minor_ext = math::abs(extent_.y) > 0.0001f ? math::abs(extent_.y) : 1.0f;

       // 【关键修复 1】：计算副轴（高度）真实的缩放比例。
       // 注意：这里去掉了原来 `> minor_size ? : 1.0f` 的限制。
       // 因为对于圆角形状，无论目标区域是变大还是变小，都应该获取真实的比例，才能保持完美的圆角。
       const float scale_minor = math::abs(minor_size) / minor_ext;

       // 【关键修复 2】：Cap（两端的圆角）的宽度缩放必须严格跟随高度的缩放比例！
       // 这保证了宽高比始终为 1:1，彻底解决横向拉长的问题。
       float scale_major = scale_minor;

       // 防溢出保护：如果主轴空间太窄，连等比缩放后的圆角都放不下，才被迫进一步挤压圆角宽度
       const float scaled_cap_len = cap_len * scale_minor;
       if (scaled_cap_len > 0.f && scaled_cap_len > math::abs(major_size)) {
           scale_major = math::abs(major_size) / cap_len;
       }

       return compute_coords_by_scale(major_pos, major_size, minor_pos, minor_size, scale_major, scale_minor);
    }

    [[nodiscard]] constexpr std::array<float, 6> execute_scaled(
        const float major_pos, const float major_size,
        const float minor_pos, const float minor_size
    ) const noexcept {
       const float cap_len = cap_width();
       const float minor_ext = math::abs(extent_.y) > 0.0001f ? math::abs(extent_.y) : 1.0f;

       // 全局等比缩放：取最小适应比例
       float scale = math::min(1.0f, math::abs(minor_size) / minor_ext);
       if (cap_len > 0.f && cap_len > math::abs(major_size)) {
           scale = math::min(scale, math::abs(major_size) / cap_len);
       }

       return compute_coords_by_scale(major_pos, major_size, minor_pos, minor_size, scale, scale);
    }

    [[nodiscard]] constexpr std::array<float, 6> execute_raw(
        const float major_pos, const float major_size,
        const float minor_pos, const float minor_size
    ) const noexcept {
       const float cap_len = cap_width();

       // Raw 模式下高度保持 1:1，只对宽度做最小限度的防交叉保护
       const float scale_major = (cap_len > 0.f && cap_len > math::abs(major_size)) ? (math::abs(major_size) / cap_len) : 1.0f;

       return compute_coords_by_scale(major_pos, major_size, minor_pos, minor_size, scale_major, 1.0f);
    }

public:
    // =========================================================================
    // 公开接口层 (极致简洁的 One-liner，直接将 X/Y 映射为 Major/Minor)
    // =========================================================================

    // A. 正常横向布局 (X为主轴)
    [[nodiscard]] constexpr std::array<float, 6> get_ortho_draw_coords_axis_scaled(const math::raw_frect region) const noexcept {
       return execute_axis_scaled(region.src.x, region.extent.x, region.src.y, region.extent.y);
    }
    [[nodiscard]] constexpr std::array<float, 6> get_ortho_draw_coords_scaled(const math::raw_frect region) const noexcept {
       return execute_scaled(region.src.x, region.extent.x, region.src.y, region.extent.y);
    }
    [[nodiscard]] constexpr std::array<float, 6> get_ortho_draw_coords(const math::raw_frect region) const noexcept {
       return execute_raw(region.src.x, region.extent.x, region.src.y, region.extent.y);
    }

    // B. 纵向转置布局 (Y为主轴)
    [[nodiscard]] constexpr std::array<float, 6> get_ortho_draw_coords_axis_scaled_transsrced(const math::raw_frect region) const noexcept {
       return execute_axis_scaled(region.src.y, region.extent.y, region.src.x, region.extent.x);
    }
    [[nodiscard]] constexpr std::array<float, 6> get_ortho_draw_coords_scaled_transsrced(const math::raw_frect region) const noexcept {
       return execute_scaled(region.src.y, region.extent.y, region.src.x, region.extent.x);
    }
    [[nodiscard]] constexpr std::array<float, 6> get_ortho_draw_coords_transsrced(const math::raw_frect region) const noexcept {
       return execute_raw(region.src.y, region.extent.y, region.src.x, region.extent.x);
    }

	//TODO return vtx instead
	[[nodiscard]] std::array<math::fquad, 3> get_regions(math::vec2 src, math::vec2 dst,
		float scale = 1.) const noexcept{
		auto approach = dst - src;
		auto len = approach.length();

		auto cap_inner_len = cap_width() / 2.f;

		auto src_len_ = src_margin / 2.f;
		auto end_len_ = end_margin / 2.f;

		if(cap_inner_len > len){
			src_len_ = len * src_len_ / cap_inner_len;
			end_len_ = len * end_len_ / cap_inner_len;
		}

		math::frect rect_src{tags::unchecked, tags::from_extent, {}, math::vec2{src_len_ * 2, extent_.y} * scale};
		math::frect rect_dst{tags::unchecked, tags::from_extent, {}, math::vec2{end_len_ * 2, extent_.y} * scale};

		const auto cos = approach.x / len;
		const auto sin = approach.y / len;
		rect_src.set_center(src).expand(margin / 2, margin);
		rect_dst.set_center(dst).expand(margin / 2, margin);

		auto quad_src = math::rect_ortho_to_quad(rect_src, rect_src.get_center(), cos, sin);
		auto quad_dst = math::rect_ortho_to_quad(rect_dst, rect_dst.get_center(), cos, sin);
		quad_src.move(math::vec2{margin / 2}.rotate(-cos, -sin));
		quad_dst.move(math::vec2{margin / 2}.rotate(cos, sin));

		math::fquad quad_mid{quad_src.v1, quad_dst.v0, quad_dst.v3, quad_src.v2};

		return {quad_src, quad_mid, quad_dst};
	}
};

export
struct nine_patch_layout{
	align::spacing edge{};
	math::vec2 inner_size{};

	[[nodiscard]] nine_patch_layout() = default;

	[[nodiscard]] nine_patch_layout(
		math::frect external,
		math::frect internal
	){
		// 直接从 internal 获取 inner_size
		this->inner_size = internal.extent();
		this->edge = align::pad_between(internal, external);
	}

	[[nodiscard]] constexpr math::vec2 get_recommended_size() const noexcept{
		return inner_size + edge.extent();
	}

	[[nodiscard]] math::vec2 get_size() const noexcept{
		return inner_size + edge.extent();
	}

	[[nodiscard]] constexpr std::array<math::frect, 9> get_regions(const math::frect bound) const noexcept{
		const std::array<float, 4> xs = {
				bound.get_src_x(),
				bound.get_src_x() + edge.left,
				bound.get_end_x() - edge.right,
				bound.get_end_x()
			};
		const std::array<float, 4> ys = {
				bound.get_src_y(),
				bound.get_src_y() + edge.bottom,
				bound.get_end_y() - edge.top,
				bound.get_end_y()
			};

		std::array<math::frect, 9> rst{};
		for(int y = 0; y < 3; ++y){
			for(int x = 0; x < 3; ++x){
				rst[x + y * 3] = math::frect{
						tags::unchecked, tags::from_vertex,
						math::vec2{xs[x], ys[y]},
						math::vec2{xs[x + 1], ys[y + 1]}
					};
			}
		}
		return rst;
	}

	[[nodiscard]] constexpr std::array<std::array<float, 6>, 3> get_row_coords(
		const math::raw_frect bound) const noexcept{
		const std::array xs = {
				(bound.get_src_x()),
				(bound.get_src_x() + edge.left),
				(bound.get_end_x() - edge.right),
				(bound.get_end_x())
			};
		const std::array ys = {
				(bound.get_src_y()),
				(bound.get_src_y() + edge.bottom),
				(bound.get_end_y() - edge.top),
				(bound.get_end_y())
			};

		return {
				std::array{xs[0], xs[1], xs[2], xs[3], ys[0], ys[1]},
				std::array{xs[0], xs[1], xs[2], xs[3], ys[1], ys[2]},
				std::array{xs[0], xs[1], xs[2], xs[3], ys[2], ys[3]}
			};
	}

	template <typename T>
	[[nodiscard]] constexpr math::section<T> interpolate_middle_row_values(
		const T& val_bottom, // 下方（或起点）的值
		const T& val_top, // 上方（或终点）的值
		const float total_height
	) const noexcept{
		if(total_height <= 0.0f){
			return {val_bottom, val_top};
		}

		const float t_bottom = edge.bottom / total_height;
		const float t_top = 1.0f - (edge.top / total_height);

		const T mid_bottom = val_bottom + (val_top - val_bottom) * t_bottom;
		const T mid_top = val_bottom + (val_top - val_bottom) * t_top;

		return {mid_bottom, mid_top};
	}
};

export
struct image_nine_region : nine_patch_layout{
	static constexpr auto size = 9;
	using region_type = image_region_borrow;

	image_region_borrow image_view{};
	float margin{};

	graphic::uniformed_rect_uv outer_uv{};
	graphic::uniformed_rect_uv inner_uv{};

	[[nodiscard]] image_nine_region() = default;

	image_nine_region(
		const region_type& image_region,
		math::urect internal_in_relative,
		const float external_margin = 0.f
	) : image_view(image_region), margin(external_margin){
		const auto external = image_region->uv.get_region();
		internal_in_relative.src += external.src;
		assert(external.contains_loose(internal_in_relative));

		// 移除了 center_size 参数
		this->nine_patch_layout::operator=(nine_patch_layout{external.as<float>(), internal_in_relative.as<float>()});

		const auto bound_size = image_view->uv.size.as<float>();
		outer_uv.fetch_into(bound_size, external.as<float>());
		inner_uv.fetch_into(bound_size, internal_in_relative.as<float>());
	}

	image_nine_region(
		const region_type& image_region,
		const align::padding2d<std::uint32_t> padding,
		const float external_margin = 0.f
	) : image_nine_region{
			image_region,
			math::urect{
				tags::from_extent, padding.bot_lft(), image_region->uv.get_region().extent().sub(padding.extent())
			},
			external_margin
		}{
		assert(padding.extent().within(image_region->uv.get_region().extent()));
	}

	[[nodiscard]] constexpr std::array<graphic::uniformed_rect_uv, 9> get_uvs() const noexcept{
		const std::array<float, 4> xs = {outer_uv.v00().x, inner_uv.v00().x, inner_uv.v11().x, outer_uv.v11().x};
		const std::array<float, 4> ys = {outer_uv.v00().y, inner_uv.v00().y, inner_uv.v11().y, outer_uv.v11().y};

		std::array<graphic::uniformed_rect_uv, 9> rst{};
		for(int y = 0; y < 3; ++y){
			for(int x = 0; x < 3; ++x){
				rst[x + y * 3] = graphic::uniformed_rect_uv{
						math::vec2{xs[x], ys[y]},
						math::vec2{xs[x + 1], ys[y + 1]}
					};
			}
		}
		return rst;
	}

	[[nodiscard]] constexpr std::array<std::array<float, 6>, 3> get_row_uvs() const noexcept{
		const std::array us = {
				outer_uv.v00().x,
				inner_uv.v00().x,
				inner_uv.v11().x,
				outer_uv.v11().x
			};
		const std::array vs = {
				outer_uv.v00().y,
				inner_uv.v00().y,
				inner_uv.v11().y,
				outer_uv.v11().y
			};

		return {
				std::array{vs[0], vs[1], us[0], us[1], us[2], us[3]},
				std::array{vs[1], vs[2], us[0], us[1], us[2], us[3]},
				std::array{vs[2], vs[3], us[0], us[1], us[2], us[3]}
			};
	}

private:
	FORCE_INLINE [[nodiscard]] constexpr std::array<std::array<float, 6>, 3> compute_coords_by_scale(
		const math::raw_frect bound,
		const float scale_x,
		const float scale_y
	) const noexcept{
#ifdef __AVX2__
		if consteval{
#endif
			const float scaled_margin_x = margin * scale_x;
			const float scaled_margin_y = margin * scale_y;

			const std::array xs = {
				bound.get_src_x() - scaled_margin_x,
				bound.get_src_x() + (edge.left * scale_x),
				bound.get_end_x() - (edge.right * scale_x),
				bound.get_end_x() + scaled_margin_x
			};

			const std::array ys = {
				bound.get_src_y() - scaled_margin_y,
				bound.get_src_y() + (edge.bottom * scale_y),
				bound.get_end_y() - (edge.top * scale_y),
				bound.get_end_y() + scaled_margin_y
			};

			return {
				std::array{xs[0], xs[1], xs[2], xs[3], ys[0], ys[1]},
				std::array{xs[0], xs[1], xs[2], xs[3], ys[1], ys[2]},
				std::array{xs[0], xs[1], xs[2], xs[3], ys[2], ys[3]}
			};
#ifdef __AVX2__
		} else{

			const __m256 base_xy = _mm256_setr_ps(
				bound.get_src_x(), bound.get_src_x(), bound.get_end_x(), bound.get_end_x(), // X
				bound.get_src_y(), bound.get_src_y(), bound.get_end_y(), bound.get_end_y() // Y
			);

			const __m256 offset_xy = _mm256_setr_ps(
				-margin, edge.left, -edge.right, margin, // X offset
				-margin, edge.bottom, -edge.top, margin // Y offset
			);

			const __m256 scale_xy = _mm256_setr_ps(
				scale_x, scale_x, scale_x, scale_x,
				scale_y, scale_y, scale_y, scale_y
			);

			const __m256 result_xy = _mm256_fmadd_ps(offset_xy, scale_xy, base_xy);

			alignas(32) float xy[8];
			_mm256_store_ps(xy, result_xy);

			return {
					std::array{xy[0], xy[1], xy[2], xy[3], xy[4], xy[5]},
					std::array{xy[0], xy[1], xy[2], xy[3], xy[5], xy[6]},
					std::array{xy[0], xy[1], xy[2], xy[3], xy[6], xy[7]}
				};
		}
#endif

	}

public:
	[[nodiscard]] constexpr std::array<std::array<float, 6>, 3> get_row_coords_axis_scaled(
		const math::raw_frect bound) const noexcept{
		const auto bound_ext = bound.extent;
		const auto edge_ext = edge.extent();

		// 优化：仅当 edge 尺寸大于 bound 时才需要缩小，避免除法运算冗余
		const float scale_x = (edge_ext.x > 0.f && edge_ext.x > bound_ext.x) ? (bound_ext.x / edge_ext.x) : 1.0f;
		const float scale_y = (edge_ext.y > 0.f && edge_ext.y > bound_ext.y) ? (bound_ext.y / edge_ext.y) : 1.0f;

		return compute_coords_by_scale(bound, scale_x, scale_y);
	}

	[[nodiscard]] constexpr std::array<std::array<float, 6>, 3> get_row_coords_scaled(
		const math::raw_frect bound) const noexcept{
		const auto bound_ext = bound.extent;
		const auto edge_ext = edge.extent();

		float scale = 1.0f;
		if(edge_ext.x > 0.f && edge_ext.y > 0.f && (edge_ext.x > bound_ext.x || edge_ext.y > bound_ext.y)){
			scale = std::min(1.0f, align::get_fit_embed_scale(edge_ext, bound_ext));
		}

		return compute_coords_by_scale(bound, scale, scale);
	}

	[[nodiscard]] constexpr std::array<std::array<float, 6>, 3> get_row_coords(
		const math::raw_frect bound) const noexcept{
		return compute_coords_by_scale(bound, 1.0f, 1.0f);
	}
};

namespace assets::builtin{
export image_row_patch get_separator_row_patch();

// export inline row_patch default_row_seperator;

export inline image_nine_region default_round_square_boarder;
export inline image_nine_region default_round_square_boarder_thin;
export inline image_nine_region default_round_square_base;
}
}
