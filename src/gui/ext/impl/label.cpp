module mo_yanxi.gui.elem.label;

import mo_yanxi.gui.renderer.frontend;
import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.graphic.draw.instruction.general;
import mo_yanxi.math.matrix3;
import mo_yanxi.math.vector2;
import mo_yanxi.math;

namespace mo_yanxi::gui{

void label_text_prov::on_update(react_flow::data_carrier<std::string>& data) {
	label->set_text(data.get());
}

void direct_label_text_prov::on_update(react_flow::data_carrier<typesetting::tokenized_text>& data){
	terminal<typesetting::tokenized_text>::on_update(data);
	label->set_tokenized_text(data.get());
}

void direct_label::draw_text() const {
	if (!render_cache_.has_drawable_text()) return;

	math::vec2 raw_ext = glyph_layout_.extent;
	math::vec2 abs_scale = {std::abs(transform_config_.scale.x), std::abs(transform_config_.scale.y)};


	math::vec2 scaled_ext = raw_ext * abs_scale;
	math::vec2 trans_ext = scaled_ext;


	if (transform_config_.rotation == text_rotation::deg_90 || transform_config_.rotation == text_rotation::deg_270) {
		trans_ext = {scaled_ext.y, scaled_ext.x};
	}

	math::vec2 reg_ext = get_glyph_draw_extent();
	math::vec2 src_local = get_glyph_src_local();
	math::vec2 src_abs = src_local + content_src_pos_abs();

	math::mat3 mat_abs = math::mat3_idt;



	mat_abs.translate(-raw_ext * 0.5f);


	if (transform_config_.scale.x != 1.f || transform_config_.scale.y != 1.f) {
		mat_abs.scale(transform_config_.scale.x, transform_config_.scale.y);
	}


	switch (transform_config_.rotation) {
	case text_rotation::deg_90:  mat_abs.rotate_90(); break;
	case text_rotation::deg_180: mat_abs.rotate_180(); break;
	case text_rotation::deg_270: mat_abs.rotate_270(); break;
	case text_rotation::deg_0:   break;
	}



	if (fit_type_ != label_fit_type::fix && trans_ext.x > 0.f && trans_ext.y > 0.f) {
		mat_abs.scale(reg_ext / trans_ext);
	}


	mat_abs.translate(src_abs + reg_ext * 0.5f);

	math::mat3 mat_local = mat_abs;
	mat_local.c3.x += (src_local.x - src_abs.x);
	mat_local.c3.y += (src_local.y - src_abs.y);

	state_guard guard{renderer(), fx::batch_draw_mode::msdf};
	transform_guard _t{renderer(), mat_abs};
	render_cache_.push_to_renderer(renderer());
}

}
