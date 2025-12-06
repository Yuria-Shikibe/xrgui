module;

#include <mo_yanxi/enum_operator_gen.hpp>

export module align;

import mo_yanxi.concepts;
export import mo_yanxi.math.vector2;
export import mo_yanxi.math.rect_ortho;

import std;
import mo_yanxi.math;

namespace mo_yanxi{
	namespace align{
		template <arithmetic T1, arithmetic T2>
		constexpr float floating_div(const T1 a, const T2 b) noexcept{
			return static_cast<float>(a) / static_cast<float>(b);
		}

		template <arithmetic T1>
		constexpr T1 floating_mul(const T1 a, const float b) noexcept{
			if constexpr(std::is_floating_point_v<T1>){
				return a * b;
			} else{
				return math::round<T1>(static_cast<float>(a) * b);
			}
		}
	}

	export namespace align{
		template <typename T>
			requires (std::is_arithmetic_v<T>)
		struct padding1d{
			T pre;
			T post;

			constexpr T length() const noexcept{
				return post + pre;
			}

			constexpr void set(T val) noexcept{
				pre = val;
				post = val;
			}
		};

		template <typename T>
			requires (std::is_arithmetic_v<T>)
		struct padding2d{
			/**@brief Left Spacing*/
			T left{};
			/**@brief Right Spacing*/
			T right{};
			/**@brief Bottom Spacing*/
			T bottom{};
			/**@brief Top Spacing*/
			T top{};

			[[nodiscard]] constexpr math::vector2<T> bot_lft() const noexcept{
				return {left, bottom};
			}

			[[nodiscard]] constexpr math::vector2<T> top_rit() const noexcept{
				return {right, top};
			}

			[[nodiscard]] constexpr math::vector2<T> top_lft() const noexcept{
				return {left, top};
			}

			[[nodiscard]] constexpr math::vector2<T> bot_rit() const noexcept{
				return {right, bottom};
			}

			[[nodiscard]] friend constexpr bool operator==(const padding2d& lhs, const padding2d& rhs) noexcept = default;

			constexpr padding2d& operator+=(const padding2d& other) noexcept {
				left += other.left;
				right += other.right;
				bottom += other.bottom;
				top += other.top;
				return *this;
			}

			constexpr padding2d& operator-=(const padding2d& other) noexcept {
				left -= other.left;
				right -= other.right;
				bottom -= other.bottom;
				top -= other.top;
				return *this;
			}

			constexpr friend padding2d operator+(padding2d lhs, const padding2d& rhs) noexcept {
				return lhs += rhs;
			}

			constexpr friend padding2d operator-(padding2d lhs, const padding2d& rhs) noexcept {
				return lhs -= rhs;
			}

			constexpr padding2d& expand(T x, T y) noexcept{
				x = align::floating_mul(x, 0.5f);
				y = align::floating_mul(y, 0.5f);

				left += x;
				right += x;
				top += y;
				bottom += y;
				return *this;
			}

			constexpr padding2d& expand(const T val) noexcept{
				return this->expand(val, val);
			}

			[[nodiscard]] constexpr T width() const noexcept{
				return left + right;
			}

			[[nodiscard]] constexpr T height() const noexcept{
				return bottom + top;
			}

			[[nodiscard]] constexpr math::vector2<T> extent() const noexcept{
				return {width(), height()};
			}

			[[nodiscard]] constexpr T getRemainWidth(const T total = 1) const noexcept{
				return total - width();
			}

			[[nodiscard]] constexpr T getRemainHeight(const T total = 1) const noexcept{
				return total - height();
			}

			constexpr padding2d& set(const T val) noexcept{
				bottom = top = left = right = val;
				return *this;
			}

			constexpr padding2d& set_hori(const T val) noexcept{
				left = right = val;
				return *this;
			}

			constexpr padding2d& set_vert(const T val) noexcept{
				bottom = top = val;
				return *this;
			}

			constexpr padding2d& set(const T l, const T r, const T b, const T t) noexcept{
				left = l;
				right = r;
				bottom = b;
				top = t;
				return *this;
			}

