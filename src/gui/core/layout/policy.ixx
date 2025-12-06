export module mo_yanxi.gui.layout.policies;

export import align;
import std;
import mo_yanxi.math.vector2;
import mo_yanxi.math;

namespace mo_yanxi::gui::layout{
export struct illegal_layout : std::exception{
	[[nodiscard]] explicit illegal_layout(char const* msg)
		: exception(msg){
	}

	[[nodiscard]] illegal_layout() : illegal_layout{"Illegal Layout"}{
	}
};

export constexpr float inline pending_size = std::numeric_limits<float>::infinity();

export bool is_size_pending(float size) noexcept{
	return std::isinf(size);
}

//TODO make it hori/vert/nullopt?
export enum class layout_policy{
	none,
	hori_major,
	vert_major,
};

export
constexpr layout_policy transpose_layout(layout_policy policy) noexcept{
	switch(policy){
	case layout_policy::none : return layout_policy::none;
	case layout_policy::hori_major : return layout_policy::vert_major;
	case layout_policy::vert_major : return layout_policy::hori_major;
	default : std::unreachable();
	}
}

export enum class expand_policy{
	resize_to_fit,
	passive,
	prefer,
};

export enum class size_category{
	passive,
	scaling,
	mastering,
	pending,
};

export struct stated_size{
	size_category type{size_category::passive};
	float value{1.};

	constexpr friend bool operator==(const stated_size& lhs, const stated_size& rhs) noexcept = default;

	[[nodiscard]] constexpr bool mastering() const noexcept{
		return type == size_category::mastering;
	}

	[[nodiscard]] constexpr float master_or(const float v) const noexcept{
		return type == size_category::mastering ? value : v;
	}

	[[nodiscard]] constexpr bool pending() const noexcept{
		return type == size_category::pending;
	}

	constexpr explicit(false) operator float() const noexcept{
		return mastering() ? value : 0.0f;
	}

	[[nodiscard]] constexpr stated_size decay() const noexcept{
		switch(type){
		case size_category::scaling :[[fallthrough]];
		case size_category::pending : return {size_category::passive, value};
		default : return *this;
		}
	}

	constexpr void try_promote_by(float mastering_size) noexcept{
		if(mastering()){
			value = std::max(mastering_size, value);
		} else{
			type = size_category::mastering;
			value = mastering_size;
		}
	}

	constexpr void try_promote_by(const stated_size o) noexcept{
		switch(o.type){
		case size_category::mastering : if(mastering()){
				value = std::max(value, o.value);
			} else{
				*this = o;
			}
			return;
		case size_category::scaling : if(type == size_category::scaling){
				value = std::max(value, o.value);
			} else if(type == size_category::passive){
				*this = o;
			}
			return;
		case size_category::pending : if(type == size_category::pending){
				value = std::max(value, o.value);
			} else if(type == size_category::passive){
				type = size_category::pending;
				value = std::max(value, o.value);
			}
			return;
		case size_category::passive : if(type == size_category::passive){
				value = std::max(value, o.value);
			}
			return;
		default : std::unreachable();
		}
	}
};

export struct stated_extent{
	stated_size width{size_category::passive, 1};
	stated_size height{size_category::passive, 1};

	[[nodiscard]] constexpr stated_extent() noexcept = default;

	[[nodiscard]] constexpr stated_extent(const stated_size width, const stated_size height) noexcept
		: width(width),
		  height(height){
	}

	[[nodiscard]] constexpr stated_extent(const float width, const float height) noexcept
		: width(math::isinf(width) ? size_category::pending : size_category::mastering, width),
		  height(math::isinf(height) ? size_category::pending : size_category::mastering, height){
	}

	constexpr void try_add(math::vec2 extent) noexcept{
		if(width.mastering()) width.value += extent.x;
		if(height.mastering()) height.value += extent.y;
	}

	constexpr friend bool operator==(const stated_extent& lhs, const stated_extent& rhs) noexcept = delete;

	[[nodiscard]] constexpr bool fully_pending() const noexcept{
		return width.type == size_category::pending && height.type == size_category::pending;
	}

	constexpr void collapse(const math::vec2 size) noexcept{
		if(!width.pending()) width = {size_category::mastering, size.x};
		if(!height.pending()) height = {size_category::mastering, size.y};
	}

	[[nodiscard]] constexpr bool mastering() const noexcept{
		if(width.mastering()){
			return height.mastering() || height.type == size_category::scaling;
		}

		if(height.mastering()){
			return width.type == size_category::scaling;
		}

		return false;
	}

	[[nodiscard]] constexpr stated_extent promote() const noexcept{
		if(width.mastering() && height.type == size_category::scaling){
			return {width, {size_category::mastering, width.value * height.value}};
		}
		if(height.mastering() && width.type == size_category::scaling){
			return {{size_category::mastering, width.value * height.value}, height};
		}
		return *this;
	}

	constexpr void try_scl(math::vec2 scl) noexcept{
		if(width.mastering()){
			width.value *= scl.x;
		}
		if(height.mastering()){
			height.value *= scl.y;
		}
	}

	[[nodiscard]] math::vec2 potential_max_size() const noexcept{
		math::vec2 rst{width, height};
		if(width.pending()) rst.x = pending_size;
		if(height.pending()) rst.y = pending_size;
		return rst;
	}
};

export constexpr stated_extent extent_by_external{{size_category::pending}, {size_category::pending}};

export struct optional_mastering_extent{
private:
	float width_{};
	float height_{};

public:
	[[nodiscard]] constexpr optional_mastering_extent() noexcept = default;

