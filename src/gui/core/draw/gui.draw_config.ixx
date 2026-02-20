module;

#include "../../src.backends/universal/backend_detect.h"

#ifdef HAS_VULKAN
#include <vulkan/vulkan.h>
#endif

#include <cassert>
#include <mo_yanxi/enum_operator_gen.hpp>

export module mo_yanxi.gui.fx.config;

import std;

import mo_yanxi.math.rect_ortho;

namespace mo_yanxi::gui::fx{

export
struct blit_pipeline_config{
	std::uint32_t pipeline_index;
	std::uint32_t inout_define_index = std::numeric_limits<std::uint32_t>::max();
};

export
struct blit_config{
	math::rect_ortho_trivial<int> blit_region;
	blit_pipeline_config pipe_info;

	bool use_default_inouts() const noexcept{
		return pipe_info.inout_define_index == std::numeric_limits<std::uint32_t>::max();
	}

	void get_clamped_to_positive() noexcept{
		if(blit_region.src.x < 0){
			blit_region.extent.x += blit_region.src.x;
			blit_region.src.x = 0;
			if(blit_region.extent.x < 0)blit_region.extent.x = 0;
		}
		if(blit_region.src.y < 0){
			blit_region.extent.y += blit_region.src.y;
			blit_region.src.y = 0;
			if(blit_region.extent.y < 0)blit_region.extent.y = 0;
		}
	}

	math::usize2 get_dispatch_groups() const noexcept{
		return (blit_region.extent.as<unsigned>() + math::usize2{15, 15}) / math::usize2{16, 16};
	}
};

export
struct render_target_mask{
    using underlying_type = std::uint32_t;

    underlying_type mask;

    static constexpr unsigned total_bits = sizeof(underlying_type) * 8;

    explicit(false) constexpr operator std::bitset<total_bits>() const noexcept{
       return {mask};
    }

    constexpr bool none() const noexcept{
       return mask == underlying_type{};
    }

    constexpr bool all() const noexcept{
       return mask == ~underlying_type{};
    }

    constexpr bool any() const noexcept{
       return mask != 0;
    }

    constexpr unsigned popcount() const noexcept{
       return std::popcount(mask);
    }

    constexpr unsigned get_highest_bit_size() const noexcept{
       return total_bits - std::countl_zero(mask);
    }

    constexpr unsigned get_lowest_bit_index() const noexcept{
       return std::countr_zero(mask);
    }

    constexpr void set(unsigned idx, bool b) noexcept{
       // 修正：无论 b 是 true 还是 false，都需要使用 1 来创建反码掩码清除原有位
       mask = (mask & ~(underlying_type{1} << idx)) | (underlying_type{b} << idx);
    }

    constexpr bool operator[](unsigned idx) const noexcept{
       assert(idx < sizeof(underlying_type) * 8);
       return mask & (1U << idx);
    }

    constexpr bool operator==(const render_target_mask&) const noexcept = default;

    // ------------------------------------------------------------------------
    // 新增：常用位运算重载
    // ------------------------------------------------------------------------

    // 按位取反 (NOT)
    constexpr render_target_mask operator~() const noexcept {
       return {~mask};
    }

    // 按位与 (AND)
    constexpr render_target_mask operator&(const render_target_mask& rhs) const noexcept {
       return {mask & rhs.mask};
    }
    constexpr render_target_mask& operator&=(const render_target_mask& rhs) noexcept {
       mask &= rhs.mask;
       return *this;
    }

    // 按位或 (OR)
    constexpr render_target_mask operator|(const render_target_mask& rhs) const noexcept {
       return {mask | rhs.mask};
    }
    constexpr render_target_mask& operator|=(const render_target_mask& rhs) noexcept {
       mask |= rhs.mask;
       return *this;
    }

    // 按位异或 (XOR)
    constexpr render_target_mask operator^(const render_target_mask& rhs) const noexcept {
       return {mask ^ rhs.mask};
    }
    constexpr render_target_mask& operator^=(const render_target_mask& rhs) noexcept {
       mask ^= rhs.mask;
       return *this;
    }

