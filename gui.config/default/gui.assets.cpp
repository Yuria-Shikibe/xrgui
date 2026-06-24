module;

#if defined(_MSC_VER)

#define MY_PUSH_IGNORE_MODULE_INCLUDE_WARNING \
__pragma(warning(push)) \
__pragma(warning(disable: 5244))

#define MY_POP_IGNORE_MODULE_INCLUDE_WARNING \
__pragma(warning(pop))
#else

#define MY_PUSH_IGNORE_MODULE_INCLUDE_WARNING
#define MY_POP_IGNORE_MODULE_INCLUDE_WARNING
#endif

module mo_yanxi.gui.cfg.builtin.assets;

import mo_yanxi.gui.assets.manager;

//TODO import image page from other place...
import mo_yanxi.graphic.image_atlas;
import mo_yanxi.gui.infrastructure;
import mo_yanxi.gui.elem.scroll_pane;
import mo_yanxi.gui.image_regions;

namespace svgs{
MY_PUSH_IGNORE_MODULE_INCLUDE_WARNING

#include <assets_summary.h>

MY_POP_IGNORE_MODULE_INCLUDE_WARNING
}


auto& load(mo_yanxi::graphic::image_page& page, std::string&& name, mo_yanxi::graphic::sdf_load&& task){
	return page.register_named_region(std::move(name), std::move(task), true).region;
}

namespace mo_yanxi::gui::cfg::builtin{


void generate_default_shapes(graphic::image_atlas& image_atlas){
	auto& atlas = image_atlas;
	auto& page = atlas.create_image_page("ui", {.usage = graphic::image_page_usage::msdf});

	auto& line = load(page, "line", {
			graphic::msdf::msdf_generator{graphic::msdf::create_capsule(32, 16)},
			math::usize2{80u, 64u}, 2
		});

	auto& side_bar = load(page,
		"side_bar",
		graphic::sdf_load{
			graphic::msdf::msdf_generator{svgs::icons::side_bar_svg, true, true}, std::nullopt, 3
		});

	auto& builtin_page = assets::builtin::get_page();

	builtin_page.insert(assets::builtin::shape_id::white, constant_image_region_borrow{});
	builtin_page.insert(assets::builtin::shape_id::row_separator, line);
	builtin_page.insert(assets::builtin::shape_id::side_bar, side_bar);

	assets::round_square::bind_image_page(page);
	(void) assets::round_square::border();
	(void) assets::round_square::thin_border();
	(void) assets::round_square::base();

}


void load_default_icons(graphic::image_atlas& image_atlas){
	auto& atlas = image_atlas;
	auto& page = atlas.create_image_page("ui", {.usage = graphic::image_page_usage::msdf});
	auto& builtin_page = assets::builtin::get_page();

#define COMBINE(name) "i-"#name




#define LOAD_ICON(name, orient_contours) \
	{ \
	auto& i = load(page, COMBINE(name), \
	graphic::sdf_load{graphic::msdf::msdf_generator{std::string(svgs::icons::basic:: name##_svg), true, orient_contours}, math::usize2{64u, 64u}, 3}); \
	builtin_page.insert(assets::builtin::shape_id:: name, i);	\
	}

	LOAD_ICON(arrow_down, false)
	LOAD_ICON(arrow_up, false)

	LOAD_ICON(check, false)
	LOAD_ICON(textarea, false)
	LOAD_ICON(file, false)
	LOAD_ICON(folder, false)
	LOAD_ICON(plus, false)
	LOAD_ICON(data_server, false)
	LOAD_ICON(time, false)
	LOAD_ICON(alphabetical_sorting, false)
	LOAD_ICON(row_height, false)

	LOAD_ICON(right, false)
	LOAD_ICON(left, false)
	LOAD_ICON(up, false)
	LOAD_ICON(down, false)
	LOAD_ICON(more, false)
	LOAD_ICON(search, false)
	LOAD_ICON(sort_two, false)

}

void dispose_generated_shapes(){
	assets::round_square::clear();
}
}
