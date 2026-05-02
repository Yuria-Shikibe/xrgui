module mo_yanxi.gui.default_config.scene;

import mo_yanxi.gui.examples.default_config.constants;

void mo_yanxi::gui::example::set_cursors(scene& scene){
	auto& cm = scene.resources().cursor_collection_manager;

	cm.add_cursor<assets::builtin::cursor::default_cursor_regular>(style::cursor_type::regular);
	cm.add_cursor<assets::builtin::cursor::default_cursor_drag>(style::cursor_type::drag);
	cm.add_cursor<image_cursor>(style::cursor_type::textarea,
	                            assets::builtin::get_page()[assets::builtin::shape_id::textarea].value_or({}));

	cm.add_cursor<assets::builtin::cursor::default_cursor_arrow>(style::cursor_decoration_type::to_left,
	                                                             style::cursor_arrow_direction::left);
	cm.add_cursor<assets::builtin::cursor::default_cursor_arrow>(style::cursor_decoration_type::to_right,
	                                                             style::cursor_arrow_direction::right);
	cm.add_cursor<assets::builtin::cursor::default_cursor_arrow>(style::cursor_decoration_type::to_up,
	                                                             style::cursor_arrow_direction::up);
	cm.add_cursor<assets::builtin::cursor::default_cursor_arrow>(style::cursor_decoration_type::to_down,
	                                                             style::cursor_arrow_direction::down);
}

mo_yanxi::gui::example::make_style_result mo_yanxi::gui::example::make_styles(scene_resources& scene){
	auto pal_front_distributor_ptr = react_flow::node_pointer(react_flow::provider_cached<style::palette>{});
	auto pal_back_distributor_ptr = react_flow::node_pointer(react_flow::provider_cached<style::palette>{});

	auto& pal_front_distributor = static_cast<make_style_result::node_type&>(*pal_front_distributor_ptr);
	auto& pal_back_distributor = static_cast<make_style_result::node_type&>(*pal_back_distributor_ptr);

	pal_front_distributor.update_value(math::lerp(style::pal::white, style::pal::dark, .155f));
	pal_back_distributor.update_value(style::pal::dark);
	react_flow::node_pointer pal_front_distributor_cursor_ignored{
			react_flow::make_transformer([](style::palette palette){
				return palette.set_cursor_ignored();
			})
		};
	pal_front_distributor_cursor_ignored->connect_predecessor(pal_front_distributor);

	{
		auto& manager = scene.style_tree_manager;
		manager.register_style<slider1d>(style::spec::make_round_slider_style({
				.handle_shape = assets::builtin::default_round_square_base,
				.bar_shape = assets::builtin::default_round_square_base,
				.handle_palette = style::make_theme_palette(graphic::colors::ROYAL.create_lerp(graphic::colors::aqua, .3f)),
				.bar_palette = style::make_theme_palette(graphic::colors::ROYAL.create_lerp(graphic::colors::aqua, .3f)),
				.bar_back_palette = style::make_theme_palette(graphic::colors::ROYAL.create_lerp(graphic::colors::aqua, .3f)),
				.bar_margin = 4.f,
				.vert_margin = 5.f
			}));

		manager.register_style<slider2d>(style::make_default_slider2d_style());
		manager.register_style<progress_bar>(style::make_default_progress_style());
		manager.register_style<scroll_adaptor_base>(style::spec::make_round_scroll_bar_style({
				.bar_shape = assets::builtin::get_separator_row_patch(),
				.bar_palette = style::pal::white.copy().mul_rgb(.8f),
				.back_palette = {},
			}));

		style::spec::create_entry e{
				.edge = {assets::builtin::default_round_square_boarder_thin, {pal_front_distributor}},
				.base = {},
				.back = {assets::builtin::default_round_square_base, {pal_back_distributor}}
			};

		auto [itr, rst] = manager.register_style<elem>(style::make_tree_node_ptr(e.make_general()));
		auto& col = itr->second;
		auto slice = style::style_tree_slice<elem>{col};

		col.default_family.set(style::family_variant::edge_only, style::make_tree_node_ptr(e.make_edge_only()));
		col.default_family.set(style::family_variant::base_only, style::make_tree_node_ptr(e.make_back_only()));

		static constexpr auto color_to_dark = [](graphic::color c){
			return c.set_value(.12f).shift_saturation(-.05f);
		};

		{
			auto gst = e;
			auto& cursor_ignored_node = *pal_front_distributor_cursor_ignored;
			gst.edge.pal = {cursor_ignored_node};
			auto back_pal = gst.back.pal.node.get_value().copy();
			back_pal.set_cursor_ignored();
			gst.back.pal = {back_pal};
			col.default_family.set(style::family_variant::general_static,
			                       style::make_tree_node_ptr(gst.make_general()));
		}

		{
			auto gst = e;
			static constexpr auto baseColor = graphic::colors::aqua.create_lerp(graphic::colors::AQUA_SKY, .5f);
			gst.edge.pal = {style::make_theme_palette(baseColor)};
			gst.back.pal = {style::make_theme_palette(color_to_dark(baseColor))};
			col.default_family.set(style::family_variant::accent, style::make_tree_node_ptr(gst.make_general()));
		}

		{
			auto gst = e;
			static constexpr auto baseColor = graphic::colors::red_dusted.create_lerp(graphic::colors::white, .1f);
			gst.edge.pal = {style::make_theme_palette(baseColor)};
			gst.back.pal = {style::make_theme_palette(color_to_dark(baseColor))};
			col.default_family.set(style::family_variant::invalid, style::make_tree_node_ptr(gst.make_general()));
		}

		{
			auto gst = e;
			static constexpr auto baseColor = graphic::color::from_rgba8888(0XDBBB6AFF).create_lerp(
				graphic::colors::pale_yellow, .5f);
			gst.edge.pal = {style::make_theme_palette(baseColor)};
			gst.back.pal = {style::make_theme_palette(color_to_dark(baseColor))};
			col.default_family.set(style::family_variant::warning, style::make_tree_node_ptr(gst.make_general()));
		}

		{
			auto gst = e;
			static constexpr auto baseColor = graphic::colors::pale_green;
			gst.edge.pal = {style::make_theme_palette(baseColor)};
			gst.back.pal = {style::make_theme_palette(color_to_dark(baseColor))};
			col.default_family.set(style::family_variant::accepted, style::make_tree_node_ptr(gst.make_general()));
		}

		{
			auto gst = e;
			gst.base.patch = {assets::builtin::default_round_square_base};
			auto base_pal = gst.back.pal.node.get_value().copy();
			base_pal.mul_alpha(2.f);
			gst.base.pal = {base_pal};
			col.default_family.set(style::family_variant::solid,
			                       style::make_tree_node_ptr(gst.make_general_with_base()));
		}
	}

	return {std::move(pal_front_distributor_ptr), std::move(pal_back_distributor_ptr)};
}

