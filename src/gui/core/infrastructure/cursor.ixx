//
// Created by Matrix on 2025/11/27.
//

export module mo_yanxi.gui.infrastructure:cursor;

import :defines;
import :elem_ptr;

export import mo_yanxi.gui.renderer.frontend;
export import mo_yanxi.gui.alloc;
export import mo_yanxi.allocator_aware_unique_ptr;
export import mo_yanxi.math.rect_ortho;
export import mo_yanxi.math.vector2;

namespace mo_yanxi::gui{
struct scene_base;

namespace style{
export
enum class cursor_arrow_direction{
	left,
	right,
	up, // 假设 Y 轴向下递增 (UI 坐标系)
	down
};

struct ArrowGeometry{
	vec2 p1; // 箭头一侧的端点
	vec2 p2; // 箭头的顶点 (Tip)
	vec2 p3; // 箭头另一侧的端点
	float thickness;
};

/**
 * @brief 计算位于矩形外部的箭头多边形顶点
 * @param pos 矩形左上角(或包围盒最小点)
 * @param extent 矩形的尺寸 (宽高)
 * @param dir 箭头的方向
 * @param arrow_size 箭头的翼长(沿方向轴的延伸长度)
 * @param margin 箭头底部距离矩形边缘的留白
 * @param thickness 箭头的线条宽度
 */
inline ArrowGeometry calculate_rect_arrow(
	vec2 pos,
	vec2 extent,
	cursor_arrow_direction dir,
	float arrow_size = 8.0f,
	float margin = 7.0f,
	float thickness = 1.f) noexcept{
	// 1. 获取方向向量
	vec2 dir_v{0.0f, 0.0f};
	switch(dir){
	case cursor_arrow_direction::left : dir_v = {-1.0f, 0.0f};
		break;
	case cursor_arrow_direction::right : dir_v = {1.0f, 0.0f};
		break;
	case cursor_arrow_direction::up : dir_v = {0.0f, -1.0f};
		break;
	case cursor_arrow_direction::down : dir_v = {0.0f, 1.0f};
		break;
	}

	// 2. 获取正交向量 (逆时针旋转 90 度: x' = -y, y' = x)
	vec2 ortho_v{-dir_v.y, dir_v.x};

	// 3. 计算矩形中心点以及边缘连接点
	vec2 center = pos + extent * 0.5f;

	// 利用 vec2 的 operator*(const_pass_t) 进行逐分量乘积，快速定位到矩形指定方向的边缘
	vec2 half_extent = extent * 0.5f;
	vec2 edge_pt = center + (half_extent * dir_v);

	// 4. 计算箭头顶点 (Tip) 和 底部中心点
	// 顶点处于最外侧，箭头朝向外部
	vec2 tip = edge_pt + dir_v * (margin + arrow_size);
	vec2 base_center = tip - dir_v * arrow_size;

	// 5. 利用正交向量计算两侧翼的端点 (因为偏移量都是 arrow_size，所以形成的夹角刚好是 90 度)
	vec2 p1 = base_center + ortho_v * arrow_size;
	vec2 p2 = tip;
	vec2 p3 = base_center - ortho_v * arrow_size;

	return {p1, p2, p3, thickness};
}

/**
 * @brief 计算箭头的粗略但绝对安全的包围盒
 */
inline rect calculate_arrow_aabb(const ArrowGeometry& arrow) noexcept{
	// 利用 vec2 提供的 copy, min, max 实现链式调用获取几何极值点
	vec2 p_min = arrow.p1.copy().min(arrow.p2).min(arrow.p3);
	vec2 p_max = arrow.p1.copy().max(arrow.p2).max(arrow.p3);

	// 对于 90 度角的折线 (Miter Join)，它的尖角斜接长度约为 thickness * 1.414。
	// 为了确保包围盒"完全包围"而不要求极致精确，我们在各方向上外扩厚度的 1.5 倍即可保证绝对安全。
	float padding_val = arrow.thickness * 1.5f;
	vec2 padding{padding_val, padding_val};

	return rect{tags::from_extent, p_min - padding, p_max + padding};
}


export
struct cursor{
	virtual ~cursor() = default;
	virtual rect draw(gui::renderer_frontend& renderer, math::raw_frect region,
		std::span<const elem* const> inbound_stack) const = 0;

	rect get_bound(math::raw_frect region) const{
		return rect{tags::from_extent, region.src - region.extent, region.extent * 2}.expand(16);
	}
};


export
enum struct cursor_type : std::uint8_t{
	none,
	regular,
	drag,
	clickable,
	textarea,
	scroll_hori,
	scroll_vert,
	scroll,

	RESERVED_COUNT,
};

export
enum struct cursor_decoration_type : std::uint8_t{
	none,
	tooltip,

