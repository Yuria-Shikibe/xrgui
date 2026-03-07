module;

#include <vulkan/vulkan.h>

export module mo_yanxi.gui.style.round_square;

export import mo_yanxi.gui.style.palette;

export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.gui.elem.scroll_pane;

export import mo_yanxi.gui.image_regions;
export import mo_yanxi.gui.fx.instruction_extension;

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
struct round_style : elem_style_drawer{
	align::spacing boarder{default_boarder};
	palette_with<image_nine_region> base{};
	palette_with<image_nine_region> edge{};
	palette_with<image_nine_region> back{};
	float disabled_opacity{.5f};

	explicit round_style(const tags::persistent_tag_t& persistent_tag)
		: elem_style_drawer(persistent_tag, {{0b11}}){
	}

	round_style() : elem_style_drawer({{0b11}}){

	}

protected:
	void draw_layer_impl(const elem& element, math::frect region, float opacityScl, fx::layer_param layer_param) const override{
		if(layer_param == 0){
			bool any = base.image_view || edge.image_view;

			if(any){
				state_guard _{element.renderer(), fx::batch_draw_mode::msdf};

				if(base.image_view){
					auto color_base = base.pal.on_instance(element).mul_a(opacityScl);
					element.renderer() << fx::nine_patch_draw{
						.patch = &base,
						.region = region,
						.color = color_base,
					};
				}

				if(edge.image_view){
					auto color_edge = edge.pal.on_instance(element).mul_a(opacityScl);
					element.renderer() << fx::nine_patch_draw{
						.patch = &edge,
						.region = region,
						.color = color_edge,
					};
				}
			}



		}else if(layer_param == 1){
			if(back.image_view){
				state_guard _{element.renderer(), fx::batch_draw_mode::msdf};

				auto color = back.pal.on_instance(element).mul_a(opacityScl);
				element.renderer() << fx::nine_patch_draw{
					.patch = &back,
					.region = region,
					.color = color,
				};
			}
		}
	}

public:
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
}