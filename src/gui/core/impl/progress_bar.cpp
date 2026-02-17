module mo_yanxi.gui.elem.progress_bar;

import mo_yanxi.graphic.draw.instruction;

namespace mo_yanxi::gui{
void style::default_progress_drawer::draw_layer_impl(const progress_bar& element, math::frect region, float opacityScl,
	fx::layer_param layer_param) const{
	if(layer_param == 0){
		using namespace graphic;

		auto prog = element.progress.get_current();

		element.get_scene().renderer().push(draw::instruction::rect_aabb{
			.v00 = region.vert_00(),
			.v11 = region.vert_00() + region.extent().mul_x(prog),
			.vert_color = {
				element.draw_config.color.src,element.draw_config.color[prog],
				element.draw_config.color.src,element.draw_config.color[prog]
			}
		});
	}
}
}
