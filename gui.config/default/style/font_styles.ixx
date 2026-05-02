//
// Created by Matrix on 2026/5/1.
//

export module mo_yanxi.gui.examples.default_config.font_styles;

import std;
import mo_yanxi.font;
import mo_yanxi.font.manager;
import mo_yanxi.font.plat;
import mo_yanxi.graphic.image_atlas;

namespace mo_yanxi::gui::example{
export
void init_font_manager(font::font_manager& font_manager, graphic::image_atlas& image_atlas){
	font_manager.set_page(image_atlas.create_image_page("font"));

	{
		auto sys_font_path = font::get_system_fonts();
		auto consolas_family = font::find_family_of(sys_font_path, "Consolas");
		auto segoe_symbol = sys_font_path.find("Segoe UI Symbol");

		const std::filesystem::path font_path = std::filesystem::current_path().append("assets/font").make_preferred();
		auto& SourceHanSansCN_regular = font_manager.register_meta("srchs", font_path / "SourceHanSansCN-Regular.otf");
		const font::font_face_meta* segoe{};
		if(segoe_symbol != sys_font_path.end()){
			segoe = &font_manager.register_meta("segui", segoe_symbol->second);
		}


		std::vector<const font::font_face_meta*> code_faces_{};
		for(const auto& [name, path] : consolas_family){
			auto& meta = font_manager.register_meta(name, path);
			code_faces_.push_back(&meta);
		}

		font_manager.register_family("mono", code_faces_, {&SourceHanSansCN_regular, segoe});

		auto& default_family = font_manager.register_family("def", {&SourceHanSansCN_regular, segoe});

		font_manager.set_default_family(&default_family);

		font::default_font_manager = &font_manager;
	}
}
}
