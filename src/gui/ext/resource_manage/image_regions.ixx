// ReSharper disable CppDFANotInitializedField
module;

#include <vulkan/vulkan.h>
#include <mo_yanxi/assume.hpp>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.assets.image_regions;

export import mo_yanxi.graphic.image_region;
export import mo_yanxi.graphic.image_region.borrow;
import mo_yanxi.math.quad;
import mo_yanxi.graphic.grid_generator;
import mo_yanxi.graphic.color;
import align;

import std;
import mo_yanxi.meta_programming;

namespace mo_yanxi::gui::assets{
export using image_native_handle = VkImageView;
export using image_region_type = graphic::combined_image_region<graphic::size_awared_uv<graphic::uniformed_rect_uv>>;
export using image_region_borrow = graphic::universal_borrowed_image_region<image_region_type, referenced_object_atomic_nonpropagation>;

export
struct simple_vertex{
	math::vec2 pos;
	math::vec2 uv;
	graphic::color color;
};

export
struct row_patch{
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
	// std::array<graphic::uniformed_rect_uv, 3> regions{};

public:
	float margin{};

	[[nodiscard]] row_patch() = default;

	constexpr row_patch(
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

	[[nodiscard]] std::array<float, 6> get_ortho_draw_coords(const math::vec2 pos, const math::vec2 size) const noexcept{
		const auto y0 = pos.y;
		const auto y1 = pos.y + size.y;

		const auto src_h = std::abs(extent_.y) > std::numeric_limits<float>::epsilon() ? extent_.y : 1.0f;
		const auto scale = std::abs(size.y / src_h);

		auto cap_inner_len = cap_width();

		auto src_len_ = src_margin;
		auto end_len_ = end_margin;

		if(const auto absX = std::abs(size.x); cap_inner_len > absX){
			src_len_ = absX * src_len_ / cap_inner_len;
			end_len_ = absX * end_len_ / cap_inner_len;
		}

		src_len_ *= scale;
		end_len_ *= scale;

		const auto x0 = pos.x;
		const auto x3 = pos.x + size.x;

		const auto x1 = x0 + std::copysign(src_len_, size.x);
		const auto x2 = x3 - std::copysign(end_len_, size.x);

		const auto mg = margin /2.f;
		const auto mX = std::copysign(mg, size.x);
		const auto mY = std::copysign(mg, size.y);
		return { x0 - mX, x1, x2, x3 + mX, y0 - mY, y1 + mY};
	}

	//TODO return vtx instead
	[[nodiscard]] std::array<math::fquad, 3> get_regions(math::vec2 src, math::vec2 dst, float scale = 1.) const noexcept{
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


template <typename T = float>
	requires (std::is_arithmetic_v<T>)
using Generator = graphic::grid_generator<4, T>;

using NinePatchProp = graphic::grid_property<4>;
constexpr auto NinePatchSize = NinePatchProp::size;

constexpr align::scale DefaultScale = align::scale::stretch;

export
template <typename T = float>
	requires (std::is_arithmetic_v<T>)
struct nine_patch_raw{
	using rect = math::rect_ortho<T>;

	std::array<rect, NinePatchSize> values{};

	[[nodiscard]] constexpr nine_patch_raw() noexcept = default;

	[[nodiscard]] constexpr nine_patch_raw(const rect internal, const rect external) noexcept
	: values{
		graphic::create_grid<4, T>({
				external.vert_00(),
				internal.vert_00(),
				internal.vert_11(),
				external.vert_11()
			})
	}{
	}

	[[nodiscard]] constexpr nine_patch_raw(align::padding2d<T> edge, const rect external) noexcept{
		constexpr T err = std::numeric_limits<T>::epsilon() * 32;

		bool need_scale{false};
		float ratio_l;
		float ratio_b;
		if(edge.width() >= external.width()){
			need_scale = true;
			ratio_l = edge.left / edge.width();
		} else{
			ratio_l = .5f;
		}

		if(edge.height() >= external.height()){
			need_scale = true;
			ratio_b = edge.bottom / edge.height();
		} else{
			ratio_b = .5f;
		}

		if(need_scale){
			auto target_bot_lft = external.extent().scl(ratio_l - err, ratio_b - err).to_abs();
			auto target_top_rit = external.extent().scl(1 - ratio_l - err, 1 - ratio_b - err).to_abs();

			auto true_botlft = align::embed_to(align::scale::fit, edge.bot_lft(), target_bot_lft);
			auto true_toprit = align::embed_to(align::scale::fit, edge.top_rit(), target_top_rit);

			values = graphic::create_grid<4, T>({
					external.vert_00(),
					external.vert_00() + true_botlft,
					external.vert_11() - true_toprit,
					external.vert_11()
				});
		} else{
			values = graphic::create_grid<4, T>({
					external.vert_00(),
					external.vert_00() + edge.bot_lft(),
					external.vert_11() - edge.top_rit(),
					external.vert_11()
				});
		}
	}


	[[nodiscard]] constexpr nine_patch_raw(
		const align::spacing edge,
		const rect rect,
		const math::vector2<T> centerSize,
		const align::scale centerScale) noexcept
	: nine_patch_raw{edge, rect}{
		nine_patch_raw::set_center_scale(centerSize, centerScale);
	}


	[[nodiscard]] constexpr nine_patch_raw(
		const rect internal, const rect external,
		const math::vector2<T> centerSize,
		const align::scale centerScale) noexcept
	: nine_patch_raw{internal, external}{
		nine_patch_raw::set_center_scale(centerSize, centerScale);
	}

	constexpr void set_center_scale(const math::vector2<T> centerSize, const align::scale centerScale) noexcept{
		if(centerScale != DefaultScale){
			const auto sz = align::embed_to(centerScale, centerSize, center().extent());
			const auto offset = align::get_offset_of<to_signed_t<T>>(align::pos::center, sz.as_signed(),
				center().as_signed());
			center() = {tags::from_extent, static_cast<math::vector2<T>>(offset), sz};
		}
	}

	constexpr rect operator [](const unsigned index) const noexcept{
		return values[index];
	}

	[[nodiscard]] constexpr math::rect_ortho<T>& center() noexcept{
		return values[graphic::grid_property<4>::center_index];
	}

	[[nodiscard]] constexpr const math::rect_ortho<T>& center() const noexcept{
		return values[graphic::grid_property<4>::center_index];
	}
};

export
struct nine_patch_brief{
	align::spacing edge{};
	math::vec2 inner_size{};
	align::scale center_scale{DefaultScale};

	[[nodiscard]] constexpr nine_patch_raw<float> get_regions(const nine_patch_raw<float>::rect bound) const noexcept{
		return nine_patch_raw{edge, bound, inner_size, center_scale};
	}

	[[nodiscard]] constexpr math::vec2 get_recommended_size() const noexcept{
		return inner_size + edge.extent();
	}

	[[nodiscard]] nine_patch_brief() = default;


	[[nodiscard]] nine_patch_brief(
		math::urect external,
		math::urect internal,
		const math::usize2 centerSize = {},
		const align::scale centerScale = DefaultScale
	){
		this->center_scale = centerScale;
		this->inner_size = centerSize.as<float>();
		this->edge = align::padBetween(internal.as<float>(), external.as<float>());
	}


	[[nodiscard]] math::vec2 get_size() const noexcept{
		return inner_size + edge.extent();
	}
};

/*
export
struct image_nine_region : nine_patch_brief{
	static constexpr auto size = NinePatchSize;
	using region_type = combined_image_region<size_awared_uv<uniformed_rect_uv>>;

	graphic::sized_image image_view{};
	std::array<graphic::uniformed_rect_uv, NinePatchSize> regions{};
	float margin{};

	[[nodiscard]] image_nine_region() = default;

	image_nine_region(
		const region_type& imageRegion,
		math::urect internal_in_relative,
		const float external_margin = 0.f,
		const math::usize2 centerSize = {},
		const align::scale centerScale = DefaultScale,
		const float edgeShrinkScale = 0.25f
	) : image_view(imageRegion), margin(external_margin){
		const auto external = imageRegion.uv.get_region();
		internal_in_relative.src += external.src;
		assert(external.contains_loose(internal_in_relative));
		this->nine_patch_brief::operator=(nine_patch_brief{external, internal_in_relative, centerSize, centerScale});


		using gen = Generator<float>;
		const auto ninePatch = nine_patch_raw{internal_in_relative, external, centerSize, centerScale};
		for(auto&& [i, region] : regions | std::views::enumerate){
			region.fetch_into(image_view.size, ninePatch[i]);
		}

		for(const auto hori_edge_index : gen::property::edge_indices | std::views::take(
			    gen::property::edge_indices.size() / 2)){
			regions[hori_edge_index].shrink(image_view.size, {edgeShrinkScale * inner_size.x, 0});
		}

		for(const auto vert_edge_index : gen::property::edge_indices | std::views::drop(
			    gen::property::edge_indices.size() / 2)){
			regions[vert_edge_index].shrink(image_view.size, {0, edgeShrinkScale * inner_size.y});
		}
	}

	image_nine_region(
		const region_type& imageRegion,
		const align::padding2d<std::uint32_t> margin,
		const float external_margin = 0.f,
		const math::usize2 centerSize = {},
		const align::scale centerScale = DefaultScale,
		const float edgeShrinkScale = 0.25f
	) : image_nine_region{
		imageRegion, math::urect{
			tags::from_extent, margin.bot_lft(), imageRegion.uv.get_region().extent().sub(margin.extent())
		},
		external_margin, centerSize, centerScale, edgeShrinkScale
	}{
		assert(margin.extent().within(imageRegion.uv.get_region().extent()));
	}
};*/


namespace builtin{
export
row_patch get_separator_row_patch();
}

}