	to_left,
	to_right,
	to_up,
	to_down,

	RESERVED_COUNT,
};

inline constexpr std::size_t dcor_max_count = 7;

export
struct cursor_style{
	cursor_type main;
	std::array<cursor_decoration_type, dcor_max_count> dcor;

	constexpr bool push_dcor(cursor_decoration_type dcor_type) noexcept{
		auto sz = get_dcor_size();
		if(sz == dcor.size()) return false;
		dcor[sz] = dcor_type;
		return true;
	}

	constexpr unsigned get_dcor_size() const noexcept{
		static_assert(std::endian::native == std::endian::little);
		auto self = std::bit_cast<std::uint64_t>(*this) | 1;
		return dcor_max_count - std::countl_zero(self) / std::numeric_limits<std::uint8_t>::digits;
	}
};

constexpr auto sz = cursor_style{
		.dcor = {cursor_decoration_type::tooltip, cursor_decoration_type::tooltip, cursor_decoration_type::tooltip}
	}.get_dcor_size();
static_assert(sz == 3);
}

namespace assets::builtin::cursor{
export
struct default_cursor_regular : public style::cursor{
	rect draw(gui::renderer_frontend& renderer, math::raw_frect region,
		std::span<const elem* const> inbound_stack) const override;
};


export
struct default_cursor_drag : public style::cursor{
	rect draw(gui::renderer_frontend& renderer, math::raw_frect region,
		std::span<const elem* const> inbound_stack) const override;
};

export
struct default_cursor_arrow : public style::cursor{
	style::cursor_arrow_direction direction;

	[[nodiscard]] explicit default_cursor_arrow(style::cursor_arrow_direction direction)
		: direction(direction){
	}

	rect draw(gui::renderer_frontend& renderer, math::raw_frect region,
		std::span<const elem* const> inbound_stack) const override;
};

export constexpr inline default_cursor_regular default_cursor;
}

export
struct cursor_drawer{
	const style::cursor* main;
	const style::cursor* dcor[style::dcor_max_count];

	rect draw(gui::scene& scene, vec2 cursor_size_) const;
};

struct cursor_collection{
private:
	using cursor_alloc = mr::unvs_allocator<style::cursor>;
	using ptr_type = allocator_aware_poly_unique_ptr<style::cursor, cursor_alloc>;
	using container = mr::vector<ptr_type>;
	container cursors_{std::to_underlying(style::cursor_type::RESERVED_COUNT)};
	container decorations_{std::to_underlying(style::cursor_decoration_type::RESERVED_COUNT)};

	math::vec2 cursor_size_{32, 32};

	void add(container cursor_collection::* which, std::size_t where, ptr_type&& ptr){
		auto& cont = this->*which;
		cont.resize(std::max(cursors_.size(), where + 1));
		cont[where] = std::move(ptr);
	}

public:
	[[nodiscard]] math::vec2 get_cursor_size() const noexcept{
		return cursor_size_;
	}

	void set_cursor_size(const math::vec2 cursor_size) noexcept{
		cursor_size_ = cursor_size;
	}

	template <std::derived_from<style::cursor> T, typename... Args>
		requires (std::constructible_from<T, Args&&...>)
	void add_cursor(const style::cursor_type type, Args&&... args){
		const std::size_t sz = std::to_underlying(type);
		this->add(&cursor_collection::cursors_, sz,
			mo_yanxi::make_allocate_aware_poly_unique<T, style::cursor>(cursor_alloc{cursors_.get_allocator()},
				std::forward<Args>(args)...));
	}

	template <std::derived_from<style::cursor> T, typename... Args>
		requires (std::constructible_from<T, Args&&...>)
	void add_cursor(const style::cursor_decoration_type type, Args&&... args){
		const std::size_t sz = std::to_underlying(type);
		this->add(&cursor_collection::decorations_, sz,
			mo_yanxi::make_allocate_aware_poly_unique<T, style::cursor>(cursor_alloc{cursors_.get_allocator()},
				std::forward<Args>(args)...));
	}

	[[nodiscard]] cursor_drawer get_drawers(style::cursor_style style_pair) const noexcept{
		const auto sz_main = std::to_underlying(style_pair.main);
		auto sz = style_pair.get_dcor_size();

		cursor_drawer rst{
				sz_main == 0
					? nullptr
					: sz_main < cursors_.size()
					? cursors_[sz_main].get()
					: cursors_[std::to_underlying(style::cursor_type::regular)].get(),
			};

		for(unsigned i = 0; i < sz; ++i){
			const auto sz_dcor = std::to_underlying(style_pair.dcor[i]);
			rst.dcor[i] = sz_dcor < decorations_.size() ? decorations_[sz_dcor].get() : nullptr;
		}

		return rst;
	}
};
}
