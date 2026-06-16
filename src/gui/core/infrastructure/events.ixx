//

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

	math::vec2 get_content_pos(const elem& elem) const noexcept;

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
enum struct event_phase : std::uint8_t{
	preview,
	target,
	bubble,
	default_action,
};

export
enum struct gui_event_type : std::uint8_t{
	pointer_move,
	pointer_down,
	pointer_up,
	pointer_drag,
	pointer_click,
	wheel,
	key_down,
	key_up,
	key_repeat,
	text_input,
	ime_composition,
	focus_lost,
};

export
struct event_context{
	gui_event_type type{};
	event_phase phase{event_phase::target};
	elem* target{};
	elem* current{};
	std::span<elem* const> path{};

	bool handled{};
	bool propagation_stopped{};
	bool immediate_propagation_stopped{};
	bool default_prevented{};

	void mark_handled() noexcept{
		handled = true;
	}

	void stop_propagation() noexcept{
		propagation_stopped = true;
	}

	void stop_immediate_propagation() noexcept{
		handled = true;
		propagation_stopped = true;
		immediate_propagation_stopped = true;
	}

	void prevent_default() noexcept{
		default_prevented = true;
	}

	[[nodiscard]] explicit operator bool() const noexcept{
		return handled;
	}
};


//TODO rename it

export
enum struct op_afterwards{
	intercepted,
	fall_through,
};

export
struct event_rst{
	elem* e{};

	[[nodiscard]] explicit operator bool() const noexcept{
		return e != nullptr;
	}

	[[nodiscard]] op_afterwards op() const noexcept{
		return e ? op_afterwards::intercepted : op_afterwards::fall_through;
	}

	[[nodiscard]] friend bool operator==(event_rst lhs, op_afterwards rhs) noexcept{
		return lhs.op() == rhs;
	}

};

}
