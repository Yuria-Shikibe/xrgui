module;

#include <vulkan/vulkan.h>

export module mo_yanxi.gui.style.round_styles;

import std;

import mo_yanxi.react_flow.flexible_value;

export import mo_yanxi.gui.style.palette;

export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.gui.elem.scroll_pane;
export import mo_yanxi.gui.elem.slider;

export import mo_yanxi.gui.image_regions;
export import mo_yanxi.graphic.draw.instruction;
export import mo_yanxi.gui.fx.instruction_extension;
export import mo_yanxi.gui.fx.fringe;

export import mo_yanxi.gui.style.tree;
export import mo_yanxi.gui.style.tree.draw;

namespace mo_yanxi::gui::style::spec{
export
struct create_entry{
	primitives::nine_patch_draw_entry edge;
	primitives::nine_patch_draw_entry base;
	primitives::nine_patch_draw_entry back;

	[[nodiscard]] auto make_edge_only() const {
		return tree_tuple_fork{
				tree_direct{
					layer_router{
						style_config{0b01},
						tree_leaf{primitives::draw_nine_patch_hollow{edge}}
					}
				}
			};
	}

	[[nodiscard]] auto make_back_only() const {
		return tree_tuple_fork{
				tree_direct{
					layer_router{
						style_config{0b10},
						tree_leaf{primitives::draw_nine_patch{back}}
					}
				}
			};
	}

	[[nodiscard]] auto make_general() const {
		return tree_direct{
			tree_tuple_fork{
				layer_router{
					style_config{0b01},
					tree_leaf{primitives::draw_nine_patch_hollow{edge}},
				},
				layer_router{
					style_config{0b10},
					tree_leaf{primitives::draw_nine_patch{back}}
				},
			}
		};
	}

	[[nodiscard]] auto make_general_with_base() const {
		return tree_direct{
			tree_tuple_fork{
				layer_router{
					style_config{0b01},
					tree_tuple_fork{
						tree_leaf{primitives::draw_nine_patch{base}},
						tree_leaf{primitives::draw_nine_patch_hollow{edge}}
					}
				},
				layer_router{
					style_config{0b10},
					tree_leaf{primitives::draw_nine_patch{back}}
				},
			}
		};
	}
};



}