			constexpr padding2d& set_zero() noexcept{
				return set(0);
			}
		};

		template <typename T>
		padding2d<T> padBetween(const math::rect_ortho<T>& internal, const math::rect_ortho<T>& external) noexcept {
			return padding2d<T>{
					internal.get_src_x() - external.get_src_x(),
					external.get_end_x() - internal.get_end_x(),
					internal.get_src_y() - external.get_src_y(),
					external.get_end_y() - internal.get_end_y(),
				};
		}

		using spacing = padding2d<float>;

		enum class pos : unsigned char{
			none,
			left = 0b0000'0001,
			right = 0b0000'0010,
			center_x = 0b0000'0100,

			mask_x = left | right | center_x,

			top = 0b0000'1000,
			bottom = 0b0001'0000,
			center_y = 0b0010'0000,

			mask_y = top | bottom | center_y,

			top_left = top | left,
			top_center = top | center_x,
			top_right = top | right,

			center_left = center_y | left,
			center = center_y | center_x,
			center_right = center_y | right,

			bottom_left = bottom | left,
			bottom_center = bottom | center_x,
			bottom_right = bottom | right,
		};

		BITMASK_OPS(, pos);

		constexpr pos operator-(const pos lhs) noexcept{
			auto x = lhs & pos::mask_x;
			auto y = lhs & pos::mask_y;

			pos nx;
			pos ny;
			switch(x){
			case pos::left : nx = pos::right;
				break;
			case pos::right : nx = pos::left;
				break;
			case pos::center_x : nx = pos::center_x;
				break;
			default : nx = pos::none;
			}
			switch(y){
			case pos::bottom : ny = pos::top;
				break;
			case pos::top : ny = pos::bottom;
				break;
			case pos::center_y : ny = pos::center_y;
				break;
			default : ny = pos::none;
			}

			return nx | ny;
		}

		constexpr pos flip_y(const pos p) noexcept{
			return -(p & pos::mask_y) | p & pos::mask_x;
		}

		enum class scale : unsigned char{
			/** The source is not scaled. */
			none,

			/**
			 * Scales the source to fit the target while keeping the same aspect ratio.
			 * This may cause the source to be smaller than the
			 * target in one direction.
			 */
			fit, fit_smaller, fit_greater,

			/**
			 * Scales the source to fit the target if it is larger, otherwise does not scale.
			 */
			clamped,

			/**
			 * Scales the source to fill the target while keeping the same aspect ratio.
			 * This may cause the source to be larger than the
			 * target in one direction.
			 */
			fill,

			/**
			 * Scales the source to fill the target in the x direction while keeping the same aspect ratio.
			 * This may cause the source to be
			 * smaller or larger than the target in the y direction.
			 */
			fillX,

			/**
			 * Scales the source to fill the target in the y direction while keeping the same aspect ratio.
			 * This may cause the source to be
			 * smaller or larger than the target in the x direction.
			 */
			fillY,

			/** Scales the source to fill the target. This may cause the source to not keep the same aspect ratio. */
			stretch,

			/**
			 * Scales the source to fill the target in the x direction, without changing the y direction.
			 * This may cause the source to not
			 * keep the same aspect ratio.
			 */
			stretchX,

			/**
			 * Scales the source to fill the target in the y direction, without changing the x direction.
			 * This may cause the source to not
			 * keep the same aspect ratio.
			 */
			stretchY,

			//TODO fit if samller/larger option?
		};

		template <arithmetic T>
		constexpr T get_fit_embed_scale(math::vector2<T> srcSize, math::vector2<T> toBound) noexcept{
			const float targetRatio = align::floating_div(toBound.y, toBound.x);
			const float sourceRatio = align::floating_div(srcSize.y, srcSize.x);
			const float scale = targetRatio > sourceRatio
									? align::floating_div(toBound.x, srcSize.x)
									: align::floating_div(toBound.y, srcSize.y);
			return scale;
		}