namespace mo_yanxi::gui::example{

#pragma region Scene
void example_scene::draw_at(math::frect clipspace, draw_call_stack& call_stack){
	auto c = get_region().intersection_with(clipspace);
	const auto bound = c.round<int>();

	auto& cfg = pass_config;

	for(unsigned i = 0; i < cfg.size(); ++i){
		renderer().update_state(cfg[i].begin_config);
		renderer().update_state(fx::push_constant{gpip::default_draw_constants{}});

		call_stack.each({
				.current_subject = this,
				.draw_bound = c,
				.opacity_scl = 1,
				.layer_param = {i}
			});

		if(cfg[i].end_config)
			renderer().update_state(fx::blit_config{
					.blit_region = {bound.src, bound.extent()},
					.pipe_info = cfg[i].end_config.value()
				});
	}


	if(auto tail = cfg.get_tail_blit())
		renderer().update_state(fx::blit_config{
				.blit_region = {bound.src, bound.extent()},
				.pipe_info = tail.value()
			});
}

void example_scene::draw_impl(rect clip){
	if(auto flags = check_display_state_changed(); flags != elem_tree_channel{}){
		if((flags & elem_tree_channel::regular) != elem_tree_channel{}){
			draw_recorder rec{call_stack_regular_};
			root().record_draw_layer(rec);
		}

		if((flags & elem_tree_channel::tooltip) != elem_tree_channel{}){
			auto seq = tooltip_manager_.get_draw_sequence();
			call_stack_tooltip_.resize(seq.size());
			for(auto&& [idx, elem] : seq | std::views::enumerate){
				draw_recorder rec{call_stack_tooltip_[idx]};
				elem.element->record_draw_layer(rec);
			}
		}

		if((flags & elem_tree_channel::overlay) != elem_tree_channel{}){
			auto seq = overlay_manager_.get_draw_sequence();
			call_stack_overlay_.resize(seq.size());
			for(auto&& [idx, elem] : seq | std::views::enumerate){
				draw_recorder rec{call_stack_overlay_[idx]};
				elem->record_draw_layer(rec);
			}
		}
	}

	renderer().init_timeline_variable();


	{
		viewport_guard _{renderer(), get_region()};

		for(const auto& [tooltip, stack] : std::ranges::views::zip(tooltip_manager_.get_draw_sequence(),
		                                                           call_stack_tooltip_)){
			if(!tooltip.belowScene) continue;
			draw_at(tooltip.element->bound_abs(), stack);
		}

		draw_at(root().bound_abs(), call_stack_regular_);

		for(const auto& [overlay, stack] : std::ranges::views::zip(overlay_manager_.get_draw_sequence(),
		                                                           call_stack_overlay_)){
			draw_at(overlay->bound_abs(), stack);
		}

		for(const auto& [tooltip, stack] : std::ranges::views::zip(tooltip_manager_.get_draw_sequence(),
		                                                           call_stack_tooltip_)){
			if(tooltip.belowScene) continue;
			draw_at(tooltip.element->bound_abs(), stack);
		}
	}

	if(input_handler_.inputs_.is_cursor_inbound()){
		renderer().update_state(fx::pipeline_config{
				.pipeline_index = gpip::idx::cursor_outline,
				.draw_targets = {0b1}
			});

		renderer().update_state(fx::push_constant{1.f});

		auto region = current_cursor_drawers_.draw(*this, resources_->cursor_collection_manager.get_cursor_size());

		renderer().update_state(fx::blit_config{
				{
					.src = region.src.as<int>(),
					.extent = region.extent().as<int>()
				},
				{.pipeline_index = cpip_idx::blend}
			});
	}
}
#pragma endregion

}