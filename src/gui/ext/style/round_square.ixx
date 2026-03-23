module;

#include <stdexcept>
#include <vulkan/vulkan.h>

export module mo_yanxi.gui.style.round_square;

import std;


export import mo_yanxi.gui.style.palette;

export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.gui.elem.scroll_pane;
export import mo_yanxi.gui.elem.slider;

export import mo_yanxi.gui.image_regions;
export import mo_yanxi.graphic.draw.instruction;
export import mo_yanxi.gui.fx.instruction_extension;
export import mo_yanxi.gui.fx.fringe;

namespace mo_yanxi::gui::style{

export
struct round_scroll_bar_style : scroll_pane_bar_drawer{
	image_row_patch bar_shape;
	palette bar_palette;
	palette back_palette;

protected:

	void draw_layer_impl(const scroll_pane& element, math::frect region, float opacityScl, fx::layer_param layer_param) const override{
		if(opacityScl < 1 / 255.f)return;
		switch(layer_param.layer_index){
		case 0 :{
			state_guard _{element.renderer(), fx::batch_draw_mode::msdf};

			each_scroll_rect(element, region, [&](math::raw_frect bar_rect, bool isHori){
				auto color = bar_palette.on_instance(element).mul_a(opacityScl);
				using namespace graphic::draw::instruction;
				element.renderer() << fx::row_patch_draw{
					.patch = &bar_shape,
					.region = bar_rect,
					.color = color,
					.flags = isHori ? row_patch_flags::none : row_patch_flags::transposed
				};
			});
			break;
		}
		case 1 :{
			// each_scroll_rect(element, region, [&](math::raw_frect bar_rect){
			// 	element.renderer().push(graphic::draw::instruction::rect_aabb{
			// 			.v00 = bar_rect.vert_00(),
			// 			.v11 = bar_rect.vert_11(),
			// 			.vert_color = graphic::colors::light_gray.copy_set_a(opacityScl)
			// 		});
			// });
			// break;
		}
		default : break;
		}
	}
};

export
struct round_slider_drawer : slider1d_drawer{
    image_nine_region handle_shape;
    image_nine_region bar_shape;

    palette handle_palette;
    palette bar_palette;
    palette bar_back_palette;

    float bar_margin = 4.0f;
	float vert_margin = 2.0f;

protected:
    void draw_layer_impl(const target_type& element, math::frect region, float opacityScl,
	    fx::layer_param layer_param) const override{
	    if(!layer_param.is_top()) return;

	    state_guard _{element.renderer(), fx::batch_draw_mode::msdf};

	    const auto extent = element.get_bar_handle_extent(region.extent());
    	auto progress_scl = math::vec2{element.bar.get_progress()[0], 1.f};
    	auto progress_temp_scl = math::vec2{element.bar.get_temp_progress()[0], 1.f};
    	if(element.is_vertical()){
    		progress_scl.swap_xy();
    		progress_temp_scl.swap_xy();
    	}

	    auto pos1 = region.src + progress_scl * region.extent().fdim(extent);
	    auto pos2 = region.src + progress_temp_scl * region.extent().fdim(extent);

	    auto& renderer = element.get_scene().renderer();
	    using namespace graphic;
	    using namespace graphic::draw::instruction;

	    // 计算左侧进度条的矩形区域
	    math::raw_frect filled_rect = region;

	    if(element.is_vertical()){
	    	// 垂直滑动条：宽度撑满，高度为从起点到滑块起点的距离减去边距
	    	filled_rect.extent.y = math::fdim(pos1.y - region.src.y, bar_margin);
	    	filled_rect.extent.x = math::fdim(filled_rect.extent.x, 2 * vert_margin);
	    	filled_rect.src.x += vert_margin;
	    } else{

	    	// 水平滑动条：高度撑满，宽度为从起点到滑块起点的距离减去边距（使用 std::fdim 防止负数）
	    	filled_rect.extent.x = math::fdim(pos1.x - region.src.x, bar_margin);
	    	filled_rect.extent.y = math::fdim(filled_rect.extent.y, 2 * vert_margin);
	    	filled_rect.src.y += vert_margin;
	    }

	    auto color = handle_palette.on_instance(element).mul_a(opacityScl);
	    auto bar_color = bar_palette.on_instance(element).mul_a(opacityScl);

	    // 绘制已填充的进度条形状（即==========部分）
	    if(filled_rect.extent.x > 0 && filled_rect.extent.y > 0){
		    renderer << fx::nine_patch_draw<&image_nine_region::get_row_coords_axis_scaled>{
				    .patch = &bar_shape,
				    .region = filled_rect,
				    .color = bar_color,
			    };
	    }

	    // 绘制滑块把手 (pos1)
	    renderer << fx::nine_patch_draw<&image_nine_region::get_row_coords_scaled>{
			    .patch = &handle_shape,
			    .region = {pos1, extent},
			    .color = color,
		    };

	    // 绘制临时滑块把手/拖拽阴影 (pos2)
	    renderer << fx::nine_patch_draw<&image_nine_region::get_row_coords_scaled>{
			    .patch = &handle_shape,
			    .region = {pos2, extent},
			    .color = color.mul(.75f),
		    };
    }
};

export
struct thin_slider_drawer : slider1d_drawer{
    image_row_patch bar_shape;