		template <arithmetic T>
		[[nodiscard]] constexpr math::vector2<T> embed_to(const scale stretch, math::vector2<T> srcSize, math::vector2<T> toBound) noexcept{
			switch(stretch){
			case scale::fit :{
				const float scale = align::get_fit_embed_scale(srcSize, toBound);
				return math::vector2<T>{align::floating_mul<T>(srcSize.x, scale), align::floating_mul<T>(srcSize.y, scale)};
			}
			case scale::fit_smaller :{
				const float scale = math::min<float>(align::get_fit_embed_scale(srcSize, toBound), 1);

				return math::vector2<T>{align::floating_mul<T>(srcSize.x, scale), align::floating_mul<T>(srcSize.y, scale)};
			}
			case scale::fit_greater :{
				const float scale = math::max<float>(align::get_fit_embed_scale(srcSize, toBound), 1);

				return math::vector2<T>{align::floating_mul<T>(srcSize.x, scale), align::floating_mul<T>(srcSize.y, scale)};
			}
			case scale::fill :{
				const float targetRatio = align::floating_div(toBound.y, toBound.x);
				const float sourceRatio = align::floating_div(srcSize.y, srcSize.x);
				const float scale = targetRatio < sourceRatio
					                    ? align::floating_div(toBound.x, srcSize.x)
					                    : align::floating_div(toBound.y, srcSize.y);
				return {align::floating_mul<T>(srcSize.x, scale), align::floating_mul<T>(srcSize.y, scale)};
			}
			case scale::fillX :{
				const float scale = align::floating_div(toBound.x, srcSize.x);
				return {align::floating_mul<T>(srcSize.x, scale), align::floating_mul<T>(srcSize.y, scale)};
			}
			case scale::fillY :{
				const float scale = align::floating_div(toBound.y, srcSize.y);
				return {align::floating_mul<T>(srcSize.x, scale), align::floating_mul<T>(srcSize.y, scale)};
			}
			case scale::stretch : return toBound;
			case scale::stretchX : return {toBound.x, std::min(srcSize.y, toBound.y)};
			case scale::stretchY : return {std::min(srcSize.x, toBound.x), toBound.y};
			case scale::clamped : if(srcSize.y > toBound.y || srcSize.x > toBound.x){
					return align::embed_to<T>(scale::fit, srcSize, toBound);
				} else{
					return align::embed_to<T>(scale::none, srcSize, toBound);
				}
			case scale::none : return srcSize;
			}

			std::unreachable();
		}

		template <signed_number T>
		constexpr math::vector2<T> get_offset_of(
			const pos align,
			const math::vector2<T> bottomLeft,
			const math::vector2<T> topRight) noexcept {
			math::vector2<T> move{};
			if((align & pos::top) != pos{}){
				move.y = -topRight.y;
			} else if((align & pos::bottom) != pos{}){
				move.y = bottomLeft.y;
			} else if((align & pos::center_y) != pos{}){
				// Center logic: average of the two opposing offsets/margins
				move.y = (bottomLeft.y - topRight.y) / static_cast<T>(2);
			}

			if((align & pos::right) != pos{}){
				move.x = -topRight.x;
			} else if((align & pos::left) != pos{}){
				move.x = bottomLeft.x;
			} else if((align & pos::center_x) != pos{}){
				// Center logic: average of the two opposing offsets/margins
				move.x = (bottomLeft.x - topRight.x) / static_cast<T>(2);
			}

			return move;
		}

		/**
		 * @brief
		 * @tparam T arithmetic type, does not accept unsigned type
		 * @return
		 */
		template <signed_number T>
		constexpr math::vector2<T> get_offset_of(const pos align, const math::vector2<T>& bound) noexcept{
			math::vector2<T> offset;
			if((align & pos::bottom) != pos{}){
				offset.y = -bound.y;
			} else if((align & pos::center_y) != pos{}){
				offset.y = -bound.y / static_cast<T>(2);
			} else if((align & pos::top) != pos{}){
				offset.y = 0;
			}

			if((align & pos::right) != pos{}){
				offset.x = -bound.x;
			} else if((align & pos::center_x) != pos{}){
				offset.x = -bound.x / static_cast<T>(2);
			} else if((align & pos::left) != pos{}){
				offset.x = 0;
			}

			return offset;
		}

