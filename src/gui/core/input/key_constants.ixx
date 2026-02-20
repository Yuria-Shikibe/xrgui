module;

#include <mo_yanxi/enum_operator_gen.hpp>

export module mo_yanxi.input_handle:constants;

import std;

consteval std::size_t genMaskFromBits(std::size_t bits) noexcept{
	std::size_t rst{};

	for(std::size_t i = 0; i < bits; ++i){
		rst |= static_cast<std::size_t>(1) << i;
	}

	return rst;
}

namespace mo_yanxi::input_handle{
export
enum struct act : std::uint8_t{
	release,
	press,
	repeat,
	continuous,
	double_press,

	ignore = std::numeric_limits<std::uint8_t>::max()
};


export
[[nodiscard]] constexpr bool matched(const act action, const act expectedAct) noexcept{
	return action == expectedAct || expectedAct == act::ignore;
}

export
[[nodiscard]] constexpr bool is_continuous(const act action) noexcept{
	return action == act::continuous;
}

export
enum struct mode : std::uint8_t{
	none = 0,
	shift = 1 << 0,
	ctrl = 1 << 1,
	alt = 1 << 2,
	super = 1 << 3,
	cap_lock = 1 << 4,
	num_lock = 1 << 5,

	strict = 1 << 6,

	ctrl_shift = ctrl | shift,

	ignore = std::numeric_limits<std::uint8_t>::max()
};


BITMASK_OPS(export, mode);

export [[nodiscard]] constexpr bool matched(const mode m, const mode expectedMode) noexcept{
	if(expectedMode == mode::ignore)return true;

	if((expectedMode & mode::strict) == mode{}) [[likely]] {
		return (m & expectedMode) == expectedMode;
	}

	return m == expectedMode;
}

// export [[nodiscard]] constexpr bool satisfy(const mode m, const mode expectedMode) noexcept{
// 	return (m & expectedMode) == expectedMode || expectedMode == mode::ignore;
// }

export
enum struct key : std::uint16_t{
	ignore = std::numeric_limits<std::uint16_t>::max(),
	space = 32,
	apostrophe = 39, /* ' */
	comma = 44, /* , */
	minus = 45, /* - */
	period = 46, /* . */
	slash = 47, /* / */
	_0 = 48,
	_1 = 49,
	_2 = 50,
	_3 = 51,
	_4 = 52,
	_5 = 53,
	_6 = 54,
	_7 = 55,
	_8 = 56,
	_9 = 57,
	semicolon = 59, /* ; */
	equal = 61, /* = */
	a = 65,
	b = 66,
	c = 67,
	d = 68,
	e = 69,
	f = 70,
	g = 71,
	h = 72,
	i = 73,
	j = 74,
	k = 75,
	l = 76,
	m = 77,
	n = 78,
	o = 79,
	p = 80,
	q = 81,
	r = 82,
	s = 83,
	t = 84,
	u = 85,
	v = 86,
	w = 87,
	x = 88,
	y = 89,
	z = 90,
	left_bracket = 91, /* [ */
	backslash = 92, /* \ */
	right_bracket = 93, /* ] */
	grave_accent = 96, /* ` */

	world_1 = 161, /* non-US #1 */
	world_2 = 162, /* non-US #2 */

	esc = 256,
	enter = 257,
	tab = 258,
	backspace = 259,
	insert = 260,
	del = 261, // "delete" is a keyword, so changed to "delete_key"
	right = 262,
	left = 263,
	down = 264,
	up = 265,
	page_up = 266,
	page_down = 267,
	home = 268,
	end = 269,
	caps_lock = 280,
	scroll_lock = 281,
	num_lock = 282,
	print_screen = 283,
	pause = 284,
	f1 = 290,
	f2 = 291,
	f3 = 292,
	f4 = 293,
	f5 = 294,
	f6 = 295,
	f7 = 296,
	f8 = 297,
	f9 = 298,
	f10 = 299,
	f11 = 300,
	f12 = 301,
	f13 = 302,
	f14 = 303,
	f15 = 304,
	f16 = 305,
	f17 = 306,
	f18 = 307,
	f19 = 308,
	f20 = 309,
	f21 = 310,
	f22 = 311,
	f23 = 312,
	f24 = 313,
	f25 = 314,

	kp_0 = 320,
	kp_1 = 321,
	kp_2 = 322,
	kp_3 = 323,
	kp_4 = 324,
	kp_5 = 325,
	kp_6 = 326,
	kp_7 = 327,
	kp_8 = 328,
	kp_9 = 329,

	kp_decimal = 330,
	kp_divide = 331,
	kp_multiply = 332,
	kp_subtract = 333,
	kp_add = 334,
	kp_enter = 335,
	kp_equal = 336,

	left_shift = 340,
	left_control = 341,
	left_alt = 342,
	left_super = 343,

	right_shift = 344,
	right_control = 345,
	right_alt = 346,
	right_super = 347,

	menu = 348,

	LAST = menu,
	COUNT = LAST + 1
};


export [[nodiscard]] constexpr bool matched(const key k, const key expectedKey) noexcept{
	return k == expectedKey || expectedKey == key::ignore;
}

export
enum struct mouse : std::uint8_t{
	_1 = 0,
	_2 = 1,
	_3 = 2,
	_4 = 3,
	_5 = 4,
	_6 = 5,
	_7 = 6,
	_8 = 7,

	Count = 8,

	left = _1,
	right = _2,
	middle = _3,

	LMB = _1,
	RMB = _2,
	CMB = _3,
};

export
struct key_set{
	std::uint16_t key_code{std::to_underlying(key::ignore)};
	act action{act::ignore};
	mode mode_bits{mode::ignore};

	[[nodiscard]] key_set() = default;

	[[nodiscard]] constexpr explicit(false) key_set(std::uint16_t k, act action = act::ignore, mode mode = mode::ignore)
		: key_code(k),
		  action(action),
		  mode_bits(mode){
	}

	[[nodiscard]] constexpr explicit(false) key_set(key k, act action = act::ignore, mode mode = mode::ignore)
		: key_code(std::to_underlying(k)),
		  action(action),
		  mode_bits(mode){
	}

	[[nodiscard]] constexpr explicit(false) key_set(mouse k, act action = act::ignore, mode mode = mode::ignore)
		: key_code(std::to_underlying(k)),
		  action(action),
		  mode_bits(mode){
	}

	[[nodiscard]] constexpr key as_key() const noexcept{
		return key{key_code};
	}

	[[nodiscard]] constexpr mouse as_mouse() const noexcept{
		return static_cast<mouse>(key_code);
	}

	[[nodiscard]] constexpr bool on_release() const noexcept{
		return action == act::release;
	}

	[[nodiscard]] constexpr bool match(key_set expected) const noexcept{
		return matched(key{key_code}, key{expected.key_code}) && matched(action, expected.action) && matched(mode_bits, expected.mode_bits);
	}

	[[nodiscard]] constexpr bool match(act expected_action, mode expected_mode) const noexcept{
		return matched(action, expected_action) && matched(mode_bits, expected_mode);
	}

	constexpr bool operator==(const key_set&) const noexcept = default;
};
}
