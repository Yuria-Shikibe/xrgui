module mo_yanxi.gui.elem.image_frame;

import mo_yanxi.gui.assets.manager;

mo_yanxi::gui::icon_frame::icon_frame(scene& scene, elem* group, std::size_t iconID, const image_display_style& style) : icon_frame(scene, group, assets::builtin::get_page()[iconID].value_or({}), style){

}
