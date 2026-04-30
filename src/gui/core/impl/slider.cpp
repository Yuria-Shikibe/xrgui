module mo_yanxi.gui.elem.slider;

import mo_yanxi.graphic.draw.instruction;

namespace mo_yanxi::gui {

void style::draw_slider1d_default::operator()(const typed_draw_param<slider1d>& p) const {
    if(!p->layer_param.is_top()) return;

    const auto& element = p.subject();
    auto region = p->draw_bound;
    float opacityScl = p->opacity_scl;
    const auto extent = element.get_bar_handle_extent(region.extent());

    math::vec2 prog_vec = element.is_vertical() ? math::vec2{0.0f, element.get_progress()} : math::vec2{element.get_progress(), 0.0f};
    math::vec2 temp_vec = element.is_vertical() ? math::vec2{0.0f, element.get_temp_progress()} : math::vec2{element.get_temp_progress(), 0.0f};

    auto pos1 = element.content_src_pos_abs() + prog_vec * region.extent().fdim(extent);
    auto pos2 = element.content_src_pos_abs() + temp_vec * region.extent().fdim(extent);

    auto& renderer = element.get_scene().renderer();
    using namespace graphic;
    using namespace graphic::draw::instruction;
    renderer.push(rect_aabb{
        .v00 = pos1,
        .v11 = pos1 + extent,
		.vert_color = {colors::white.copy().mul_a(opacityScl)}
    });
    renderer.push(rect_aabb{
        .v00 = pos2,
        .v11 = pos2 + extent,
		.vert_color = {colors::white.copy().mul_a(.5f * opacityScl)}
    });
}

void style::draw_slider2d_default::operator()(const typed_draw_param<slider2d>& p) const {
    if(!p->layer_param.is_top()) return;

    const auto& element = p.subject();
    auto region = p->draw_bound;
    float opacityScl = p->opacity_scl;
    const auto extent = element.get_bar_handle_extent(region.extent());

    auto pos1 = element.content_src_pos_abs() + element.get_progress() * region.extent().fdim(extent);
    auto pos2 = element.content_src_pos_abs() + element.get_temp_progress() * region.extent().fdim(extent);

    auto& renderer = element.get_scene().renderer();
    using namespace graphic;
    using namespace graphic::draw::instruction;
    renderer.push(rect_aabb{
        .v00 = pos1,
        .v11 = pos1 + extent,
		.vert_color = {colors::white.copy().mul_a(opacityScl)}
    });
    renderer.push(rect_aabb{
        .v00 = pos2,
        .v11 = pos2 + extent,
		.vert_color = {colors::white.copy().mul_a(.5f * opacityScl)}
    });
}

style::target_known_node_ptr<slider1d> slider1d::init_content_style_(){
	return post_sync_assign(*this, &slider1d::content_style_, [](const slider1d& s){
		return s.get_style_tree_manager().get_default<slider1d>();
	});
}

style::target_known_node_ptr<slider2d> slider2d::init_content_style_(){
	return post_sync_assign(*this, &slider2d::content_style_, [](const slider2d& s){
		return s.get_style_tree_manager().get_default<slider2d>();
	});
}

}
