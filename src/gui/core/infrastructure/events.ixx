//

//

export module mo_yanxi.gui.infrastructure:events;

import mo_yanxi.math.vector2;
import mo_yanxi.input_handle;
import mo_yanxi.input_handle.input_event_queue;
import std;

import :elem_ptr;

namespace mo_yanxi::gui::events{

export
using key_set = input_handle::key_set;

export
/**
 * @brief Current propagation phase for a routed GUI event.
 */
enum struct event_phase : std::uint8_t{
	/** Event is traveling from the root toward the target. */
	preview,
	/** Event is being delivered to the target element. */
	target,
	/** Event is traveling from the target back toward the root. */
	bubble,
	/** Target-only default action phase, skipped when default is prevented. */
	default_action,
};

export
/**
 * @brief Semantic category of a GUI event.
 */
enum struct gui_event_type : std::uint8_t{
	pointer_move, pointer_press, pointer_release, pointer_drag, pointer_click,

	wheel,

	key_down, key_up, key_repeat,

	text_input, ime_composition,

	focus_lost,
};

export
enum struct dispatch_result{
	handled,
	unhandled,
};

export
/**
 * @brief Mutable dispatch state shared by every phase of one routed event.
 */
struct event_control{
private:
	/** Element that last marked the event as handled. */
	elem* handled_by_{};
	/** Whether remaining preview/target/bubble propagation should stop. */
	bool propagation_stopped_{};
	/** Whether the target default action phase should be skipped. */
	bool default_prevented_{};

public:
	/**
	 * @brief Mark the event as handled by the given element.
	 */
	void mark_handled(elem& handler) noexcept{
		handled_by_ = std::addressof(handler);
	}

	/**
	 * @brief Stop delivery to later elements in the current route.
	 */
	void stop_propagation() noexcept{
		propagation_stopped_ = true;
	}

	/**
	 * @brief Mark as handled and stop further propagation.
	 */
	void consume(elem& handler) noexcept{
		mark_handled(handler);
		stop_propagation();
	}

	/**
	 * @brief Skip the target default action phase.
	 */
	void prevent_default() noexcept{
		default_prevented_ = true;
	}

	[[nodiscard]] bool handled() const noexcept{
		return handled_by_ != nullptr;
	}

	[[nodiscard]] elem* handled_by() const noexcept{
		return handled_by_;
	}

	[[nodiscard]] bool propagation_stopped() const noexcept{
		return propagation_stopped_;
	}

	[[nodiscard]] bool default_prevented() const noexcept{
		return default_prevented_;
	}

	[[nodiscard]] dispatch_result result() const noexcept{
		return handled() ? dispatch_result::handled : dispatch_result::unhandled;
	}

	[[nodiscard]] explicit operator bool() const noexcept{
		return handled();
	}
};

export
/**
 * @brief Immutable routing values for one delivery of an event.
 */
struct event_route{
	/** Event category being delivered. */
	gui_event_type type{};
	/** Current propagation phase. */
	event_phase phase{};
	/** Target element selected for this routed event. */
	elem* target{};
	/** Element currently receiving this delivery. */
	elem* current{};
	/** Root-to-target route, including both root and target. */
	std::span<elem* const> path{};
};

export
//TODO encapsulation seems bad with too may redundant functions
/**
 * @brief Per-delivery event context: immutable route plus mutable dispatch control.
 */
struct event_context{
private:
	/** Route metadata for this delivery. */
	event_route route_;
	/** Shared mutable control state for the whole dispatch. */
	event_control* control_;

public:
	[[nodiscard]] event_context(
		const event_route& route_value,
		event_control& control_value) noexcept
		: route_(route_value),
		  control_(std::addressof(control_value)){
	}

	[[nodiscard]] gui_event_type type() const noexcept{
		return route_.type;
	}

	[[nodiscard]] event_phase phase() const noexcept{
		return route_.phase;
	}

	[[nodiscard]] elem* target() const noexcept{
		return route_.target;
	}

	[[nodiscard]] elem* current() const noexcept{
		return route_.current;
	}

	[[nodiscard]] std::span<elem* const> path() const noexcept{
		return route_.path;
	}

	/**
	 * @brief Route descendants after the current element and before or including the target.
	 */
	[[nodiscard]] std::span<elem* const> descendants_to_target() const noexcept;

