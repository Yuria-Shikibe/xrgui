module mo_yanxi.gui.elem.check_box;

import mo_yanxi.gui.assets.manager;

void mo_yanxi::gui::check_box::set_default_style(){
	auto region = assets::builtin::get_page()[assets::builtin::shape_id::check];
	icons[0] = {};
	icons[1].components.color = {graphic::colors::white};
	icons[1].components.enabled = true;
	icons[1].components.mode = fx::batch_draw_mode::msdf;
	icons[1].image_region = region.value_or({});
}
