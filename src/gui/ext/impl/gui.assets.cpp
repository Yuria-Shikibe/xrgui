module;

#include <magic_enum/magic_enum.hpp>

module mo_yanxi.gui.assets;

import mo_yanxi.gui.assets.manager;

//TODO import image page from other place...
import mo_yanxi.graphic.image_atlas;
import mo_yanxi.gui.infrastructure;
import mo_yanxi.gui.elem.scroll_pane;
import mo_yanxi.gui.image_regions;
import align;

constexpr auto check_svg_test = R"(<?xml version='1.0' encoding='utf-8'?>
<svg xmlns="http://www.w3.org/2000/svg" width="48" height="48" viewBox="0 0 48 48" fill="none"><path d="M43 11L16.875 37L5 25.1818" stroke="#333333" stroke-width="3" stroke-linecap="round" stroke-linejoin="round" /></svg>
)";


auto& load(mo_yanxi::graphic::image_page& page, std::string&& name, mo_yanxi::graphic::sdf_load&& task){
	return page.register_named_region(std::move(name), std::move(task), true).region;
}

namespace mo_yanxi::gui::assets{


void generate_default_shapes(graphic::image_atlas& image_atlas){
	auto& atlas = image_atlas;
	auto& page = atlas.create_image_page("ui");

	constexpr static math::usize2 extent{96, 96};

	auto& line = load(page, "line", {
			graphic::msdf::msdf_generator{graphic::msdf::create_capsule(32, 16), 4},
			math::usize2{80u, 64u}, 2
		});

	auto& boarder = load(page,
			"edge",
			graphic::sdf_load{
				graphic::msdf::msdf_generator{graphic::msdf::create_boarder(8.f, 3.f), 4.}, extent, 3
			});

	auto& boarder_thin = load(page,
		"edge_thin",
		graphic::sdf_load{
			graphic::msdf::msdf_generator{graphic::msdf::create_boarder(8.f, 1.f), 4.}, extent, 3
		});

	auto& base = load(page,
		"base",
		graphic::sdf_load{
			graphic::msdf::msdf_generator{graphic::msdf::create_solid_boarder(8.f), 4.}, extent, 3
		});

	using namespace builtin;
	auto& builtin_page = get_page();

	builtin_page.insert(shape_id::white, image_region_borrow{});
	builtin_page.insert(shape_id::row_separator, line);
	builtin_page.insert(shape_id::round_square_edge, boarder);
	builtin_page.insert(shape_id::round_square_edge_thin, boarder_thin);
	builtin_page.insert(shape_id::round_square_base, base);

	builtin::default_round_square_boarder = {boarder, align::padding2d<std::uint32_t>{}.set(12).expand(graphic::msdf::sdf_image_boarder), graphic::msdf::sdf_image_boarder};
	builtin::default_round_square_boarder_thin = {boarder_thin, align::padding2d<std::uint32_t>{}.set(12).expand(graphic::msdf::sdf_image_boarder), graphic::msdf::sdf_image_boarder};
	builtin::default_round_square_base = {base, align::padding2d<std::uint32_t>{}.set(12).expand(graphic::msdf::sdf_image_boarder), graphic::msdf::sdf_image_boarder};

}

void load_default_icons(graphic::image_atlas& image_atlas){
	auto& atlas = image_atlas;
	auto& page = atlas.create_image_page("ui");

	auto& base = load(page,
		"i-check",
		graphic::sdf_load{
			graphic::msdf::msdf_generator{check_svg_test, true}, math::usize2{64u, 64u}, 3
		});

	using namespace builtin;
	auto& builtin_page = get_page();
	builtin_page.insert(shape_id::check, base);
}

void dispose_generated_shapes(){
	style::global_default_style_drawer = {};
	style::global_scroll_pane_bar_drawer = {};

	builtin::default_round_square_boarder = {};
	builtin::default_round_square_boarder_thin = {};
	builtin::default_round_square_base = {};
}
}