    palette handle_palette;
    palette bar_palette;
    palette bar_back_palette;

    // float bar_margin = 4.0f;
	float bar_thick = 12.0f;

protected:
    void draw_layer_impl(const target_type& element, math::frect region, float opacityScl,
	    fx::layer_param layer_param) const override{
	    if(!layer_param.is_top()) return;

	    state_guard _{element.renderer(), fx::batch_draw_mode::msdf};

    	const auto extent = element.get_bar_handle_extent(region.extent());
    	auto progress_scl = math::vec2{element.bar.get_progress()[0], 1.f};
    	auto progress_temp_scl = math::vec2{element.bar.get_temp_progress()[0], 1.f};
    	if(element.is_vertical()){
    		progress_scl.swap_xy();
    		progress_temp_scl.swap_xy();
    	}

    	auto base_off = progress_scl * region.extent().fdim(extent);
    	auto curr_off = progress_temp_scl * region.extent().fdim(extent);
    	auto radius = math::fdim(extent.get_min() / 2.f, 4.f);

	    auto& renderer = element.get_scene().renderer();
	    using namespace graphic;
	    using namespace graphic::draw::instruction;

	    // 计算左侧进度条的矩形区域
	    math::raw_frect filled_rect{region.src, base_off};

	    if(element.is_vertical()){
		    filled_rect.src.x += extent.x * .5f - bar_thick;
		    curr_off.x = base_off.x = extent.x * .5f;

		    filled_rect.extent.y += extent.y * .5f;
		    curr_off.y += extent.y * .5f;
		    base_off.y += extent.y * .5f;

		    filled_rect.extent.x = bar_thick * 2.f;
	    } else{
		    // 水平滑动条：高度撑满，宽度为从起点到滑块起点的距离减去边距（使用 std::fdim 防止负数）
		    filled_rect.src.y += extent.y * .5f - bar_thick;
		    curr_off.y = base_off.y = extent.y * .5f;

		    filled_rect.extent.x += extent.x * .5f;
		    curr_off.x += extent.x * .5f;
		    base_off.x += extent.x * .5f;

		    filled_rect.extent.y = bar_thick * 2.f;
	    }

	    auto color = handle_palette.on_instance(element).mul_a(opacityScl);
	    auto bar_color = bar_palette.on_instance(element).mul_a(opacityScl);

	    // 绘制已填充的进度条形状（即==========部分）
	    if(filled_rect.extent.x > 0 && filled_rect.extent.y > 0){
		    renderer << fx::row_patch_draw{
				    .patch = &bar_shape,
				    .region = filled_rect,
				    .color = bar_color,
		    		.flags = element.is_vertical() ? row_patch_flags::transposed : row_patch_flags::none
			    };
	    }

    	fx::fringe::poly(renderer, fx::circle{
			.pos = region.src + curr_off,
			.radius = {math::fdim(radius, 1.25f), radius + 1.25f},
			.color = {color, color}
		}, .7f);

    	fx::fringe::poly(renderer, fx::circle{
			.pos = region.src + base_off,
			.radius = {0, radius},
			.color = {color, color}
		}, .7f);


    }
};

struct basic_elem_style_drawer : elem_style_drawer{
	align::spacing boarder{default_boarder};
	float disabled_opacity{.5f};

	using elem_style_drawer::elem_style_drawer;

[[nodiscard]] gui::boarder get_boarder() const noexcept override{
		return boarder;
	}

	[[nodiscard]] float content_opacity(const elem& element) const override{
		if(element.is_disabled()){
			return disabled_opacity;
		} else{
			return style_drawer::content_opacity(element);
		}
	}

};

export
struct round_style_no_edge : basic_elem_style_drawer{
	palette_with<image_nine_region> base{};
	palette_with<image_nine_region> back{};


	explicit round_style_no_edge(const tags::persistent_tag_t& persistent_tag)
			: basic_elem_style_drawer(persistent_tag, {{0b11}}){
	}

	round_style_no_edge() : basic_elem_style_drawer({{0b11}}){

	}

protected:
	void draw_base(const elem& element, math::frect region, float opacityScl) const{
		auto color_base = base.pal.on_instance(element).mul_a(opacityScl);
		element.renderer() << fx::nine_patch_draw<>{
			.patch = &base,
			.region = region,
			.color = color_base,
		};
	}

	void draw_back(const elem& element, math::frect region, float opacityScl) const{
		auto color = back.pal.on_instance(element).mul_a(opacityScl);
		element.renderer() << fx::nine_patch_draw<>{
			.patch = &back,
			.region = region,
			.color = color,
		};
	}