		/**
		 * @brief
		 * @tparam T arithmetic type, does not accept unsigned type
		 * @return
		 */
		template <signed_number T>
		constexpr math::vector2<T> get_offset_of(const pos align, const math::rect_ortho<T>& bound) noexcept{
			return align::get_offset_of<T>(align, bound.extent());
		}


		template <signed_number T>
		[[nodiscard]] constexpr math::vector2<T> get_vert(const pos align, const math::vector2<T>& size) noexcept{
			math::vector2<T> offset{};


			if((align & pos::bottom) != pos{}){
				offset.y = size.y;
			} else if((align & pos::center_y) != pos{}){
				offset.y = size.y / static_cast<T>(2);
			} else if((align & pos::top) != pos{}){
				offset.y = 0;
			}

			if((align & pos::right) != pos{}){
				offset.x = size.x;
			} else if((align & pos::center_x) != pos{}){
				offset.x = size.x / static_cast<T>(2);
			} else if((align & pos::left) != pos{}){
				offset.x = 0;
			}

			return offset;
		}


		/**
		 * @brief
		 * @tparam T arithmetic type, does not accept unsigned type
		 * @return
		 */
		template <signed_number T>
		[[nodiscard]] constexpr math::vector2<T> get_vert(const pos align, const math::rect_ortho<T>& bound) noexcept{
			return align::get_vert<T>(align, bound.extent()) + bound.get_src();
		}

		/**
		 * @brief
		 * @tparam T arithmetic type, does not accept unsigned type
		 * @return
		 */
		template <signed_number T>
		[[nodiscard]] constexpr math::vector2<T> get_offset_of(
			const pos align,
			typename math::vector2<T>::const_pass_t internal_toAlignSize,
			const math::rect_ortho<T>& external) noexcept{
			math::vector2<T> offset{};
			switch(align & pos::mask_y){
			case pos::bottom :
				offset.y = external.get_end_y() - internal_toAlignSize.y;
				break;
			case pos::top :
				offset.y = external.get_src_y();
				break;
			case pos::center_y :
				offset.y = external.get_src_y() + (external.height() - internal_toAlignSize.y) / static_cast<T>(2);
				break;
			default : break;
			}

			switch(align & pos::mask_x){
			case pos::right :
				offset.x = external.get_end_x() - internal_toAlignSize.x;
				break;
			case pos::left :
				offset.x = external.get_src_x();
				break;
			case pos::center_x :
				offset.x = external.get_src_x() + (external.width() - internal_toAlignSize.x) / static_cast<T>(2);
				break;
			default : break;
			}

			return offset;
		}


		template <signed_number T>
		constexpr math::vector2<T> transform_offset(
			const pos align,
			math::vector2<T> bound_size,
			math::rect_ortho<T> to_transform_inner
		) noexcept{
			if((align & pos::bottom) != pos{}){
				to_transform_inner.src.y = bound_size.y - to_transform_inner.src.y - to_transform_inner.height();
			} else if((align & pos::center_y) != pos{}){
				to_transform_inner.src.y = (bound_size.y - to_transform_inner.height()) / static_cast<T>(2);
			} else if((align & pos::top) != pos{}){
				// Already top-aligned by default logic or do nothing
			}

			if((align & pos::right) != pos{}){
				to_transform_inner.src.x = bound_size.x - to_transform_inner.src.x - to_transform_inner.width();
			} else if((align & pos::center_x) != pos{}){
				to_transform_inner.src.x = (bound_size.x - to_transform_inner.width()) / static_cast<T>(2);
			} else if((align & pos::left) != pos{}){
				// Already left-aligned by default logic or do nothing
			}

			return to_transform_inner.src;
		}


		/**
		 * @brief
		 * @tparam T arithmetic type, does not accept unsigned type
		 * @return
		 */
		template <signed_number T>
		[[nodiscard]] constexpr math::vector2<T> get_offset_of(const pos align, const math::rect_ortho<T>& internal_toAlign,
		                                        const math::rect_ortho<T>& external) noexcept{
			return align::get_offset_of(align, internal_toAlign.extent(), external);
		}
	}
}