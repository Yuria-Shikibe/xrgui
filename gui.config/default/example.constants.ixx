//
// Created by Matrix on 2026/4/13.
//

export module mo_yanxi.gui.examples.default_config.constants;

import std;
import mo_yanxi.gui.fx.config;

namespace mo_yanxi::gui::example{
using pipeline_index = std::uint32_t;



namespace gpip{
export
struct default_draw_constants{
	fx::batch_draw_mode mode;
	float draw_defilade;
};

export
struct mask_apply_draw_constants{
	default_draw_constants draw;
	fx::mask_read_mode mode;
};


namespace idx{
export enum : pipeline_index{
	def,
	cursor_outline,
	coordinate,
	mask_draw,
	mask_apply,
};

}

}

namespace cpip_idx{

export enum : pipeline_index{
	blit,
	blend,
	inverse,
};

}
namespace cpip_bind_idx{

export enum : pipeline_index{
	to_background,
};

}

}