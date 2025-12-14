//
// Created by Matrix on 2025/12/14.
//

export module mo_yanxi.gui.draw_config;

import std;

namespace mo_yanxi::gui::draw_config{
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