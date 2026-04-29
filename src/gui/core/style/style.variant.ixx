export module mo_yanxi.gui.style.variant;

import std;

namespace mo_yanxi::gui::style {

export enum class family_variant : std::size_t {
	general,
	general_static,
	solid,
	base_only,
	edge_only,

	accent,
	accepted,
	warning,
	invalid,
};

export enum class direction_variant : std::size_t {
	left, right, top, bottom,
};

}
