//
// Created by Matrix on 2025/11/19.
//

module mo_yanxi.gui.examples;



import std;

import binary_trace;

import mo_yanxi.gui.infrastructure;
import mo_yanxi.gui.elem.group;
import mo_yanxi.gui.global;

import mo_yanxi.react_flow;
import mo_yanxi.react_flow.common;

import mo_yanxi.gui.elem.scaling_stack;
import mo_yanxi.gui.elem.sequence;
import mo_yanxi.gui.elem.scroll_pane;
import mo_yanxi.gui.elem.collapser;
import mo_yanxi.gui.elem.table;
import mo_yanxi.gui.elem.grid;
import mo_yanxi.gui.elem.menu;
import mo_yanxi.gui.elem.slider;
import mo_yanxi.gui.elem.progress_bar;
import mo_yanxi.gui.elem.image_frame;
import mo_yanxi.gui.elem.image_frame;
import mo_yanxi.gui.elem.drag_split;
import mo_yanxi.gui.elem.label;
import mo_yanxi.gui.elem.label_v2;
import mo_yanxi.gui.elem.text_edit_v2;
import mo_yanxi.gui.elem.viewport;
import mo_yanxi.gui.elem.check_box;

import mo_yanxi.gui.elem.text_holder;

import mo_yanxi.gui.infrastructure;
import mo_yanxi.gui.elem.text_holder;
import mo_yanxi.font;

import mo_yanxi.typesetting.util;
import mo_yanxi.font;
import mo_yanxi.font.manager;
import mo_yanxi.graphic.color;
import mo_yanxi.heterogeneous.open_addr_hash;
import align;

import mo_yanxi.typesetting;
import mo_yanxi.graphic.draw.instruction.recorder;


import mo_yanxi.gui.compound.color_picker;
import mo_yanxi.gui.compound.named_slider;

import mo_yanxi.gui.style.round_square;
import mo_yanxi.gui.style.palette;

import mo_yanxi.gui.assets.manager;

import mo_yanxi.backend.communicator;
import mo_yanxi.backend.vulkan.context;


namespace mo_yanxi::gui::example{
struct vp : gui::viewport{
	[[nodiscard]] vp(scene& scene, elem* parent)
		: viewport(scene, parent){
	}

