module;

#include <mo_yanxi/enum_operator_gen.hpp>


export module mo_yanxi.gui.flags;

import std;

namespace mo_yanxi::gui{
export enum struct propagate_mask : std::uint8_t{
	none = 0,
	local = 1u << 0,
	super = 1u << 1,
	child = 1u << 2,

	/**
	 * @brief force propagate to UPPER
	 */
	force_upper = 1u << 3,

	all = local | super | child,
	all_force = all | force_upper,

	lower = local | child,
	upper = local | super,
};

BITMASK_OPS(export, propagate_mask)
BITMASK_OPS_ADDITIONAL(export, propagate_mask)


export constexpr bool check_propagate_satisfy(const propagate_mask var, propagate_mask expected) noexcept{
	return (var & expected) == expected;
}

export struct layout_state{
	/**
	 * @brief Describes the accept direction in layout context
	 * e.g. An element in @link BedFace @endlink only accept layout notification from parent
	 */
	propagate_mask context_accept_mask{propagate_mask::all};

	/**
	 * @brief Describes the accept direction an element inherently owns
	 * e.g. An element of @link ScrollPanel @endlink deny children layout notify
	 */
	propagate_mask inherent_accept_mask{propagate_mask::all};

	propagate_mask inherent_broadcast_mask{propagate_mask::all};

	bool intercept_lower_to_isolated{false};

private:
	bool children_changed{};
	bool parent_changed{};
	bool local_changed{};

public:
	constexpr void ignore_children() noexcept{
		inherent_accept_mask -= propagate_mask::child;
	}

	constexpr void ignore_parent() noexcept{
		inherent_accept_mask -= propagate_mask::super;
	}

	[[nodiscard]] constexpr bool check_children_changed() noexcept{
		return std::exchange(children_changed, false);
	}

	[[nodiscard]] constexpr bool check_changed() noexcept{
		return std::exchange(local_changed, false);
	}

	[[nodiscard]] constexpr bool check_parent_changed() noexcept{
		return std::exchange(parent_changed, false);
	}

	[[nodiscard]] constexpr bool check_any_changed() noexcept{
		bool a = check_changed();
		bool b = check_children_changed();
		bool c = check_parent_changed();
		return a || b || c;
	}

	[[nodiscard]] constexpr bool is_parent_changed() const noexcept{
		return parent_changed;
	}

	[[nodiscard]] constexpr bool is_changed() const noexcept{
		return local_changed;
	}

	[[nodiscard]] constexpr bool is_children_changed() const noexcept{
		return children_changed;
	}

	[[nodiscard]] constexpr bool any_lower_changed() const noexcept{
		return local_changed || children_changed;
	}

	[[nodiscard]] constexpr bool is_acceptable(propagate_mask mask) const noexcept{
		return check_propagate_satisfy(context_accept_mask, mask) && check_propagate_satisfy(inherent_accept_mask, mask);
	}

	[[nodiscard]] constexpr bool is_broadcastable(propagate_mask mask) const noexcept{
		return check_propagate_satisfy(inherent_broadcast_mask, mask);
	}

	constexpr void notify_self_changed() noexcept{
		if(is_acceptable(propagate_mask::local)){
			local_changed = true;
		}
	}

	constexpr bool notify_children_changed(const bool force = false) noexcept{
		if(force || is_acceptable(propagate_mask::child)){
			children_changed = true;
			return true;
		}
		return false;
	}

	constexpr bool notify_parent_changed() noexcept{
		if(is_acceptable(propagate_mask::super)){
			parent_changed = true;
			return false;
		}
		return false;
	}

	constexpr void clear() noexcept{
		local_changed = children_changed = parent_changed = false;
	}
};

export enum struct interactivity_flag : std::uint8_t{
	disabled,
	children_only,
	enabled,
	intercept,
};
}
