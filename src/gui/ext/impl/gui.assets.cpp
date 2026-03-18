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

namespace svgs{

constexpr std::string_view check = R"(
<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<svg
   width="48"
   height="48"
   viewBox="0 0 48 48"
   fill="none"
   version="1.1"
   id="svg1"
   xmlns="http://www.w3.org/2000/svg"
   xmlns:svg="http://www.w3.org/2000/svg">
  <defs
     id="defs1" />
  <path
     style="baseline-shift:baseline;display:inline;overflow:visible;vector-effect:none;fill:#333333;stroke-linecap:round;stroke-linejoin:round;enable-background:accumulate;stop-color:#000000;stop-opacity:1;opacity:1"
     d="M 41.941406,9.9375 16.875,34.882812 6.0585937,24.119141 a 1.5,1.5 0 0 0 -2.1210937,0.0039 1.5,1.5 0 0 0 0.00391,2.121094 L 15.816406,38.0625 a 1.50015,1.50015 0 0 0 2.117188,0 l 26.125,-26 A 1.5,1.5 0 0 0 44.0625,9.9414063 1.5,1.5 0 0 0 41.941406,9.9375 Z"
     id="path1" />
</svg>
)";

constexpr std::string_view textarea = R"(
<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<svg
   width="48"
   height="48"
   viewBox="0 0 48 48"
   fill="none"
   version="1.1"
   id="svg2"
   xmlns="http://www.w3.org/2000/svg"
   xmlns:svg="http://www.w3.org/2000/svg">
  <defs
     id="defs2" />
  <path
     style="baseline-shift:baseline;display:inline;overflow:visible;vector-effect:none;fill:#333333;stroke-linecap:round;stroke-linejoin:round;enable-background:accumulate;stop-color:#000000;stop-opacity:1;opacity:1"
     d="m 16,2 a 2,2 0 0 0 -2,2 2,2 0 0 0 2,2 c 2.928571,0 4.120299,1.0517145 4.943359,2.3320313 C 21.76642,9.612348 22,11.333333 22,12 v 24 c 0,0.666667 -0.23358,2.387652 -1.056641,3.667969 C 20.120299,40.948285 18.928571,42 16,42 a 2,2 0 0 0 -2,2 2,2 0 0 0 2,2 c 4.071429,0 6.879701,-1.948285 8.306641,-4.167969 C 25.73358,39.612348 26,37.333333 26,36 V 12 C 26,10.666667 25.73358,8.387652 24.306641,6.1679687 22.879701,3.9482855 20.071429,2 16,2 Z"
     id="path1" />
  <path
     style="baseline-shift:baseline;display:inline;overflow:visible;vector-effect:none;fill:#333333;stroke-linecap:round;stroke-linejoin:round;enable-background:accumulate;stop-color:#000000;stop-opacity:1;opacity:1"
     d="M 32,2 C 28.333333,2 25.645339,3.9566822 24.123047,6.0878906 22.600755,8.2190991 22,10.333333 22,12 v 24 c 0,1.333333 0.26642,3.612348 1.693359,5.832031 C 25.120299,44.051715 27.928571,46 32,46 a 2,2 0 0 0 2,-2 2,2 0 0 0 -2,-2 C 29.071429,42 27.879701,40.948285 27.056641,39.667969 26.23358,38.387652 26,36.666667 26,36 V 12 C 26,11.666667 26.399245,9.7809009 27.376953,8.4121094 28.354661,7.0433178 29.666667,6 32,6 A 2,2 0 0 0 34,4 2,2 0 0 0 32,2 Z"
     id="path2" />
</svg>
)";
}


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

	auto& i_check = load(page,
		"i-check",
		graphic::sdf_load{
			graphic::msdf::msdf_generator{std::string(svgs::check), true}, math::usize2{64u, 64u}, 3
		});
	auto& i_textarea = load(page,
		"i-textarea",
		graphic::sdf_load{
			graphic::msdf::msdf_generator{std::string(svgs::textarea), true}, math::usize2{64u, 64u}, 3
		});

	using namespace builtin;
	auto& builtin_page = get_page();
	builtin_page.insert(shape_id::check, i_check);
	builtin_page.insert(shape_id::textarea, i_textarea);
}

void dispose_generated_shapes(){
	style::global_default_style_drawer = {};
	style::global_scroll_pane_bar_drawer = {};

	builtin::default_round_square_boarder = {};
	builtin::default_round_square_boarder_thin = {};
	builtin::default_round_square_base = {};
}
}