    // 左移 (Shift Left)
    constexpr render_target_mask operator<<(unsigned shift) const noexcept {
       return {mask << shift};
    }
    constexpr render_target_mask& operator<<=(unsigned shift) noexcept {
       mask <<= shift;
       return *this;
    }

    // 右移 (Shift Right)
    constexpr render_target_mask operator>>(unsigned shift) const noexcept {
       return {mask >> shift};
    }
    constexpr render_target_mask& operator>>=(unsigned shift) noexcept {
       mask >>= shift;
       return *this;
    }

    // ------------------------------------------------------------------------

    template <std::invocable<unsigned> Fn>
    constexpr void for_each_popbit(this render_target_mask self, Fn&& fn) noexcept(std::is_nothrow_invocable_v<Fn, unsigned>){
       for(unsigned i = self.get_lowest_bit_index(); i < self.get_highest_bit_size(); ++i){
          if(self[i]){
             fn(i);
          }
       }
    }
};

export
struct layer_param{
	std::uint32_t layer_index;

	constexpr bool operator==(std::uint32_t idx) const noexcept{
		return layer_index == idx;
	}
};

export
using layer_param_pass_t = const layer_param;

export
struct ui_state{
	float time;
	std::uint32_t _cap[3];
};

export
struct slide_line_config{
	float angle{45};
	float scale{1};

	float spacing{20};
	float stroke{25};

	float speed{15};
	float phase{0};

	float margin{0.05f};

	float opacity{0};
};


export
template <typename T>
constexpr inline bool is_vertex_stage_only = requires{
	typename T::tag_vertex_only;
};


export
enum struct state_type{
	blit,
	pipe,
	push_constant,
	set_color_blend_enable,
	set_color_blend_equation,
	set_color_write_mask,

	set_scissor,
	set_viewport,

	fill_color,
	//TODO other states...
	reserved_count
};

template <state_type StateType, bool RequiresMinorTag = false, bool IsIdempotent = true>
struct state_type_deduce_base{
	static constexpr state_type type{StateType};
	static constexpr bool requires_minor_tag{RequiresMinorTag};
	static constexpr bool is_idempotent{IsIdempotent};
};

export
template <typename T>
struct state_type_deduce : state_type_deduce_base<state_type::reserved_count>{

};

export
template <typename T>
concept state_type_deducable = state_type_deduce<T>::type != state_type::reserved_count;



export
enum struct batch_draw_mode : std::uint32_t{
	def,
	msdf,
};

export
enum struct blending_type : std::uint16_t{
	alpha,
	add,
	reverse,
	lock_alpha,
	SIZE,
};

export
constexpr inline std::uint32_t use_default_pipeline = std::numeric_limits<std::uint32_t>::max();

export
struct pipeline_config{
	render_target_mask draw_targets{};
	std::uint32_t pipeline_index{use_default_pipeline};

	constexpr bool use_fallback_pipeline() const noexcept{
		return pipeline_index == use_default_pipeline;
	}
};

export
enum struct primitive_draw_mode : std::uint32_t{
	none,

	draw_slide_line = 1 << 0,
};

BITMASK_OPS(export , primitive_draw_mode);
//
// export
// template <typename T>
// struct draw_state_config_deduce{};
//
// export
// template <typename T>
// concept draw_state_config_deduceable = requires{
// 	requires std::same_as<typename draw_state_config_deduce<T>::value_type, std::uint32_t>;
// };
//
// template <>
// struct draw_state_config_deduce<fx::blit_config> : std::integral_constant<std::uint32_t, std::to_underlying(state_type::blit)>{
// };
//
// template <>
// struct draw_state_config_deduce<pipeline_config> : std::integral_constant<std::uint32_t, std::to_underlying(state_type::pipe)>{
// };

// export
// template <typename T>
// constexpr inline std::uint32_t draw_state_index_deduce_v = draw_state_config_deduce<T>::value;

export using blend_enable_flag = VkBool32;
export using blend_write_mask_type = VkColorComponentFlags;

export
struct blend_equation{
	VkBlendFactor    src_color_blend_factor;
	VkBlendFactor    dst_color_blend_factor;
	VkBlendOp        color_blend_op;
	VkBlendFactor    src_alpha_blend_factor;
	VkBlendFactor    dst_alpha_blend_factor;
	VkBlendOp        alpha_blend_op;

