module mo_yanxi.gui.elem.slider_1d;

import mo_yanxi.graphic.draw.instruction;

namespace mo_yanxi::gui{

	void style::default_slider_1d_drawer::draw_layer_impl(const slider_1d& element, math::frect region, float opacityScl,
		fx::layer_param layer_param) const{

		const auto extent = element.get_bar_handle_extent();
        math::vec2 prog1 = element.is_vertical ? math::vec2{0.0f, element.get_progress()} : math::vec2{element.get_progress(), 0.0f};
        math::vec2 prog2 = element.is_vertical ? math::vec2{0.0f, element.get_temp_progress()} : math::vec2{element.get_temp_progress(), 0.0f};

		auto pos1 = element.content_src_pos_abs() + prog1 * element.content_extent().fdim(extent);
		auto pos2 = element.content_src_pos_abs() + prog2 * element.content_extent().fdim(extent);

		auto& renderer = element.get_scene().renderer();
		using namespace graphic;
		using namespace graphic::draw::instruction;
		renderer.push(rect_aabb{
			.v00 = pos1,
			.v11 = pos1 + extent,
			.vert_color = {colors::white}
		});
		renderer.push(rect_aabb{
			.v00 = pos2,
			.v11 = pos2 + extent,
			.vert_color = {colors::white.copy().mul_a(.5f)}
		});
	}

void style::default_slider_1d_drawer::draw(const slider_1d& element, math::frect region, float opacityScl) const{
	const auto extent = element.get_bar_handle_extent();
    math::vec2 prog1 = element.is_vertical ? math::vec2{0.0f, element.get_progress()} : math::vec2{element.get_progress(), 0.0f};
    math::vec2 prog2 = element.is_vertical ? math::vec2{0.0f, element.get_temp_progress()} : math::vec2{element.get_temp_progress(), 0.0f};

	auto pos1 = region.src + prog1 * region.extent().fdim(extent);
	auto pos2 = region.src + prog2 * region.extent().fdim(extent);

	auto& renderer = element.get_scene().renderer();
	using namespace graphic;
	using namespace graphic::draw::instruction;
	renderer.push(rect_aabb{
		.v00 = pos1,
		.v11 = pos1 + extent,
		.vert_color = {colors::white.copy().mul_a(.5f * opacityScl)}
	});
	renderer.push(rect_aabb{
		.v00 = pos2,
		.v11 = pos2 + extent,
		.vert_color = {colors::white.copy().mul_a(opacityScl)}
	});
}


void slider_1d::draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const{
	elem::draw_layer(clipSpace, param);

	if(drawer_){
		drawer_->draw_layer(*this, content_bound_abs(), get_draw_opacity(), param);
	}
}

}
