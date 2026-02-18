module;

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

	constexpr operator std::bitset<total_bits>() const noexcept{
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
		mask = underlying_type{b} << idx | mask & ~(underlying_type{b} << idx);
	}

	constexpr bool operator[](unsigned idx) const noexcept{
		assert(idx < sizeof(underlying_type) * 8);
		return mask & (1U << idx);
	}

	constexpr bool operator==(const render_target_mask&) const noexcept = default;

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
	mode,
	push_constant,
	blend_switch,
	//TODO other states...
	reserved_count
};

export
enum struct draw_mode : std::uint32_t{
	def,
	msdf,

	COUNT_or_fallback,
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
struct draw_config{
	draw_mode mode{};
	blending_type blending{};
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

export
template <typename T>
struct draw_state_config_deduce{};

export
template <typename T>
concept draw_state_config_deduceable = requires{
	requires std::same_as<typename draw_state_config_deduce<T>::value_type, std::uint32_t>;
};

template <>
struct draw_state_config_deduce<fx::blit_config> : std::integral_constant<std::uint32_t, std::to_underlying(state_type::blit)>{
};

template <>
struct draw_state_config_deduce<draw_config> : std::integral_constant<std::uint32_t, std::to_underlying(state_type::mode)>{
};


export
template <typename T>
constexpr inline std::uint32_t draw_state_index_deduce_v = draw_state_config_deduce<T>::value;

}
