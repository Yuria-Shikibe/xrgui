module;

#include <magic_enum/magic_enum.hpp>

#if defined(_MSC_VER)
// MSVC 使用 __pragma 可以在宏内展开
#define MY_PUSH_IGNORE_MODULE_INCLUDE_WARNING \
__pragma(warning(push)) \
__pragma(warning(disable: 5244))

#define MY_POP_IGNORE_MODULE_INCLUDE_WARNING \
__pragma(warning(pop))
#else
// 其他编译器（GCC/Clang）目前没有对应的 C5244 警告
#define MY_PUSH_IGNORE_MODULE_INCLUDE_WARNING
#define MY_POP_IGNORE_MODULE_INCLUDE_WARNING
#endif

module mo_yanxi.gui.assets;

import mo_yanxi.gui.assets.manager;

//TODO import image page from other place...
import mo_yanxi.graphic.image_atlas;
import mo_yanxi.gui.infrastructure;
import mo_yanxi.gui.elem.scroll_pane;
import mo_yanxi.gui.image_regions;
import align;

namespace svgs{
MY_PUSH_IGNORE_MODULE_INCLUDE_WARNING

constexpr char check[]{
#include <icons/basic/check.svg.h>
};

constexpr char textarea[]{
#include <icons/basic/textarea.svg.h>
};

constexpr char file[]{
#include <icons/basic/file.svg.h>
};

constexpr char folder[]{
#include <icons/basic/folder.svg.h>
};

constexpr char plus[]{
#include <icons/basic/plus.svg.h>
};

constexpr char data_server[]{
#include <icons/basic/data_server.svg.h>
};

constexpr char up[]{
#include <icons/basic/up.svg.h>
};
constexpr char left[]{
#include <icons/basic/left.svg.h>
};
constexpr char right[]{
#include <icons/basic/right.svg.h>
};
constexpr char down[]{
#include <icons/basic/down.svg.h>
};

constexpr char more[]{
#include <icons/basic/more.svg.h>
};

constexpr char side_bar[]{
#include <icons/side_bar.svg.h>
};

MY_POP_IGNORE_MODULE_INCLUDE_WARNING
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
			graphic::msdf::msdf_generator{graphic::msdf::create_capsule(32, 16)},
			math::usize2{80u, 64u}, 2
		});

	auto& boarder = load(page,
			"edge",
			graphic::sdf_load{
				graphic::msdf::msdf_generator{graphic::msdf::create_boarder(8.f, 2.f)}, extent, 3
			});

	auto& boarder_thin = load(page,
		"edge_thin",
		graphic::sdf_load{
			graphic::msdf::msdf_generator{graphic::msdf::create_boarder(8.f, 1.f)}, extent, 3
		});

	auto& base = load(page,
		"base",
		graphic::sdf_load{
			graphic::msdf::msdf_generator{graphic::msdf::create_solid_boarder(8.f)}, extent, 3
		});

	auto& side_bar = load(page,
		"side_bar",
		graphic::sdf_load{
			graphic::msdf::msdf_generator{svgs::side_bar, true, true}, std::nullopt, 3
		});

	using namespace builtin;
	auto& builtin_page = get_page();

	builtin_page.insert(shape_id::white, image_region_borrow{});
	builtin_page.insert(shape_id::row_separator, line);
	builtin_page.insert(shape_id::round_square_edge, boarder);
	builtin_page.insert(shape_id::round_square_edge_thin, boarder_thin);
	builtin_page.insert(shape_id::round_square_base, base);
	builtin_page.insert(shape_id::side_bar, side_bar);

	builtin::default_round_square_boarder = {boarder, align::padding2d<std::uint32_t>{}.set(12).expand(graphic::msdf::sdf_image_boarder), graphic::msdf::sdf_image_boarder};
	builtin::default_round_square_boarder_thin = {boarder_thin, align::padding2d<std::uint32_t>{}.set(12).expand(graphic::msdf::sdf_image_boarder), graphic::msdf::sdf_image_boarder};
	builtin::default_round_square_base = {base, align::padding2d<std::uint32_t>{}.set(12).expand(graphic::msdf::sdf_image_boarder), graphic::msdf::sdf_image_boarder};

}


void load_default_icons(graphic::image_atlas& image_atlas){
	auto& atlas = image_atlas;
	auto& page = atlas.create_image_page("ui");

	using namespace builtin;
	auto& builtin_page = get_page();

#define COMBINE(name) "i-"#name


#define LOAD_ICON(name) \
	{ \
	auto& i = load(page, COMBINE(name), \
	graphic::sdf_load{graphic::msdf::msdf_generator{std::string(svgs:: name), true}, math::usize2{64u, 64u}, 3}); \
	builtin_page.insert(shape_id:: name, i);	\
	}

	LOAD_ICON(check)
	LOAD_ICON(textarea)
	LOAD_ICON(file)
	LOAD_ICON(folder)
	LOAD_ICON(plus)
	LOAD_ICON(data_server)

	LOAD_ICON(right)
	LOAD_ICON(left)
	LOAD_ICON(up)
	LOAD_ICON(down)
	LOAD_ICON(more)

}

void dispose_generated_shapes(){
	style::global_default_style_drawer = {};
	style::global_scroll_pane_bar_drawer = {};

	builtin::default_round_square_boarder = {};
	builtin::default_round_square_boarder_thin = {};
	builtin::default_round_square_base = {};
}
}