	[[nodiscard]] constexpr optional_mastering_extent(const float x, const float y) noexcept
		: width_(x),
		  height_(y){
	}

	[[nodiscard]] constexpr explicit(false) optional_mastering_extent(const math::vec2 vec2) noexcept
		: width_(vec2.x),
		  height_(vec2.y){
	}

	[[nodiscard]] constexpr explicit(false) optional_mastering_extent(const stated_extent vec2) noexcept
		: width_(
			  vec2.width.pending()
				  ? pending_size
				  : vec2.width.mastering()
				  ? vec2.width.value
				  : std::numeric_limits<float>::signaling_NaN()),
		  height_(vec2.height.pending()
			          ? pending_size
			          : vec2.height.mastering()
			          ? vec2.height.value
			          : std::numeric_limits<float>::signaling_NaN()){
	}


	[[nodiscard]] constexpr bool fully_dependent() const noexcept{
		return math::isinf(width_) && math::isinf(height_);
	}

	[[nodiscard]] constexpr bool fully_mastering() const noexcept{
		return !math::isinf(width_) && !math::isinf(height_);
	}

	[[nodiscard]] constexpr math::bool2 get_pending() const noexcept{
		return {math::isinf(width_), math::isinf(height_)};
	}

	[[nodiscard]] constexpr math::bool2 get_mastering() const noexcept{
		return {!math::isinf(width_), !math::isinf(height_)};
	}

	[[nodiscard]] constexpr bool width_pending() const noexcept{
		return math::isinf(width_);
	}

	[[nodiscard]] constexpr bool height_pending() const noexcept{
		return math::isinf(height_);
	}

	[[nodiscard]] constexpr bool width_mastering() const noexcept{
		return !math::isinf(width_);
	}

	[[nodiscard]] constexpr bool height_mastering() const noexcept{
		return !math::isinf(height_);
	}

	[[nodiscard]] constexpr math::vec2 potential_extent() const noexcept{
		return {width_, height_};
	}

	[[nodiscard]] constexpr float potential_width() const noexcept{
		return width_;
	}

	[[nodiscard]] constexpr float potential_height() const noexcept{
		return height_;
	}

	constexpr void set_width(const float width) noexcept{
		this->width_ = width;
	}

	constexpr void set_height(const float height) noexcept{
		this->height_ = height;
	}

	constexpr void set_width_pending() noexcept{
		width_ = pending_size;
	}

	constexpr void set_height_pending() noexcept{
		height_ = pending_size;
	}

	constexpr void set_minor_pending(layout_policy policy) noexcept{
		switch(policy){
		case layout_policy::none : return;
		case layout_policy::hori_major : set_height_pending();
			return;
		case layout_policy::vert_major : set_width_pending();
			return;
		default : std::unreachable();
		}
	}

	constexpr void set_major_pending(layout_policy policy) noexcept{
		switch(policy){
		case layout_policy::none : return;
		case layout_policy::hori_major : set_width_pending();
			return;
		case layout_policy::vert_major : set_height_pending();
			return;
		default : std::unreachable();
		}
	}

	constexpr void collapse(const math::vec2 size) noexcept{
		auto [dx, dy] = get_pending();
		if(!dx) width_ = size.x;
		if(!dy) height_ = size.y;
	}

	constexpr void apply(const math::vec2 size) noexcept{
		auto [dx, dy] = get_pending();
		if(dx) width_ = size.x;
		if(dy) height_ = size.y;
	}

	constexpr void collapse_min(const math::vec2 size) noexcept{
		auto [dx, dy] = get_pending();
		if(!dx) width_ = math::min(size.x, width_);
		if(!dy) height_ = math::min(size.y, height_);
	}
};

export{
	[[nodiscard]] constexpr std::array<float align::spacing::*, 4> get_pad_ptr(layout_policy policy) noexcept{
		if(policy == layout_policy::vert_major){
			return {
					&align::spacing::top,
					&align::spacing::bottom,

					&align::spacing::left,
					&align::spacing::right,
				};
		} else{
			return {
					&align::spacing::left,
					&align::spacing::right,

					&align::spacing::top,
					&align::spacing::bottom,
				};
		}
	}

	template <typename T>
	struct paired_target{
		T major;
		T minor;
	};

	[[nodiscard]] constexpr paired_target<stated_size stated_extent::*> get_extent_ptr(layout_policy policy) noexcept{
		if(policy == layout_policy::vert_major){
			return {
					&stated_extent::height,
					&stated_extent::width,
				};
		} else{
			return {
					&stated_extent::width,
					&stated_extent::height
				};
		}
	}

	template <typename T = float>
	[[nodiscard]] constexpr auto get_vec_ptr(layout_policy policy) noexcept{
		if(policy == layout_policy::vert_major){
			return paired_target{
					&math::vector2<T>::y,
					&math::vector2<T>::x,
				};
		} else{
			return paired_target{
					&math::vector2<T>::x,
					&math::vector2<T>::y,
				};
		}
	}


	paired_target<float> get_pad_extent(layout_policy policy, const align::spacing& boarder) noexcept{
		const auto [
			pad_major_src,
			pad_major_dst,
			pad_minor_src,
			pad_minor_dst] = get_pad_ptr(policy);

		return {
				boarder.*pad_major_src + boarder.*pad_major_dst,
				boarder.*pad_minor_src + boarder.*pad_minor_dst
			};
	}
}
}
