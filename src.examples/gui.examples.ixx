//
// Created by Matrix on 2025/11/19.
//

export module mo_yanxi.gui.examples;



import mo_yanxi.gui.infrastructure;
import mo_yanxi.gui.elem.group;
import mo_yanxi.gui.global;

import mo_yanxi.react_flow;
import mo_yanxi.react_flow.common;

import mo_yanxi.gui.elem.manual_table;
import mo_yanxi.gui.elem.sequence;
import mo_yanxi.gui.elem.scroll_pane;
import mo_yanxi.gui.elem.collapser;
import mo_yanxi.gui.elem.table;
import mo_yanxi.gui.elem.grid;
import mo_yanxi.gui.elem.menu;
import mo_yanxi.gui.elem.label;
import mo_yanxi.gui.elem.slider;
import mo_yanxi.gui.elem.progress_bar;
import mo_yanxi.gui.elem.image_frame;
import mo_yanxi.gui.elem.text_edit;
import mo_yanxi.gui.elem.image_frame;
import mo_yanxi.gui.elem.drag_split;
import mo_yanxi.gui.elem.label_v2;

import mo_yanxi.gui.assets.manager;

import mo_yanxi.backend.communicator;
import mo_yanxi.backend.vulkan.context;

import std;

namespace mo_yanxi::gui::example{


export
void build_main_ui(backend::vulkan::context& ctx, scene& scene, loose_group& root){
	scene.set_pass_config({
		gfx_config::scene_render_pass_config::value_type{
			.begin_config = {
				.mode = draw_mode::msdf,
				.draw_targets = 0b1,
			},
			.end_config = {}
		},
		{
			.begin_config = {
				.mode = draw_mode::msdf,
				.draw_targets = 0b10,
			},
			.end_config = {}
		}
	});

	scene.set_native_communicator<backend::glfw::communicator>(ctx.window().get_handle());
	scene.get_communicator()->set_native_cursor_visibility(false);

	auto e = scene.create<scaling_stack>();
	e->set_fill_parent({true, true});
	auto& mroot = static_cast<scaling_stack&>(root.insert(0, std::move(e)));

	/*auto& node_layout = scene.request_independent_react_node<label_layout_node>();
	auto& node_format = scene.request_independent_react_node<format_node>();
	auto& node_stoint = scene.request_independent_react_node<react_flow::string_to_arth<int, std::string_view>>();
	auto& node_proj_x = scene.request_independent_react_node(react_flow::make_transformer(react_flow::async_type::none, [](math::vec2 v){
		return v.x;
	}));

	auto make_create_table = [&]{
		using function_signature = void(scroll_pane&);
		std::vector<std::function<function_signature>> creators{
				[&](scroll_pane& pane){
					pane.create([&](sequence& sequence){
						sequence.template_cell.set_pad({4, 4});

						auto slider = sequence.emplace_back<gui::slider>();
						slider->set_smooth_scroll(true);
						slider->set_smooth_jump(true);
						slider->set_smooth_drag(true);
						slider->set_hori_only();
						slider.cell().set_size(60);

						auto& progNode = slider->request_react_node();
						node_format.connect_predecessor(progNode);

						sequence.create_back([&](async_label& label){
							label.set_as_config_prov();
							label.set_dependency(node_layout);
							label.set_text_color_scl(graphic::colors::ACID.to_light_by_luma(1.2));
							// label.set_expand_policy(gui::layout::expand_policy::prefer);
						}).cell().set_pending();

						sequence.create_back([&](label& label){
							auto& t = label.request_receiver();
							t.connect_predecessor(node_format);
						}).cell().set_pending();

						sequence.create_back([&](progress_bar& prog){
							prog.progress.set_state(progress_state::approach_scaled);
							prog.progress.set_speed(.0001f);
							auto& t = prog.request_receiver();
							react_flow::connect_chain({&progNode, &node_proj_x, &t});
						}).cell().set_size(60);

						sequence.create_back([&](text_edit& area){
							auto& nd = area.set_as_string_prov();
							react_flow::connect_chain({&nd, &node_format, &node_layout});
							react_flow::connect_chain({&nd, &node_stoint, &node_format});
						}).cell().set_pending();
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
							auto check_box = table.emplace_back<gui::check_box>();
							check_box.cell().set_size({60, 60});
							check_box->set_drawable<drawable_image<>>(1,
							                                          assets::builtin::get_page()[
								                                          assets::builtin::icon::check].
							                                          value_or({}));

							auto receiver = table.emplace_back<label>();
							receiver->set_fit();
							auto& listener = receiver->request_react_node(react_flow::make_listener(
								[&e = receiver.elem()](std::size_t i){
									e.set_toggled(i);
									if(i){
										e.set_text("Toggled");
									} else{
										e.set_text("");
									}
								}));
							listener.connect_predecessor(check_box->request_provider());

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
						[](drag_split& table){
							constexpr static auto test_text =
R"({s:*.5}Basic{size:64} Token {size:128}Test{//}
{u}AVasdfdjknfhvbawhboozx{/}cgiuTeWaVoT.P.àáâãäåx̂̃ñ
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
							table.create_head([](drag_split& inner){
								inner.set_expand_policy(layout::expand_policy::passive);
								inner.set_layout_policy(layout::layout_policy::hori_major);
								inner.create_head([](scroll_pane& label){
									label.set_style();
									label.create([](label_v2& l){
										l.set_expand_policy(layout::expand_policy::prefer);
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
									label.set_style();
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


							table.create_body([](drag_split& inner){
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
	menu_hdl.cell().region_scale = {.0f, .0f, 1.f, 1.f};
	menu_hdl.cell().align = align::pos::center;

	menu_hdl->get_head_template_cell().set_pending();
	menu_hdl->get_head_template_cell().set_pad({4, 4});

	for (const auto & [idx, creator] : make_create_table() | std::views::enumerate){
		menu_hdl->create_back([&](label& label){
			label.set_text(std::format("Test: {}", idx));
			label.text_entire_align = align::pos::center;
			label.interactivity = interactivity_flag::enabled;
		}, [&](scroll_pane& scroll_pane){
			creator(scroll_pane);
		});
	}
	*/

}
}
