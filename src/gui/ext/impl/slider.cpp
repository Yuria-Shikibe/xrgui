module mo_yanxi.gui.elem.slider;

import mo_yanxi.graphic.draw.instruction;


namespace mo_yanxi::gui{
void style::default_slider_drawer::draw(const slider& element, math::frect region, float opacityScl) const{
	const auto extent = element.get_bar_handle_extent();
	auto pos1 = element.content_src_pos_abs() + element.bar.get_progress() * element.content_extent().fdim(extent);
	auto pos2 = element.content_src_pos_abs() + element.bar.get_temp_progress() * element.content_extent().fdim(extent);

	auto& renderer = element.get_scene().renderer();
	using namespace graphic;
	using namespace graphic::draw::instruction;
	renderer.push(rectangle_ortho{
		.v00 = pos1,
		.v11 = pos1 + extent,
		.vert_color = {colors::white}
	});
	renderer.push(rectangle_ortho{
		.v00 = pos2,
		.v11 = pos2 + extent,
		.vert_color = {colors::white.copy().mul_a(.5f)}
	});
}

void slider::draw_content(rect clipSpace) const{
	draw_background();
	if(drawer_){
		drawer_->draw(*this, clipSpace, 1);
	}
}
}