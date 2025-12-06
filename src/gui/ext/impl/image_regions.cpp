module mo_yanxi.gui.assets.image_regions;

import mo_yanxi.gui.assets.manager;

namespace mo_yanxi::gui::assets::builtin{
row_patch get_separator_row_patch(){
	if(auto v = get_page().find(id_enum::row_patch_area)){
		return row_patch{*v, (*v)->uv.get_region(), 32, 32, 4};
	}
	return {{}, {4, 1}, 1, 1};
}

}
