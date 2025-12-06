module;

#include <magic_enum/magic_enum.hpp>

module mo_yanxi.gui.assets;

import mo_yanxi.gui.assets.manager;

//TODO import image page from other place...
import mo_yanxi.graphic.image_atlas;


auto& load(mo_yanxi::graphic::image_page& page, std::string&& name, mo_yanxi::graphic::sdf_load&& task){
	return page.register_named_region(std::move(name), std::move(task), true).region;
}

namespace mo_yanxi::gui::assets{
void load_default_assets(graphic::image_atlas& image_atlas, const std::filesystem::path& svg_root){
	auto& atlas = image_atlas;
	auto& page = atlas.create_image_page("ui");

	auto& line = load(page, "line", {
			graphic::msdf::msdf_generator{graphic::msdf::create_capsule(32, 16), 16, 8},
			math::usize2{80u, 64u}, 2
		});

	auto line_region = gui::assets::row_patch{line, line.get_region(), 32, 32, 8};

	using namespace gui::assets::builtin;
	get_page().insert(white, {});
	get_page().insert(row_patch_area, line);


	for (const auto& [val, name] : magic_enum::enum_entries<icon>()) {
		auto path = svg_root / name;
		path.replace_extension(".svg");
		if(!std::filesystem::exists(path)){
			std::println(std::cerr, "Icon Path Not Found: {}", std::filesystem::absolute(path).string());
			get_page().insert(val, {});
		} else{
			auto& region = load(page, std::string{name}, {
					graphic::msdf::msdf_generator{path.string()},
					std::nullopt, 2
				});
			get_page().insert(val, region);
		}

	}
}
}