	constexpr explicit(false) operator VkColorBlendEquationEXT() const noexcept{
		return std::bit_cast<VkColorBlendEquationEXT>(*this);
	}
};

export namespace blend {

// ==========================================
// 直通 Alpha (Straight Alpha) 混合模式
// 适用于未预乘 Alpha 的普通纹理或颜色
// ==========================================

// 标准 Alpha 混合
constexpr inline blend_equation standard = {
    .src_color_blend_factor = VK_BLEND_FACTOR_SRC_ALPHA,
    .dst_color_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    .color_blend_op         = VK_BLEND_OP_ADD,
    .src_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
    .dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    .alpha_blend_op         = VK_BLEND_OP_ADD
};

// 正片叠底
constexpr inline blend_equation multiply = {
    .src_color_blend_factor = VK_BLEND_FACTOR_DST_COLOR,
    .dst_color_blend_factor = VK_BLEND_FACTOR_ZERO,
    .color_blend_op         = VK_BLEND_OP_ADD,
    .src_alpha_blend_factor = VK_BLEND_FACTOR_DST_ALPHA,
    .dst_alpha_blend_factor = VK_BLEND_FACTOR_ZERO,
    .alpha_blend_op         = VK_BLEND_OP_ADD
};

// 滤色 (注：直通 Alpha 在半透明边缘会存在不可避免的计算瑕疵，这里提供最接近的近似实现)
constexpr inline blend_equation screen = {
    .src_color_blend_factor = VK_BLEND_FACTOR_SRC_ALPHA,
    .dst_color_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
    .color_blend_op         = VK_BLEND_OP_ADD,
    .src_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
    .dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    .alpha_blend_op         = VK_BLEND_OP_ADD
};

// 加法发光
constexpr inline blend_equation additive = {
    .src_color_blend_factor = VK_BLEND_FACTOR_SRC_ALPHA,
    .dst_color_blend_factor = VK_BLEND_FACTOR_ONE,
    .color_blend_op         = VK_BLEND_OP_ADD,
    .src_alpha_blend_factor = VK_BLEND_FACTOR_ZERO,
    .dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
    .alpha_blend_op         = VK_BLEND_OP_ADD
};

// 减法
constexpr inline blend_equation subtractive = {
    .src_color_blend_factor = VK_BLEND_FACTOR_SRC_ALPHA,
    .dst_color_blend_factor = VK_BLEND_FACTOR_ONE,
    .color_blend_op         = VK_BLEND_OP_REVERSE_SUBTRACT,
    .src_alpha_blend_factor = VK_BLEND_FACTOR_ZERO,
    .dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
    .alpha_blend_op         = VK_BLEND_OP_ADD
};

// 完全不透明 / 覆盖
constexpr inline blend_equation opaque = {
    .src_color_blend_factor = VK_BLEND_FACTOR_ONE,
    .dst_color_blend_factor = VK_BLEND_FACTOR_ZERO,
    .color_blend_op         = VK_BLEND_OP_ADD,
    .src_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
    .dst_alpha_blend_factor = VK_BLEND_FACTOR_ZERO,
    .alpha_blend_op         = VK_BLEND_OP_ADD
};


// ==========================================
// 预乘 Alpha (Premultiplied Alpha - PMA) 混合模式
// 适用于已预乘 Alpha 的纹理或颜色
// ==========================================
namespace pma {

    // 标准 PMA 混合 (GUI 与现代渲染推荐)
    constexpr inline blend_equation standard = {
        .src_color_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_color_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .color_blend_op         = VK_BLEND_OP_ADD,
        .src_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alpha_blend_op         = VK_BLEND_OP_ADD
    };