	void draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const override{
		viewport::draw_layer(clipSpace, param);

		if(param.is_top()){
			viewport_begin();

			renderer() << graphic::draw::instruction::rect_aabb{
					.v00 = {-50, -50},
					.v11 = {50, 50},
					.vert_color = {graphic::colors::white}
				};

			auto trans = renderer().top_viewport().get_element_to_root_screen();


			// renderer() << graphic::draw::instruction::rect_aabb{
			// 	.v00 = {content_bound_abs().vert_00()},
			// 	.v11 = {content_bound_abs().vert_11()},
			// 	.vert_color = {graphic::colors::white}
			// };

			viewport_end();
		}
	}
};

void set_cursors(scene& scene){
	auto& cm = scene.cursor_collection_manager;

	cm.add_cursor<assets::builtin::cursor::default_cursor_regular>(style::cursor_type::regular);
	cm.add_cursor<assets::builtin::cursor::default_cursor_drag>(style::cursor_type::drag);

	cm.add_cursor<assets::builtin::cursor::default_cursor_arrow>(style::cursor_decoration_type::to_left,
		style::cursor_arrow_direction::left);
	cm.add_cursor<assets::builtin::cursor::default_cursor_arrow>(style::cursor_decoration_type::to_right,
		style::cursor_arrow_direction::right);
	cm.add_cursor<assets::builtin::cursor::default_cursor_arrow>(style::cursor_decoration_type::to_up,
		style::cursor_arrow_direction::up);
	cm.add_cursor<assets::builtin::cursor::default_cursor_arrow>(style::cursor_decoration_type::to_down,
		style::cursor_arrow_direction::down);
}

void make_styles(scene& scene){
	auto& sm = scene.style_manager;

	{
		referenced_ptr<style::round_style> round_style{std::in_place};
		round_style->edge.pal = style::pal::white.border;
		round_style->edge = assets::builtin::default_round_square_boarder_thin;
		round_style->back.pal = style::pal::dark;
		round_style->back = assets::builtin::default_round_square_base;

		// sm.register_style<style::elem_style_drawer>(round_style);
		sm.register_style<style::elem_style_drawer>(referenced_ptr<style::debug_elem_drawer>{std::in_place});
	}

	{
		referenced_ptr<style::round_scroll_bar_style> round_scroll_bar_style{std::in_place};
		round_scroll_bar_style->bar_shape = assets::builtin::get_separator_row_patch();
		round_scroll_bar_style->bar_palette = style::pal::white.border.copy().mul_rgb(.8f);

		sm.register_style<style::scroll_pane_bar_drawer>(std::move(round_scroll_bar_style));
	}

	{
		referenced_ptr<style::thin_slider_drawer> round_scroll_bar_style{std::in_place};

		constexpr auto pal = gui::style::make_theme_palette(graphic::colors::ROYAL);
		round_scroll_bar_style->handle_palette = pal;
		round_scroll_bar_style->bar_shape = assets::builtin::get_separator_row_patch();
		round_scroll_bar_style->bar_palette = pal;

		sm.register_style<style::slider1d_drawer>(std::move(round_scroll_bar_style));
	}

	sm.register_style<style::slider2d_drawer>(referenced_ptr<style::default_slider2d_drawer>{std::in_place});

}

ui_outputs build_main_ui(backend::vulkan::context& ctx, scene& scene, loose_group& root){
	make_styles(scene);
	set_cursors(scene);

	scene.set_pass_config({
			{
				fx::scene_render_pass_config::value_type{
					.begin_config = {
						.draw_targets = 0b1,
					},
					.end_config = std::nullopt
				},
				{
					.begin_config = {
						.draw_targets = 0b10,
					},
					.end_config = std::nullopt
				}
			},
			fx::blit_pipeline_config{}
		});

	scene.set_native_communicator<backend::glfw::communicator>(ctx.window().get_handle());
	scene.get_communicator()->set_native_cursor_visibility(false);

	auto e = scene.create<scaling_stack>();
	e->set_fill_parent({true, true});
	auto& mroot = static_cast<scaling_stack&>(root.insert(0, std::move(e)));


	{
		referenced_ptr<style::round_style> round_style{std::in_place};
		round_style->edge.pal = style::pal::white.border;
		round_style->edge = assets::builtin::default_round_square_boarder_thin;
		round_style->back.pal = style::pal::dark;
		round_style->back = assets::builtin::default_round_square_base;

		style::global_default_style_drawer = round_style;
	}

	{
		referenced_ptr<style::round_scroll_bar_style> round_scroll_bar_style{std::in_place};
		round_scroll_bar_style->bar_shape = assets::builtin::get_separator_row_patch();
		round_scroll_bar_style->bar_palette = style::pal::white.border.copy().mul_rgb(.8f);

		style::global_scroll_pane_bar_drawer = round_scroll_bar_style;
	}

	ui_outputs result{};

	auto make_create_table = [&]{
		using function_signature = void(scroll_pane&);
		std::vector<std::function<function_signature>> creators{
				[&](scroll_pane& pane){
					pane.create([&](sequence& sequence){
						sequence.set_expand_policy(layout::expand_policy::prefer);
						sequence.template_cell.set_pending();
						sequence.template_cell.pad = {4, 4};
						sequence.set_has_smooth_pos_animation(false);
						{
							auto hdl = sequence.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
								"Bloom Sample Scale", 50.f);
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(.25f);


							auto& trans = hdl->add_relay_func([](float val){
								return math::lerp(0.25f, 4.f, val);
							});
							hdl->add_formatter_func([](float val){
								return std::format("{:.2f}", val);
							});
							result.shader_bloom_scale = &trans;
						}

						{
							auto hdl = sequence.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
								"BloomSrcFactor", 50.f);
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(.5f);

							auto& trans = hdl->add_relay(react_flow::make_transformer([](float val){
								return math::lerp(0.f, 2.f, val);
							}));
							auto& formatter = hdl->request_embedded_react_node(react_flow::make_transformer(
								[](float val){
									return std::format("{:.2f}", val);
								}));
							react_flow::connect_chain(trans, formatter, hdl->get_display_text_receiver());

							result.shader_bloom_src_factor = &trans;
						}

						{
							auto hdl = sequence.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
								"BloomDstFactor", 50.f);
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(.5f);

							auto& trans = hdl->add_relay_func([](float val){
								return math::lerp(0.f, 2.f, val);
							});
							hdl->add_formatter_func([](float val){
								return std::format("{:.2f}", val);
							});
							result.shader_bloom_dst_factor = &trans;
						}

						{
							auto hdl = sequence.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
								"BloomMixFactor", 50.f);
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(.5f);
							result.shader_bloom_mix_factor = &hdl.elem().get_slider_provider();
						}

						{
							sequence.emplace_back<row_separator>().cell().set_size(8);
						}

						{
							auto hdl = sequence.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
								"HighlightThres", 50.f);
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(.25f);
							auto& trans = hdl->add_relay_func([](float val){
								return math::lerp(0.5f, 2.5f, val);
							});
							hdl->add_formatter_func([](float val){
								return std::format("{:.2f}", val);
							});
							result.highlight_filter_threshold = &trans;
						}

						{
							auto hdl = sequence.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
								"HighlightSmooth", 50.f);
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(.5f);
							hdl->add_formatter_func([](float val){
								return std::format("{:.2f}", val);
							});
							result.highlight_filter_smooth = &hdl.elem().get_slider_provider();
						}

