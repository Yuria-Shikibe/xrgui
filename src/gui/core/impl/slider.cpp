module mo_yanxi.gui.elem.slider;

import mo_yanxi.graphic.draw.instruction;


namespace mo_yanxi::gui{
	void style::default_slider_drawer::draw_layer_impl(const slider& element, math::frect region, float opacityScl,
		fx::layer_param layer_param) const{

		const auto extent = element.get_bar_handle_extent();
		auto pos1 = element.content_src_pos_abs() + element.bar.get_progress() * element.content_extent().fdim(extent);
		auto pos2 = element.content_src_pos_abs() + element.bar.get_temp_progress() * element.content_extent().fdim(extent);

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

void style::default_slider_drawer::draw(const slider& element, math::frect region, float opacityScl) const{
	const auto extent = element.get_bar_handle_extent();
	auto pos1 = region.src + element.bar.get_progress() * region.extent().fdim(extent);
	auto pos2 = region.src + element.bar.get_temp_progress() * region.extent().fdim(extent);

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


void slider::draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const{
	elem::draw_layer(clipSpace, param);

	if(drawer_){
		drawer_->draw_layer(*this, content_bound_abs(), get_draw_opacity(), param);
	}
}

void style::default_slider_1d_drawer::draw_layer_impl(const slider_1d& element, math::frect region, float opacityScl,
	fx::layer_param layer_param) const{

	const auto extent = element.get_bar_handle_extent();
	math::vec2 extent_fdim = element.content_extent().fdim(extent);

	auto p1_offset = element.is_vertical ? math::vec2{0, element.bar.get_progress() * extent_fdim.y} : math::vec2{element.bar.get_progress() * extent_fdim.x, 0};
	auto p2_offset = element.is_vertical ? math::vec2{0, element.bar.get_temp_progress() * extent_fdim.y} : math::vec2{element.bar.get_temp_progress() * extent_fdim.x, 0};

	auto pos1 = element.content_src_pos_abs() + p1_offset;
	auto pos2 = element.content_src_pos_abs() + p2_offset;

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
	math::vec2 extent_fdim = region.extent().fdim(extent);

	auto p1_offset = element.is_vertical ? math::vec2{0, element.bar.get_progress() * extent_fdim.y} : math::vec2{element.bar.get_progress() * extent_fdim.x, 0};
	auto p2_offset = element.is_vertical ? math::vec2{0, element.bar.get_temp_progress() * extent_fdim.y} : math::vec2{element.bar.get_temp_progress() * extent_fdim.x, 0};

	auto pos1 = region.src + p1_offset;
	auto pos2 = region.src + p2_offset;

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

}