	void draw_layer_impl(const elem& element, math::frect region, float opacityScl, fx::layer_param layer_param) const override{
		if(layer_param == 0){
			if(base.image_view){
				element.renderer().update_state(fx::batch_draw_mode::msdf);
				draw_base(element, region, opacityScl);
			}
		}else if(layer_param == 1){
			if(back.image_view){
				element.renderer().update_state(fx::batch_draw_mode::msdf);
				draw_back(element, region, opacityScl);
			}
		}
	}

};

export
struct round_style : round_style_no_edge{
	palette_with<image_nine_region> edge{};

	using round_style_no_edge::round_style_no_edge;

protected:
	void draw_layer_impl(const elem& element, math::frect region, float opacityScl,
		fx::layer_param layer_param) const override{
		if(layer_param == 0){
			if(base.image_view || edge.image_view){
				element.renderer().update_state(fx::batch_draw_mode::msdf);

				if(base.image_view){
					draw_base(element, region, opacityScl);
				}

				if(edge.image_view){
					auto color_edge = edge.pal.on_instance(element).mul_a(opacityScl);
					element.renderer() << fx::nine_patch_draw<>{
							.patch = &edge,
							.region = region,
							.color = color_edge,
						};
				}
			}
		} else if(layer_param == 1){
			if(back.image_view){
				element.renderer().update_state(fx::batch_draw_mode::msdf);
				draw_back(element, region, opacityScl);
			}
		}
	}
};

export
struct round_style_edge_only : basic_elem_style_drawer{
	palette_with<image_nine_region> edge{};

	explicit round_style_edge_only(const tags::persistent_tag_t& persistent_tag)
			: basic_elem_style_drawer(persistent_tag, {{0b1}}){
	}

	round_style_edge_only() : basic_elem_style_drawer({{0b1}}){

	}

protected:
	void draw_layer_impl(const elem& element, math::frect region, float opacityScl,
		fx::layer_param layer_param) const override{
		if(layer_param.is_top()){
			element.renderer().update_state(fx::batch_draw_mode::msdf);

			auto color_edge = edge.pal.on_instance(element).mul_a(opacityScl);
			element.renderer() << fx::nine_patch_draw<>{
				.patch = &edge,
				.region = region,
				.color = color_edge,
			};
		}
	}
};

export
enum struct side_bar_pos{
	left, right, top, bottom
};

export
struct side_bar_style : round_style_no_edge{
	palette_with<image_row_patch> bar{};
	side_bar_pos pos{};
	graphic::draw::instruction::row_patch_flags flags{};


	using round_style_no_edge::round_style_no_edge;

	constexpr float get_bar_stroke() const noexcept{
		switch(pos){
		case side_bar_pos::left : return boarder.left * .8f;
		case side_bar_pos::right : return boarder.right * .8f;
		case side_bar_pos::top : return boarder.top * .8f;
		case side_bar_pos::bottom : return boarder.bottom * .8f;
		}
		std::unreachable();
	}

	constexpr graphic::draw::instruction::row_patch_flags get_actual_flag() const noexcept{
		//TODO provide uv flip
		switch(pos){
		case side_bar_pos::left : return flags | graphic::draw::instruction::row_patch_flags::transposed;
		case side_bar_pos::right : return flags | graphic::draw::instruction::row_patch_flags::transposed;
		case side_bar_pos::top : return flags & ~graphic::draw::instruction::row_patch_flags::transposed;
		case side_bar_pos::bottom : return flags & ~graphic::draw::instruction::row_patch_flags::transposed;
		}
		std::unreachable();
	}
protected:
	void draw_layer_impl(const elem& element, math::frect region, float opacityScl,
		fx::layer_param layer_param) const override{
		if(layer_param == 0){
			if(base.image_view || bar.get_image_view()){
				element.renderer().update_state(fx::batch_draw_mode::msdf);

				if(base.image_view){
					draw_base(element, region, opacityScl);
				}

				if(bar.get_image_view()){
					math::vec2 src;
					math::vec2 ext;

					switch(pos){
					case side_bar_pos::left :
						src = region.src;
						ext = {get_bar_stroke(), region.extent().y};
						break;
					case side_bar_pos::right :
						src = region.vert_01();
						ext = {-get_bar_stroke(), region.extent().y};
						break;
					case side_bar_pos::top :
						src = region.src;
						ext = {region.extent().x, get_bar_stroke()};
						break;
					case side_bar_pos::bottom :
						src = region.vert_10();
						ext = {region.extent().x, -get_bar_stroke()};
						break;
					default:
						std::unreachable();
					}

					element.renderer() << fx::row_patch_draw{
						.patch = &bar,
						.region = {src, ext},
						.color = bar.pal.on_instance(element).mul_a(opacityScl),
						.flags = get_actual_flag()
					};
				}
			}
		} else if(layer_param == 1){
			if(back.image_view){
				element.renderer().update_state(fx::batch_draw_mode::msdf);
				draw_back(element, region, opacityScl);
			}
		}
	}

};
}