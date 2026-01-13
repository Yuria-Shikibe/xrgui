module;

#include <cassert>

export module mo_yanxi.gui.draw_config;

import std;

namespace mo_yanxi::gui::draw_config{


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
}
