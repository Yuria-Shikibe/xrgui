module mo_yanxi.gui.elem.slider;

import mo_yanxi.graphic.draw.instruction;

namespace mo_yanxi::gui {

void style::default_slider1d_drawer::draw_layer_impl(const slider1d& element, math::frect region, float opacityScl, fx::layer_param layer_param) const {
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

void style::default_slider2d_drawer::draw_layer_impl(const slider2d& element, math::frect region, float opacityScl, fx::layer_param layer_param) const {
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

}
