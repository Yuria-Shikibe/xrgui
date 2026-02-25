module;

#include <vulkan/vulkan.h>

export module mo_yanxi.gui.style.round_square;

export import mo_yanxi.gui.style.palette;

export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.gui.image_regions;
export import mo_yanxi.gui.fx.instruction_extension;

namespace mo_yanxi::gui::style{

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
				state_guard _{element.renderer(), fx::batch_draw_mode::msdf,
				make_state_tag(fx::state_type::push_constant, VK_SHADER_STAGE_FRAGMENT_BIT)};

				if(base.image_view){
					auto color_base = base.pal.on_instance(element);
					element.renderer() << fx::nine_patch_draw{
						.nine_region = &base,
						.region = region,
						.color = color_base,
					};
				}

				if(edge.image_view){
					auto color_edge = edge.pal.on_instance(element);
					element.renderer() << fx::nine_patch_draw{
						.nine_region = &edge,
						.region = region,
						.color = color_edge,
					};
				}
			}



		}else if(layer_param == 1){
			if(back.image_view){
				state_guard _{element.renderer(), fx::batch_draw_mode::msdf,
				make_state_tag(fx::state_type::push_constant, VK_SHADER_STAGE_FRAGMENT_BIT)};

				auto color = back.pal.on_instance(element);
				element.renderer() << fx::nine_patch_draw{
					.nine_region = &back,
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