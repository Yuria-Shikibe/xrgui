module;

#include <vulkan/vulkan.h>

export module mo_yanxi.gui.default_config.round_styles;

import std;

import mo_yanxi.react_flow.flexible_value;

export import mo_yanxi.gui.style.palette;

export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.gui.elem.scroll_pane;
export import mo_yanxi.gui.elem.slider;

export import mo_yanxi.gui.image_regions;
export import mo_yanxi.graphic.draw.instruction;
export import mo_yanxi.gui.fx.instruction_extension;
export import mo_yanxi.gui.fx.fringe;

export import mo_yanxi.gui.style.tree;
export import mo_yanxi.gui.style.tree.draw;

namespace mo_yanxi::gui::style::spec{
export
struct static_metrics{
	gui::boarder value;

	style_tree_metrics operator()(const typed_style_tree_metrics_query_param<elem>& val) const noexcept{
		return {
			.inset = value
		};
	}
};

export
struct round_scroll_bar_entry{
	image_row_patch bar_shape;
	palette bar_palette;
	palette back_palette;
};

export
struct draw_round_scroll_bar : scroll_pane_bar_drawer{
	round_scroll_bar_entry entry;

	[[nodiscard]] explicit(false) draw_round_scroll_bar(round_scroll_bar_entry entry)
		: entry(std::move(entry)){
	}

	void operator()(const typed_draw_param<scroll_adaptor_base>& p) const{
		if(!p->layer_param.is_top() || p->opacity_scl < 1.f / 255.f) return;

		const auto& element = p.subject();
		state_guard _{element.renderer(), fx::batch_draw_mode::msdf};
		each_scroll_rect(element, p->draw_bound, [&](math::raw_frect bar_rect, bool is_hori){
			auto color = entry.bar_palette.on_instance(element).mul_a(p->opacity_scl);
			using namespace graphic::draw::instruction;
			element.renderer() << fx::row_patch_draw{
				.patch = &entry.bar_shape,
				.region = bar_rect,
				.color = color,
				.flags = is_hori ? row_patch_flags::none : row_patch_flags::transposed,
			};
		});
	}
};

export
[[nodiscard]] inline auto make_round_scroll_bar_style(round_scroll_bar_entry entry){
	return make_tree_node_ptr(tree_leaf{draw_round_scroll_bar{std::move(entry)}});
}

export
struct round_slider_entry{
	image_nine_region handle_shape;
	image_nine_region bar_shape;
	palette handle_palette;
	palette bar_palette;
	palette bar_back_palette;
	float bar_margin = 4.0f;
	float vert_margin = 2.0f;
};

export
struct draw_round_slider{
	using target_type = slider1d;
	round_slider_entry entry;

	[[nodiscard]] explicit(false) draw_round_slider(round_slider_entry entry)
		: entry(std::move(entry)){
	}

	void operator()(const typed_draw_param<slider1d>& p) const{
		if(!p->layer_param.is_top()) return;

		const auto& element = p.subject();
		auto region = p->draw_bound;
		float opacity_scl = p->opacity_scl;
		element.renderer().update_state(fx::push_constant{fx::batch_draw_mode::msdf});

		const auto extent = element.get_bar_handle_extent(region.extent());
		auto progress_scl = math::vec2{element.bar.get_progress()[0], 1.f};
		auto progress_temp_scl = math::vec2{element.bar.get_temp_progress()[0], 1.f};
		if(element.is_vertical()){
			progress_scl.swap_xy();
			progress_temp_scl.swap_xy();
		}

		auto pos1 = region.src + progress_scl * region.extent().fdim(extent);
		auto pos2 = region.src + progress_temp_scl * region.extent().fdim(extent);

		math::raw_frect filled_rect = region;
		if(element.is_vertical()){
			filled_rect.extent.y = math::fdim(pos1.y - region.src.y, entry.bar_margin);
			filled_rect.extent.x = math::fdim(filled_rect.extent.x, 2 * entry.vert_margin);
			filled_rect.src.x += entry.vert_margin;
		} else{
			filled_rect.extent.x = math::fdim(pos1.x - region.src.x, entry.bar_margin);
			filled_rect.extent.y = math::fdim(filled_rect.extent.y, 2 * entry.vert_margin);
			filled_rect.src.y += entry.vert_margin;
		}

		auto color = entry.handle_palette.on_instance(element).mul_a(opacity_scl);
		auto bar_color = entry.bar_palette.on_instance(element).mul_a(opacity_scl);

		if(filled_rect.extent.x > 0 && filled_rect.extent.y > 0){
			element.renderer() << fx::nine_patch_draw<&image_nine_region::get_row_coords_axis_scaled>{
				.patch = &entry.bar_shape,
				.region = filled_rect,
				.color = bar_color,
			};
		}

		element.renderer() << fx::nine_patch_draw<&image_nine_region::get_row_coords_scaled>{
			.patch = &entry.handle_shape,
			.region = {pos1, extent},
			.color = color,
		};

		element.renderer() << fx::nine_patch_draw<&image_nine_region::get_row_coords_scaled>{
			.patch = &entry.handle_shape,
			.region = {pos2, extent},
			.color = color.mul(1.05f),
		};
	}
};

export
[[nodiscard]] inline auto make_round_slider_style(round_slider_entry entry){
	return make_tree_node_ptr(tree_leaf{draw_round_slider{std::move(entry)}});
}

export
struct thin_slider_entry{
	image_row_patch bar_shape;
	palette handle_palette;
	palette bar_palette;
	palette bar_back_palette;
	float bar_thick = 12.0f;
};

export
struct draw_thin_slider{
	using target_type = slider1d;
	thin_slider_entry entry;

