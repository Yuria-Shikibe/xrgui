//
// Created by Matrix on 2026/4/13.
//

export module mo_yanxi.gui.examples.constants;

import std;

namespace mo_yanxi::gui::example{
using pipeline_index = std::uint32_t;
namespace gpip_idx{

export enum : pipeline_index{
	def,
	cursor_outline,
	coordinate,
	mask_draw,
	mask_apply,
};

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