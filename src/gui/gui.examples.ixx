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

import mo_yanxi.gui.assets.manager;

import mo_yanxi.backend.communicator;
import mo_yanxi.backend.vulkan.context;

import std;

struct format_node : mo_yanxi::react_flow::modifier_transient<std::string, mo_yanxi::react_flow::stoa_result<int>, std::string_view, mo_yanxi::math::vec2>{
	[[nodiscard]] format_node() = default;

protected:
	std::optional<std::string> operator()(
		const std::stop_token& stop_token,
		const mo_yanxi::react_flow::stoa_result<int>& arth,
		const std::string_view& p1,
		const mo_yanxi::math::vector2<float>& p2) override{
		return std::format("T2A:{}, {}/{}", to_string(arth), p1, p2);
	}
};

namespace mo_yanxi::gui::example{

export
void build_main_ui(backend::vulkan::context& ctx, gui::scene& scene, gui::loose_group& root){
	auto e = scene.create<gui::scaling_stack>();

	scene.set_native_communicator<backend::glfw::communicator>(ctx.window().get_handle());

	e->set_fill_parent({true, true});
	auto& mroot = static_cast<gui::scaling_stack&>(root.insert(0, std::move(e)));

	auto& node_layout = scene.request_independent_react_node<gui::label_layout_node>();
	auto& node_format = scene.request_independent_react_node<format_node>();
	auto& node_stoint = scene.request_independent_react_node<react_flow::string_to_arth<int, std::string_view>>();
	auto& node_proj_x = scene.request_independent_react_node(react_flow::make_transformer(react_flow::async_type::none, [](math::vec2 v){
		return v.x;
	}));

	// mo_yanxi::react_flow::connect_chain({&console_input, &formatnode});

	auto hdl = mroot.create_back([&](gui::scroll_pane& scroll_pane){
		scroll_pane.create([&](gui::sequence& sequence){
			sequence.template_cell.set_pad({4, 4});

			auto slider = sequence.emplace_back<gui::slider>();
			slider->set_smooth_scroll(true);
			slider->set_smooth_jump(true);
			slider->set_smooth_drag(true);
			slider->set_hori_only();
			slider.cell().set_size(60);

			auto& progNode = slider->request_react_node();
			node_format.connect_predecessor(progNode);

			sequence.create_back([&](gui::async_label& label){
				label.set_as_config_prov();
				label.set_dependency(node_layout);
				label.set_text_color_scl(graphic::colors::ACID.to_light_by_luma(1.2));
				// label.set_expand_policy(gui::layout::expand_policy::prefer);
			}).cell().set_pending();

			sequence.create_back([&](gui::label& label){
				auto& t = label.request_receiver();
				t.connect_predecessor(node_format);
			}).cell().set_pending();

			sequence.create_back([&](gui::progress_bar& prog){
				prog.progress.set_state(gui::progress_state::approach_scaled);
				prog.progress.set_speed(.0001f);
				auto& t = prog.request_receiver();
				react_flow::connect_chain({&progNode, &node_proj_x, &t});
			}).cell().set_size(60);

			sequence.create_back([&](gui::text_edit& area){
				auto& nd = area.set_as_string_prov();
				react_flow::connect_chain({&nd, &node_format, &node_layout});
				react_flow::connect_chain({&nd, &node_stoint, &node_format});

			}).cell().set_pending();
		});
	});


	//
	// auto hdl = mroot.create_back([&](mo_yanxi::gui::async_label& label){
	// 	label.set_dependency(layoutnode);
	// });

	hdl.cell().region_scale = {.0f, .0f, .4f, 1.f};
	hdl.cell().align = gui::align::pos::bottom_right;

	{
		auto hdl = mroot.emplace_back<gui::scroll_pane>(gui::layout::layout_policy::vert_major);
		hdl.cell().region_scale = {.0f, .0f, .4f, 1.f};
		hdl.cell().align = gui::align::pos::bottom_left;

		hdl->create([](gui::sequence& sequence){
			sequence.set_style();
			sequence.set_expand_policy(gui::layout::expand_policy::prefer);
			sequence.template_cell.set_pending();
			sequence.template_cell.set_pad({6.f});

			for(int i = 0; i < 14; ++i){
				sequence.create_back([&](gui::collapser& c){
					c.set_update_opacity_during_expand(true);
					c.set_expand_cond(gui::collapser_expand_cond::inbound);

					c.emplace_head<gui::elem>();
					c.set_head_size(50);
					c.create_content([](gui::table& e){
						e.set_tooltip_state({
							.layout_info = gui::tooltip::align_meta{
								.follow = gui::tooltip::anchor_type::owner,
								.attach_point_spawner = align::pos::top_left,
								.attach_point_tooltip = align::pos::top_right,
							},
						}, [](gui::table& tooltip){
							using namespace gui;
							struct dialog_creator : elem{
								[[nodiscard]] dialog_creator(gui::scene& scene, elem* parent)
								: elem(scene, parent){
									interactivity = interactivity_flag::enabled;
								}

								events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override{
									if(event.key.on_release()){
										get_scene().create_overlay({
												.extent = {
													{layout::size_category::passive, .4f},
													{layout::size_category::scaling, 1.f}
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
							tooltip.emplace_back<dialog_creator>().cell().set_size({160, 60});
						});
						e.set_entire_align(align::pos::top_left);
						for(int k = 0; k < 5; ++k){
							e.emplace_back<gui::elem>().cell().set_size({250, 60});
						}
						// e.interactivity = gui::interactivity_flag::enabled;
					});

					c.set_head_body_transpose(i & 1);

				});
			}
		});
	}

	/*{
		auto hdl = mroot.create_back([](mo_yanxi::gui::menu& menu){
			// menu.set_layout_policy(gui::layout::layout_policy::vert_major);
			menu.set_head_size(90);
			menu.get_head_template_cell().set_size(240).set_pad({4, 4});

			for(int i = 0; i < 4; ++i){
				auto hdl = menu.create_back([](mo_yanxi::gui::elem& e){

					e.interactivity = gui::interactivity_flag::enabled;
				}, [&](mo_yanxi::gui::sequence& e){
					e.set_has_smooth_pos_animation(true);
					e.set_expand_policy(gui::layout::expand_policy::passive);
					e.template_cell.set_pad({4, 4});
					for(int j = 0; j < i + 1; ++j){
						e.emplace_back<gui::elem>();
					}
				});
			}
		});
		hdl->set_expand_policy(gui::layout::expand_policy::passive);
		hdl.cell().region_scale = {.0f, .0f, .6f, 1.f};
		hdl.cell().align = gui::align::pos::bottom_right;
		//
		// hdl->create([](gui::table& table){
		// 	table.set_expand_policy(gui::layout::expand_policy::prefer);
		// 	table.set_entire_align(align::pos::center);
		// 	for(int i = 0; i < 4; ++i){
		// 		table.emplace_back<gui::elem>().cell().set_size({60, 120});
		// 		table.emplace_back<gui::elem>();
		// 		table.end_line();
		// 	}
		// 	table.emplace_back<gui::elem>().cell().set_height(40).set_width_passive(.85f).saturate = true;
		// 	//.align = align::pos::center;
		// 	// table.emplace_back<gui::elem>();
		// 	table.end_line();
		//
		// 	for(int i = 0; i < 4; ++i){
		// 		table.emplace_back<gui::elem>().cell().set_size({120, 120});
		// 		table.emplace_back<gui::elem>();
		// 		table.end_line();
		// 	}
		// });
	}*/

	/*{
		auto hdl = mroot.emplace_back<mo_yanxi::gui::scroll_pane>();
		hdl.cell().region_scale = {.0f, .0f, .4f, 1.f};
		hdl.cell().align = gui::align::pos::bottom_left;

		hdl->create([](gui::table& table){
			table.set_expand_policy(gui::layout::expand_policy::prefer);
			table.set_entire_align(align::pos::center);
			for(int i = 0; i < 4; ++i){
				{
					auto check_box = table.emplace_back<gui::check_box>();
					check_box.cell().set_size({60, 60});
					check_box->set_drawable<gui::drawable_image<>>(1,
						gui::assets::builtin::get_page()[gui::assets::builtin::icon::check].value());

					auto receiver = table.emplace_back<gui::label>();
					receiver->set_fit();
					auto& listener = receiver->request_react_node(react_flow::make_listener(
						[&e = receiver.elem()](std::size_t i){
							e.set_toggled(i);
							if(i){
								e.set_text("Toggled");
							}else{
								e.set_text("");
							}
						}));
					listener.connect_predecessor(check_box->request_provider());
				}

				table.end_line();
			}
			table.end_line();

			{
				auto sep = table.emplace_back<mo_yanxi::gui::row_separator>();
				sep.cell().set_height(20).set_width_passive(.85f).saturate = true;
				sep.cell().margin.set_vert(4);
			}
			//.align = align::pos::center;
			// table.emplace_back<gui::elem>();
			table.end_line();

			for(int i = 0; i < 4; ++i){
				table.emplace_back<gui::elem>().cell().set_size({120, 120});
				table.emplace_back<gui::elem>();
				table.end_line();
			}
		});

	}*/
	/*{
		auto hdl = mroot.emplace_back<mo_yanxi::gui::scroll_pane>();
		hdl.cell().region_scale = {.0f, .0f, .6f, 1.f};
		hdl.cell().align = gui::align::pos::bottom_right;
		hdl->set_layout_policy(gui::layout::layout_policy::vert_major);



		hdl->create([](mo_yanxi::gui::grid& table){
			table.set_expand_policy(gui::layout::expand_policy::prefer);
			table.emplace_back<gui::elem>().cell().extent = {
					{.type = gui::grid_extent_type::src_extent, .desc = {0, 2},},
					{.type = gui::grid_extent_type::src_extent, .desc = {0, 1},},
				};
			table.emplace_back<gui::elem>().cell().extent = {
					{.type = gui::grid_extent_type::src_extent, .desc = {1, 2},},
					{.type = gui::grid_extent_type::src_extent, .desc = {1, 1},},
				};
			table.emplace_back<gui::elem>().cell().extent = {
					{.type = gui::grid_extent_type::src_extent, .desc = {2, 2},},
					{.type = gui::grid_extent_type::src_extent, .desc = {2, 1},},
				};
			table.emplace_back<gui::elem>().cell().extent = {
					{.type = gui::grid_extent_type::src_extent, .desc = {0, 4},},
					{.type = gui::grid_extent_type::src_extent, .desc = {2, 2},},
				};
			table.emplace_back<gui::elem>().cell().extent = {
					{.type = gui::grid_extent_type::margin, .desc = {1, 1},},
					{.type = gui::grid_extent_type::src_extent, .desc = {5, 1},},
				};
			table.emplace_back<gui::elem>().cell().extent = {
					{.type = gui::grid_extent_type::margin, .desc = {4, 1},},
					{.type = gui::grid_extent_type::src_extent, .desc = {7, 1},},
				};
			table.emplace_back<gui::elem>().cell().extent = {
					{.type = gui::grid_extent_type::src_extent, .desc = {0, 5},},
					{.type = gui::grid_extent_type::margin, .desc = {0, 0},},
				};
		}, math::vector2<mo_yanxi::gui::grid_dim_spec>{
			mo_yanxi::gui::grid_uniformed_mastering{6, 300.f, {4, 4}},
			mo_yanxi::gui::grid_uniformed_passive{8, {4, 4}}
		});

	}*/


}
}
