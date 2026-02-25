module;

#include <magic_enum/magic_enum.hpp>

module mo_yanxi.gui.assets;

import mo_yanxi.gui.assets.manager;

//TODO import image page from other place...
import mo_yanxi.graphic.image_atlas;
import mo_yanxi.gui.image_regions;
import align;


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

	auto line_region = gui::row_patch{line, line.get_region(), 32, 32, 8};

	using namespace gui::assets::builtin;
	get_page().insert(white, {});
	get_page().insert(row_separator, line);


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

void generate_default_shapes(graphic::image_atlas& image_atlas){
	auto& atlas = image_atlas;
	auto& page = atlas.create_image_page("ui");

	constexpr static math::usize2 extent{96, 96};

	auto& line = load(page, "line", {
			graphic::msdf::msdf_generator{graphic::msdf::create_capsule(32, 16), 16, 8},
			math::usize2{80u, 64u}, 2
		});


	auto& boarder = load(page,
			"edge",
			graphic::sdf_load{
				graphic::msdf::msdf_generator{graphic::msdf::create_boarder(12.f, 3.f), 4.}, extent, 2
			});

	auto& boarder_thin = load(page,
		"edge_thin",
		graphic::sdf_load{
			graphic::msdf::msdf_generator{graphic::msdf::create_boarder(12.f, 2.f), 4.}, extent, 2
		});

	auto& base = load(page,
		"base",
		graphic::sdf_load{
			graphic::msdf::msdf_generator{graphic::msdf::create_solid_boarder(12.f), 4.}, extent, 3
		});

	using namespace builtin;
	auto& builtin_page = get_page();

	builtin_page.insert(white, image_region_borrow{});
	builtin_page.insert(row_separator, line);
	builtin_page.insert(round_square_edge, boarder);
	builtin_page.insert(round_square_edge_thin, boarder_thin);
	builtin_page.insert(round_square_base, base);

	builtin::default_round_square_boarder = {boarder, align::padding2d<std::uint32_t>{}.set(12).expand(graphic::msdf::sdf_image_boarder), graphic::msdf::sdf_image_boarder};
	builtin::default_round_square_boarder_thin = {boarder_thin, align::padding2d<std::uint32_t>{}.set(12).expand(graphic::msdf::sdf_image_boarder), graphic::msdf::sdf_image_boarder};
	builtin::default_round_square_base = {base, align::padding2d<std::uint32_t>{}.set(12).expand(graphic::msdf::sdf_image_boarder), graphic::msdf::sdf_image_boarder};

}
}
