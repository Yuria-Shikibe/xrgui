//
// Created by Matrix on 2025/11/1.
//

export module mo_yanxi.gui.infrastructure:events;

import mo_yanxi.math.vector2;
import mo_yanxi.input_handle;
import std;

import :elem_ptr;

namespace mo_yanxi::gui::events{

export
using key_set = input_handle::key_set;

export
struct click{
	math::vec2 pos;
	key_set key;

	bool within_elem(const elem& elem, float margin = 0) const noexcept;
};


export
struct scroll{
	math::vec2 delta;
	input_handle::mode mode;
};

export
struct cursor_move{
	math::vec2 src;
	math::vec2 dst;

	[[nodiscard]] math::vec2 delta() const noexcept{
		return dst - src;
	}
};

export
struct drag{
	math::vec2 src;
	math::vec2 dst;
	key_set key;

	[[nodiscard]] math::vec2 delta() const noexcept{
		return dst - src;
	}
};

export
enum struct op_afterwards{
	intercepted,
	fall_through,
};

}