	[[nodiscard]] explicit(false) draw_thin_slider(thin_slider_entry entry)
		: entry(std::move(entry)){
	}

	void operator()(const typed_draw_param<slider1d>& p) const{
		if(!p->layer_param.is_top()) return;

		const auto& element = p.subject();
		auto region = p->draw_bound;
		float opacity_scl = p->opacity_scl;
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

		math::raw_frect filled_rect{region.src, base_off};
		if(element.is_vertical()){
			filled_rect.src.x += extent.x * .5f - entry.bar_thick;
			curr_off.x = base_off.x = extent.x * .5f;
			filled_rect.extent.y += extent.y * .5f;
			curr_off.y += extent.y * .5f;
			base_off.y += extent.y * .5f;
			filled_rect.extent.x = entry.bar_thick * 2.f;
		} else{
			filled_rect.src.y += extent.y * .5f - entry.bar_thick;
			curr_off.y = base_off.y = extent.y * .5f;
			filled_rect.extent.x += extent.x * .5f;
			curr_off.x += extent.x * .5f;
			base_off.x += extent.x * .5f;
			filled_rect.extent.y = entry.bar_thick * 2.f;
		}

		auto color = entry.handle_palette.on_instance(element).mul_a(opacity_scl);
		auto bar_color = entry.bar_palette.on_instance(element).mul_a(opacity_scl);

		if(filled_rect.extent.x > 0 && filled_rect.extent.y > 0){
			element.renderer() << fx::row_patch_draw{
				.patch = &entry.bar_shape,
				.region = filled_rect,
				.color = bar_color,
				.flags = element.is_vertical() ? graphic::draw::instruction::row_patch_flags::transposed
				                              : graphic::draw::instruction::row_patch_flags::none,
			};
		}

		element.renderer() << fx::fringe::poly(fx::circle{
			.pos = region.src + curr_off,
			.radius = {math::fdim(radius, 1.25f), radius + 1.25f},
			.color = {color, color},
		}, .7f);

		element.renderer() << fx::fringe::poly(fx::circle{
			.pos = region.src + base_off,
			.radius = {0, radius},
			.color = {color, color},
		}, .7f);
	}
};

export
[[nodiscard]] inline auto make_thin_slider_style(thin_slider_entry entry){
	return make_tree_node_ptr(tree_leaf{draw_thin_slider{std::move(entry)}});
}

export
struct create_entry{
	primitives::nine_patch_draw_entry edge;
	primitives::nine_patch_draw_entry base;
	primitives::nine_patch_draw_entry back;
	gui::boarder boarder{gui::default_boarder};

	[[nodiscard]] auto make_edge_only() const {
		return tree_tuple_fork{
				tree_direct{
					layer_router{
						style_config{0b01},
						tree_leaf{primitives::draw_nine_patch_hollow{edge}}
					}
				},
				tree_metrics_leaf{static_metrics{boarder}}
			};
	}

	[[nodiscard]] auto make_back_only() const {
		return tree_tuple_fork{
				tree_direct{
					layer_router{
						style_config{0b10},
						tree_leaf{primitives::draw_nine_patch{back}}
					}
				},
				tree_metrics_leaf{static_metrics{auto{boarder}.scl(.3f)}}
			};
	}

	[[nodiscard]] auto make_general() const {
		return tree_direct{
			tree_tuple_fork{
				layer_router{
					style_config{0b01},
					tree_leaf{primitives::draw_nine_patch_hollow{edge}},
				},
				layer_router{
					style_config{0b10},
					tree_leaf{primitives::draw_nine_patch{back}}
				},
				tree_metrics_leaf{static_metrics{boarder}}
			}
		};
	}

	[[nodiscard]] auto make_general_with_base() const {
		return tree_direct{
			tree_tuple_fork{
				layer_router{
					style_config{0b01},
					tree_tuple_fork{
						tree_leaf{primitives::draw_nine_patch{base}},
						tree_leaf{primitives::draw_nine_patch_hollow{edge}}
					}
				},
				layer_router{
					style_config{0b10},
					tree_leaf{primitives::draw_nine_patch{back}}
				},
				tree_metrics_leaf{static_metrics{boarder}}
			}
		};
	}
};



}