    // PMA 正片叠底 (正确处理透明区域不发黑)
    constexpr inline blend_equation multiply = {
        .src_color_blend_factor = VK_BLEND_FACTOR_DST_COLOR,
        .dst_color_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .color_blend_op         = VK_BLEND_OP_ADD,
        .src_alpha_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alpha_blend_op         = VK_BLEND_OP_ADD
    };

    // PMA 滤色 (数学上完美的 Screen)
    constexpr inline blend_equation screen = {
        .src_color_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_color_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
        .color_blend_op         = VK_BLEND_OP_ADD,
        .src_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alpha_blend_op         = VK_BLEND_OP_ADD
    };

    // PMA 加法发光
    constexpr inline blend_equation additive = {
        .src_color_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_color_blend_factor = VK_BLEND_FACTOR_ONE,
        .color_blend_op         = VK_BLEND_OP_ADD,
        .src_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
        .alpha_blend_op         = VK_BLEND_OP_ADD
    };

    // PMA 减法
    constexpr inline blend_equation subtractive = {
        .src_color_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_color_blend_factor = VK_BLEND_FACTOR_ONE,
        .color_blend_op         = VK_BLEND_OP_REVERSE_SUBTRACT,
        .src_alpha_blend_factor = VK_BLEND_FACTOR_ZERO,
        .dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
        .alpha_blend_op         = VK_BLEND_OP_ADD
    };

    // 完全不透明 / 覆盖 (与直通模式相同)
    constexpr inline blend_equation opaque = {
        .src_color_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_color_blend_factor = VK_BLEND_FACTOR_ZERO,
        .color_blend_op         = VK_BLEND_OP_ADD,
        .src_alpha_blend_factor = VK_BLEND_FACTOR_ONE,
        .dst_alpha_blend_factor = VK_BLEND_FACTOR_ZERO,
        .alpha_blend_op         = VK_BLEND_OP_ADD
    };

}
}

export
struct scissor{
	math::i32point2 pos;
	math::u32size2 size;

#ifdef HAS_VULKAN
	constexpr explicit(false) operator VkRect2D() const noexcept{
		return std::bit_cast<VkRect2D>(*this);
	}
#endif

};

export
struct viewport : math::raw_frect{

#ifdef HAS_VULKAN
	constexpr explicit(false) operator VkViewport() const noexcept{
		return {
			.x = src.x,
			.y = src.y,
			.width = extent.x,
			.height = extent.y,
			.minDepth = 0.f,
			.maxDepth = 1.f
		};
	}
#endif

};

export
union color_clear_value{
	std::monostate none{};

#ifdef HAS_VULKAN
	VkClearColorValue vk;

	explicit(false) operator const VkClearColorValue&() const noexcept{
		return vk;
	}
#endif

	constexpr bool is_clear() const noexcept;
};

constexpr bool color_clear_value::is_clear() const noexcept{
	static constexpr std::array<std::byte, sizeof(color_clear_value)> empty{};
	if consteval{
		return std::bit_cast<decltype(empty)>(*this) == empty;
	}
	return std::memcmp(this, empty.data(), sizeof(empty)) == 0;
}

template <>
struct state_type_deduce<blend_equation> : state_type_deduce_base<state_type::set_color_blend_equation, true>{
};

template <>
struct state_type_deduce<scissor> : state_type_deduce_base<state_type::set_scissor, false>{
};

template <>
struct state_type_deduce<viewport> : state_type_deduce_base<state_type::set_viewport, false>{
};

template <>
struct state_type_deduce<blit_config> : state_type_deduce_base<state_type::blit, false, false>{
};

template <>
struct state_type_deduce<pipeline_config> : state_type_deduce_base<state_type::pipe>{
};

template <>
struct state_type_deduce<color_clear_value> : state_type_deduce_base<state_type::fill_color, true, false>{
};




}