						{
							sequence.emplace_back<row_separator>().cell().set_size(8);
						}

						{
							auto hdl = sequence.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
								"Contrast", 50.f);
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(1.f);
							hdl->add_formatter_func([](float val){
								return std::format("{:.2f}", val);
							});
							result.tonemap_contrast = &hdl->get_slider_provider();
						}
						{
							auto hdl = sequence.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
								"Exposure", 50.f);
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(.5f);
							auto& trans = hdl->add_relay_func([](float val){
								return math::lerp(0.f, 2.f, val);
							});
							hdl->add_formatter_func([](float val){
								return std::format("{:.2f}", val);
							});
							result.tonemap_exposure = &hdl->get_slider_provider();
						}
						{
							auto hdl = sequence.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
								"Saturation", 50.f);
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(1.f);
							hdl->add_formatter_func([](float val){
								return std::format("{:.2f}", val);
							});
							result.tonemap_saturation = &hdl->get_slider_provider();
						}
						{
							auto hdl = sequence.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
								"Gamma", 50.f);
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(math::map(1.2f, 0.5f, 3.f, 0.f, 1.f));
							auto& trans = hdl->add_relay_func([](float val){
								return math::lerp(0.5f, 3.f, val);
							});
							hdl->add_formatter_func([](float val){
								return std::format("{:.2f}", val);
							});
							result.tonemap_gamma = &trans;
						}
					});
				},
				[&](scroll_pane& pane){
					pane.create([&](sequence& sequence){
						sequence.template_cell.set_pad({4, 4});

						auto slider = sequence.emplace_back<gui::slider1d_with_output>();
						slider->set_smooth_scroll(true);
						slider->set_smooth_jump(false);
						slider->set_smooth_drag(true);
						slider.cell().set_size(60);

						auto& progNode = slider->get_provider();
						// node_format.connect_predecessor(progNode);

						// sequence.create_back([&](async_label& label){
						// 	label.set_as_config_prov();
						// 	label.set_dependency(node_layout);
						// 	label.set_text_color_scl(graphic::colors::ACID.to_light_by_luma(1.2));
						// 	// label.set_expand_policy(gui::layout::expand_policy::prefer);
						// }).cell().set_pending();

						// sequence.create_back([&](label& label){
						// 	auto& t = label.request_receiver();
						// 	t.connect_predecessor(node_format);
						// }).cell().set_pending();

						sequence.create_back([&](progress_bar& prog){
							prog.progress.set_state(progress_state::approach_smooth);
							prog.progress.set_speed(.0001f);
							auto& t = prog.request_receiver();
							react_flow::connect_chain(progNode, t);
						}).cell().set_size(60);

						sequence.create_back([&](progress_bar& prog){
							prog.progress.set_state(progress_state::approach_smooth);
							prog.progress.set_speed(.0001f);
							referenced_ptr<style::progress_drawer_arc> drawer{std::in_place};

							drawer->angle_range = {.1f, -.7f};
							drawer->radius = {5, 2};

							prog.drawer = std::move(drawer);
							auto& t = prog.request_receiver();
							react_flow::connect_chain(progNode, t);
						}).cell().set_size(400);

						{
							auto label = sequence.create_back([&](direct_label& l){});
							label.cell().set_pending();

							auto& ln = label->request_react_node<direct_label_text_prov>();
							auto& trans = label->request_embedded_react_node(react_flow::make_transformer([](std::u32string_view sv){
								return typesetting::tokenized_text{sv};
							}));

							sequence.create_back([&](text_edit_prov& area){
								area.set_on_changed_interval(30.f);
							   react_flow::connect_chain(area.get_provider(), trans, ln);
						   }).cell().set_pending();
						}
					});
				},
				[&](scroll_pane& pane){
					pane.set_layout_policy(layout::layout_policy::vert_major);
					pane.create([](sequence& sequence){
						sequence.set_style();
						sequence.set_expand_policy(layout::expand_policy::prefer);
						sequence.template_cell.set_pending();
						sequence.template_cell.set_pad({6.f});

						for(int i = 0; i < 14; ++i){
							sequence.create_back([&](collapser& c){
								c.set_update_opacity_during_expand(true);
								c.set_expand_cond(collapser_expand_cond::inbound);

								c.emplace_head<elem>();
								c.set_head_size(50);
								c.create_body([](table& e){
									e.set_tooltip_state(
										{
											.layout_info = tooltip::align_meta{
												.follow = tooltip::anchor_type::owner,
												.attach_point_spawner = align::pos::top_left,
												.attach_point_tooltip = align::pos::top_right,
											},
										}, [](table& tooltip){
											using namespace gui;
											struct dialog_creator : elem{
												[[nodiscard]] dialog_creator(
													gui::scene& scene, elem* parent)
													: elem(scene, parent){
													interactivity = interactivity_flag::enabled;
												}

												events::op_afterwards on_click(
													const events::click event,
													std::span<elem* const> aboves) override{
													if(event.key.on_release()){
														get_scene().create_overlay({
																.extent = {
																	{
																		layout::size_category::passive,
																		.4f
																	},
																	{
																		layout::size_category::scaling,
																		1.f
																	}
																},
																.align = align::pos::center,
															}, [](table& e){
																e.end_line().emplace_back<elem>();
																e.end_line().emplace_back<elem>();
																e.end_line().emplace_back<elem>();
																e.end_line().emplace_back<elem>();
																e.end_line().emplace_back<elem>();
															});
													}
													return events::op_afterwards::intercepted;
												}
											};
											tooltip.emplace_back<dialog_creator>().cell().set_size({
													160, 60
												});
										});
									e.set_entire_align(align::pos::top_left);
									for(int k = 0; k < 5; ++k){
										e.emplace_back<elem>().cell().set_size({250, 60});
									}
									// e.interactivity = gui::interactivity_flag::enabled;
								});

								c.set_head_body_transpose(i & 1);
							});
						}
					});
				},
				[](scroll_pane& pane){
					pane.create([](menu& menu){
						menu.set_expand_policy(layout::expand_policy::prefer);
						menu.set_head_size(90);
						menu.get_head_template_cell().set_size(240).set_pad({4, 4});

						for(int i = 0; i < 4; ++i){
							auto hdl = menu.create_back(
								[](elem& e){
									e.interactivity = interactivity_flag::enabled;
								}, [&](sequence& e){
									e.set_has_smooth_pos_animation(true);
									e.set_expand_policy(layout::expand_policy::passive);
									e.template_cell.set_pad({4, 4});
									for(int j = 0; j < i + 1; ++j){
										e.emplace_back<elem>();
									}
								});
						}
					});
				},
				[](scroll_pane& pane){
					pane.create([](table& table){
						table.set_expand_policy(layout::expand_policy::prefer);
						table.set_entire_align(align::pos::center);
						for(int i = 0; i < 4; ++i){
							auto check_box = table.emplace_back<gui::check_box>(std::in_place);
							check_box->icons[1].components.color = {graphic::colors::pale_green};
							check_box.cell().set_size({60, 60});

							auto receiver = table.emplace_back<label>();
							receiver->set_fit();
							auto& listener = receiver->request_embedded_react_node(react_flow::make_listener(
								[&e = receiver.elem()](bool i){
									e.set_toggled(i);
									if(i){
										e.set_text("Toggled");
									} else{
										e.set_text("");
									}
								}));
							listener.connect_predecessor(check_box->get_prov());

							receiver.cell().set_end_line();
						}

						auto sep = table.emplace_back<row_separator>();
						sep.cell().set_height(20).set_width_passive(.85f).saturate = true;
						sep.cell().margin.set_vert(4);
						sep.cell().set_end_line();

						for(int i = 0; i < 4; ++i){
							table.emplace_back<elem>().cell().set_size({120, 120});
							table.emplace_back<elem>();
							table.end_line();
						}
					});
				},
				[](scroll_pane& pane){
					pane.set_layout_policy(layout::layout_policy::vert_major);
					pane.create(
						[](grid& table){
							table.set_has_smooth_pos_animation(true);
							table.set_expand_policy(layout::expand_policy::prefer);
							table.emplace_back<elem>().cell().extent = {
									{.type = grid_extent_type::src_extent, .desc = {0, 2},},
									{.type = grid_extent_type::src_extent, .desc = {0, 1},},
								};
							table.emplace_back<elem>().cell().extent = {
									{.type = grid_extent_type::src_extent, .desc = {1, 2},},
									{.type = grid_extent_type::src_extent, .desc = {1, 1},},
								};
							table.emplace_back<elem>().cell().extent = {
									{.type = grid_extent_type::src_extent, .desc = {2, 2},},
									{.type = grid_extent_type::src_extent, .desc = {2, 1},},
								};
							table.emplace_back<elem>().cell().extent = {
									{.type = grid_extent_type::src_extent, .desc = {0, 4},},
									{.type = grid_extent_type::src_extent, .desc = {2, 2},},
								};
							table.emplace_back<elem>().cell().extent = {
									{.type = grid_extent_type::margin, .desc = {1, 1},},
									{.type = grid_extent_type::src_extent, .desc = {5, 1},},
								};
							table.emplace_back<elem>().cell().extent = {
									{.type = grid_extent_type::margin, .desc = {4, 1},},
									{.type = grid_extent_type::src_extent, .desc = {7, 1},},
								};
							table.emplace_back<elem>().cell().extent = {
									{.type = grid_extent_type::src_extent, .desc = {5, 6},},
									{.type = grid_extent_type::margin, .desc = {0, 0},},
								};
						}, math::vector2<grid_dim_spec>{
							grid_uniformed_mastering{6, 300.f, {4, 4}},
							grid_uniformed_passive{8, {4, 4}}
						});
				},
				[](scroll_pane& pane){
					pane.set_layout_policy(layout::layout_policy::vert_major);
					pane.create(
						[](split_pane& table){
							constexpr static auto test_text =
								R"({s:*.5}Basic{size:64} Token {size:128}Test{//}
{u}AVasdfdjknfhvbawhboozx{/}cgiuTeWaVoT.P.àáâã ä åx̂̃ñ
{color:#FF0000}Red Text{/} and {font:gui}Font Change{/}

Escapes Test:
1. Backslash: \\ {_}(Should see single backslash){/}
2. Braces {size:128}with{/} slash: \{ and \} (Should see literal { and })
3. Braces with double: {{ and }} (Should see literal { and })

Line Continuation Test:
This is a very long line that \
{font:gui}should be joined together{/} \
without newlines.

{feature:liga}0 ff {feature:-liga}1 ff {feature:liga} 2 ff{feature} 3 ff{feature} 4 ff

O{ftr:liga}off file flaff{/} ff

Edge Cases:
1. Token without arg: {bold}Bold Text{/bold}
2. {u}Unclosed brace{/}: { This is just text because no closing bracket
3. Unknown escape: \z (Should show 'z')
4. Colon in arg: {log:Time:12:00} (Name="log", Arg="Time:12:00")
)";

							table.set_expand_policy(layout::expand_policy::prefer);
							using namespace std::literals;
							table.create_head([](split_pane& inner){
								inner.set_expand_policy(layout::expand_policy::passive);
								inner.set_layout_policy(layout::layout_policy::hori_major);
								inner.create_head([](scroll_pane& p){
									p.set_style();
									p.create([](label& l){
										l.set_tokenizer_tag(typesetting::tokenize_tag::raw);
										l.set_expand_policy(layout::expand_policy::prefer);
										// l.text_line_align = typesetting::content_alignment::justify;
										l.set_fit(false);
										l.set_typesetting_config(typesetting::layout_config{
												.throughout_scale = 1.75f
											});
										l.set_text(test_text);
									});
								});
								inner.create_body([](scroll_pane& label){
									label.set_overlay_bar(true);
									label.set_layout_policy(layout::layout_policy::vert_major);
									// label.set_style();
									label.create([&](label_v2& l){
										l.set_expand_policy(layout::expand_policy::prefer);
										l.set_fit(false);
										l.set_typesetting_config(typesetting::layout_config{
												.direction = typesetting::layout_direction::ttb
											});
										l.set_text(test_text);
									});
								});
							});


							table.create_body([](split_pane& inner){
								inner.set_expand_policy(layout::expand_policy::passive);
								inner.set_layout_policy(layout::layout_policy::hori_major);
								inner.create_head([](scroll_pane& label){
									label.set_style();
									label.create([](label_v2& l){
										l.set_expand_policy(layout::expand_policy::prefer);
										l.set_fit(false);
										l.set_typesetting_config(typesetting::layout_config{
												.direction = typesetting::layout_direction::rtl,
												.throughout_scale = 1.75f
											});
										l.set_text(test_text);
									});
								});
								inner.create_body([](scroll_pane& label){
									label.set_layout_policy(layout::layout_policy::vert_major);
									label.set_overlay_bar(true);
									label.set_style();
									label.create([&](label_v2& l){
										l.set_expand_policy(layout::expand_policy::prefer);
										l.set_fit(false);
										l.set_typesetting_config(typesetting::layout_config{
												.direction = typesetting::layout_direction::btt
											});
										l.set_text(test_text);
									});
								});
							});
						});
				},
				[](scroll_pane& pane){
					pane.set_layout_policy(layout::layout_policy::none);
					pane.create(
						[](table& table){
							table.set_expand_policy(layout::expand_policy::prefer);
							table.create_back([](cpd::rgb_picker& picker){
							}).cell().set_size({600, 600});
						});
				},
				[](scroll_pane& pane){
					pane.set_layout_policy(layout::layout_policy::none);
					pane.create(
						[](sequence& table){
							table.set_expand_policy(layout::expand_policy::prefer);
							table.emplace_back<vp>();
						});
				}
			};

		return creators;
	};


	root.set_style();
	mroot.set_style();

	const auto menu_hdl = mroot.emplace_back<menu>();
	menu_hdl->set_layout_policy(layout::layout_policy::vert_major);
	menu_hdl->set_expand_policy(layout::expand_policy::passive);
	menu_hdl->set_head_size({layout::size_category::mastering, 256});
	menu_hdl.cell().region_scale = {.0f, .0f, .8f, 1.f};
	menu_hdl.cell().align = align::pos::left;

	menu_hdl->get_head_template_cell().set_pending();
	menu_hdl->get_head_template_cell().set_pad({4, 4});

	for(const auto& [idx, creator] : make_create_table() | std::views::enumerate){
		menu_hdl->create_back([&](label& label){
				label.set_text(std::format("Test: {}", idx));
				label.text_entire_align = align::pos::center;
				label.interactivity = interactivity_flag::enabled;
			label.set_transform_config({
				.rotation = text_rotation::deg_270
			});
			}, [&](scroll_pane& scroll_pane){
				creator(scroll_pane);
			});
	}

	return result;
}
}
