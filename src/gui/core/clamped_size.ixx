//
// Created by Matrix on 2024/9/13.
//

export module mo_yanxi.gui.util.clamped_size;

export import mo_yanxi.math.vector2;
import mo_yanxi.gui.util;
import std;

export namespace mo_yanxi::gui{
	template <typename T>
	struct clamped_size{
		using extent_type = math::vector2<T>;
	private:
		extent_type minimumSize{math::vectors::constant2<T>::zero_vec2};
		extent_type maximumSize{math::vectors::constant2<T>::inf_positive_vec2};
		extent_type size{};

	public:
		[[nodiscard]] constexpr clamped_size() = default;

		[[nodiscard]] constexpr explicit clamped_size(const extent_type& size)
			: size{size}{}

		[[nodiscard]] constexpr clamped_size(const extent_type& minimumSize, const extent_type& maximumSize, const extent_type& size)
			: minimumSize{minimumSize},
			  maximumSize{maximumSize},
			  size{size}{}

		[[nodiscard]] constexpr extent_type get_minimum_size() const noexcept{
			return minimumSize;
		}

		[[nodiscard]] constexpr extent_type get_maximum_size() const noexcept{
			return maximumSize;
		}

		[[nodiscard]] constexpr extent_type get_size() const noexcept{
			return size;
		}

		[[nodiscard]] constexpr T get_width() const noexcept{
			return size.x;
		}

		[[nodiscard]] constexpr T get_height() const noexcept{
			return size.y;
		}

		constexpr void set_size_unchecked(const extent_type size) noexcept{
			this->size = size;
		}

		/**
		 * @brief
		 * @return true if width has been changed
		 */
		constexpr bool set_width(T w) noexcept{
			w = std::clamp(w, minimumSize.x, maximumSize.x);
			return util::try_modify(size.x, w);
		}

		/**
		 * @brief
		 * @return true if height has been changed
		 */
		constexpr bool set_height(T h) noexcept{
			h = std::clamp(h, minimumSize.y, maximumSize.y);
			return util::try_modify(size.y, h);
		}

		/**
		 * @brief
		 * @return true if size has been changed
		 */
		constexpr bool set_size(const extent_type s) noexcept{
			bool b{};
			b |= this->set_width(s.x);
			b |= this->set_height(s.y);
			return b;
		}

		/**
		 * @brief
		 * @return true if size has been changed
		 */
		constexpr bool set_minimum_size(extent_type sz) noexcept{
			if(this->minimumSize != sz){
				this->minimumSize = sz;
				return util::try_modify(size, sz.max(size));
			}
			return false;
		}

		/**
		 * @brief
		 * @return true if size has been changed
		 */
		constexpr bool set_maximum_size(extent_type sz) noexcept{
			if(this->maximumSize != sz){
				this->maximumSize = sz;
				return util::try_modify(size, sz.min(size));
			}
			return false;
		}

		[[nodiscard]] math::vec2 clamp(math::vec2 sz) const noexcept{
			return sz.clamp_xy(minimumSize, maximumSize);
		}

		constexpr friend bool operator==(const clamped_size& lhs, const clamped_size& rhs) = default;
	};

	using clamped_fsize = clamped_size<float>;
}
