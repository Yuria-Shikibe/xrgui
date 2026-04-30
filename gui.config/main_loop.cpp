module mo_yanxi.gui.examples.main_loop;

import mo_yanxi.gui.examples.constants;
import mo_yanxi.gui.style.tree;
import mo_yanxi.gui.style.interface;
import mo_yanxi.react_flow.flexible_value;
import mo_yanxi.gui.style.palette;
import mo_yanxi.gui.assets;
import mo_yanxi.gui.assets.manager;

import mo_yanxi.gui.elem.sequence;
import mo_yanxi.gui.style.tree.draw;

namespace mo_yanxi::gui::style{

template <typename T>
struct paletted_value{
	T val;
	react_flow::flexible_value_holder<palette> pal;

	auto* operator->(this auto& self) noexcept{
		return &self.val;
	}

	auto operator*(this auto& self) noexcept{
		return self.val;
	}
};

}

namespace mo_yanxi::gui::example{



auto make_style(){
	auto& p = gui::assets::builtin::get_page();
	using sid = assets::builtin::shape_id;
	using namespace style;

	auto ret = tree_tuple_fork{
		layer_router{
			style_config{0b01},
			tree_router_dynamic{
				[](const typed_draw_param<elem>& p){
					return true;
				},
				tree_direct{
					tree_fork{
						std::array{
							tree_leaf{
								primitives::draw_nine_patch{
										{
											.patch = {assets::builtin::default_round_square_base},
										   .pal = 	{pal::dark.copy().mul_alpha(.3f)}
										}
								},
							},
							tree_leaf{
								primitives::draw_nine_patch{
										{
											.patch = assets::builtin::default_round_square_boarder_thin,
										   .pal = {make_theme_palette(graphic::colors::AQUA_SKY)}
										}
								},
							}
						}
					}
				}
			}
		},
	};
	return ret;
}
}