	/**
	 * @brief True for phases intended for normal element interaction logic.
	 */
	[[nodiscard]] bool is_target_or_bubble_phase() const noexcept{
		return phase() == event_phase::target || phase() == event_phase::bubble;
	}

	void mark_handled(elem& handler) noexcept{
		control_->mark_handled(handler);
	}

	void stop_propagation() noexcept{
		control_->stop_propagation();
	}

	void consume(elem& handler) noexcept{
		control_->consume(handler);
	}

	void prevent_default() noexcept{
		control_->prevent_default();
	}

	[[nodiscard]] elem* handled_by() const noexcept{
		return control_->handled_by();
	}

	[[nodiscard]] bool handled() const noexcept{
		return control_->handled();
	}
};

export
/**
 * @brief Pointer button press/release/click event value.
 */
struct pointer_button_event{
	/** Pointer position in scene coordinates. */
	math::vec2 scene_pos{};
	/** Pointer position in the current receiver's local coordinates. */
	math::vec2 local_pos{};
	/** Button, action, and modifier state for this pointer input. */
	key_set key{};

	/**
	 * @brief Pointer position relative to an element's content origin.
	 */
	[[nodiscard]] math::vec2 get_content_pos(const event_context& ctx, const elem& elem) const noexcept;

	/**
	 * @brief Whether the pointer is inside an element's local extent plus margin.
	 */
	[[nodiscard]] bool within_elem(const event_context& ctx, const elem& elem, float margin = 0) const noexcept;
};

export
/**
 * @brief Pointer drag event value.
 */
struct pointer_drag_event{
	/** Drag start position in scene coordinates. */
	math::vec2 scene_src{};
	/** Current drag position in scene coordinates. */
	math::vec2 scene_dst{};
	/** Drag start position in the current receiver's local coordinates. */
	math::vec2 local_src{};
	/** Current drag position in the current receiver's local coordinates. */
	math::vec2 local_dst{};
	/** Button and modifier state associated with the drag. */
	key_set key{};

	/**
	 * @brief Drag delta in scene coordinates.
	 */
	[[nodiscard]] math::vec2 scene_delta() const noexcept{
		return scene_dst - scene_src;
	}

	/**
	 * @brief Drag delta in the current receiver's local coordinates.
	 */
	[[nodiscard]] math::vec2 local_delta() const noexcept{
		return local_dst - local_src;
	}
};

export
/**
 * @brief Pointer movement event value.
 */
struct pointer_move_event{
	/** Previous pointer position in scene coordinates. */
	math::vec2 scene_src{};
	/** Current pointer position in scene coordinates. */
	math::vec2 scene_dst{};
	/** Previous pointer position in the current receiver's local coordinates. */
	math::vec2 local_src{};
	/** Current pointer position in the current receiver's local coordinates. */
	math::vec2 local_dst{};

	/**
	 * @brief Movement delta in scene coordinates.
	 */
	[[nodiscard]] math::vec2 scene_delta() const noexcept{
		return scene_dst - scene_src;
	}

	/**
	 * @brief Movement delta in the current receiver's local coordinates.
	 */
	[[nodiscard]] math::vec2 local_delta() const noexcept{
		return local_dst - local_src;
	}
};

export
/**
 * @brief Wheel or touchpad scroll event value.
 */
struct wheel_event{
	/** Pointer position at the time of scrolling in scene coordinates. */
	math::vec2 scene_pos{};
	/** Pointer position at the time of scrolling in the current receiver's local coordinates. */
	math::vec2 local_pos{};
	/** Scroll delta supplied by the input backend. */
	math::vec2 delta{};
	/** Active modifier mode during the scroll. */
	input_handle::mode mode{input_handle::mode::none};
};

export
/**
 * @brief Keyboard key event value.
 */
struct key_event{
	/** Key code, action, and modifier state. */
	key_set key{};
};

export
/**
 * @brief Text input event value.
 */
struct text_event{
	/** Unicode scalar value produced by text input. */
	char32_t value{};
};

export
/**
 * @brief IME composition event value.
 */
struct ime_event{
	/** Composition data owned by the input dispatch caller for the duration of handling. */
	const input_handle::ime_composition_event* composition{};
};

}
