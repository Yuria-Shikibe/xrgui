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
import mo_yanxi.gui.elem.overflow_sequence;
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
import mo_yanxi.gui.elem.text_edit_v2;
import mo_yanxi.gui.elem.viewport;
import mo_yanxi.gui.elem.check_box;
import mo_yanxi.gui.elem.double_side;

import mo_yanxi.gui.infrastructure;
import mo_yanxi.font;

import mo_yanxi.typesetting.util;
import mo_yanxi.font;
import mo_yanxi.font.manager;
import mo_yanxi.graphic.color;
import mo_yanxi.graphic.msdf;
import mo_yanxi.heterogeneous.open_addr_hash;
import align;

import mo_yanxi.typesetting;
import mo_yanxi.graphic.draw.instruction.recorder;


import mo_yanxi.gui.compound.color_picker;
import mo_yanxi.gui.compound.named_slider;
import mo_yanxi.gui.compound.file_selector;
import mo_yanxi.gui.compound.data_table;

import mo_yanxi.gui.style.round_square;
import mo_yanxi.gui.style.progress_bars;
import mo_yanxi.gui.style.palette;

import mo_yanxi.gui.assets.manager;

import mo_yanxi.backend.communicator;
import mo_yanxi.backend.vulkan.context;


namespace mo_yanxi::gui::example{

struct test_entry{
	std::string name;
	std::function<elem_ptr(scene&, elem*)> creator;

	[[nodiscard]] test_entry(const std::string& name, const std::function<elem_ptr(scene&, elem*)>& creator)
		: name(name),
		  creator(creator){
	}

	template <invocable_elem_init_func Fn>
	[[nodiscard]] test_entry(const std::string& name, Fn&& fn)
		: name(name),
		  creator([f = std::forward<Fn>(fn)](scene& s, elem* p){
			  return elem_ptr{s, p, f};
		  }){
	}
};

struct image_cursor : style::cursor{

	gui::image_region_borrow icon_region;

	[[nodiscard]] explicit image_cursor(const gui::image_region_borrow& icon_region)
		: icon_region(icon_region){
	}

	rect draw(gui::renderer_frontend& renderer, math::raw_frect region,
		std::span<const elem* const> inbound_stack) const override{
		region.src -= region.extent * .5f;

		region.expand({mo_yanxi::graphic::msdf::sdf_image_boarder + 4, mo_yanxi::graphic::msdf::sdf_image_boarder + 4});
		state_guard g{renderer, gui::fx::batch_draw_mode::msdf};
		renderer << graphic::draw::instruction::rect_aabb{
				.generic = {icon_region->view},
				.v00 = region.vert_00(),
				.v11 = region.vert_11(),
				.uv00 = icon_region->uv.v00(),
				.uv11 = icon_region->uv.v11(),
				.vert_color = {graphic::colors::white}
			};

		return {tags::from_extent, region.src, region.extent};
	}
};




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
	auto& cm = scene.resources().cursor_collection_manager;

	cm.add_cursor<assets::builtin::cursor::default_cursor_regular>(style::cursor_type::regular);
	cm.add_cursor<assets::builtin::cursor::default_cursor_drag>(style::cursor_type::drag);
	cm.add_cursor<image_cursor>(style::cursor_type::textarea, assets::builtin::get_page()[assets::builtin::shape_id::textarea].value_or({}));

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
	auto& sm = scene.resources().style_manager;


	{
		referenced_ptr<style::round_style> round_style{std::in_place};
		round_style->edge.pal = style::pal::white.border;
		round_style->edge = assets::builtin::default_round_square_boarder_thin;
		round_style->back.pal = style::pal::dark;
		round_style->back = assets::builtin::default_round_square_base;

		sm.register_style<style::elem_style_drawer>(round_style);
	}


	auto elem_slice = sm.get_slice<style::elem_style_drawer>().value();

	{
		referenced_ptr<style::round_style_no_edge> round_style{std::in_place};
		round_style->boarder.set(4);
		round_style->back.pal = style::pal::dark;
		round_style->back = assets::builtin::default_round_square_base;

		elem_slice.insert_or_assign("round_base_only", round_style);
	}

	{
		referenced_ptr<style::round_style_edge_only> round_style{std::in_place};
		round_style->edge.pal = style::pal::white.border;
		round_style->edge = assets::builtin::default_round_square_boarder_thin;

		elem_slice.insert_or_assign("round_edge_only", round_style);
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

	{
		style::side_bar_style style{};
		const auto region = assets::builtin::get_page()[assets::builtin::shape_id::side_bar].value_or({});
		style.boarder.set(2);
		style.bar.pal = style::pal::white.border;
		style.bar = image_row_patch{region, region->uv.get_region(), 18, 18, 4};
		style.back.pal = style::pal::dark;
		style.back = assets::builtin::default_round_square_base;


		{
			auto s = style;
			s.boarder.left = 16;
			s.pos = style::side_bar_pos::left;
			elem_slice.insert_or_assign("side_bar_left", referenced_ptr<style::side_bar_style>{std::in_place, s});
		}
		{
			auto s = style;
			s.boarder.right = 16;
			s.pos = style::side_bar_pos::right;
			elem_slice.insert_or_assign("side_bar_right", referenced_ptr<style::side_bar_style>{std::in_place, s});
		}
		{
			auto s = style;
			s.boarder.top = 16;
			s.pos = style::side_bar_pos::top;
			elem_slice.insert_or_assign("side_bar_top", referenced_ptr<style::side_bar_style>{std::in_place, s});
		}
		{
			auto s = style;
			s.boarder.bottom = 16;
			s.pos = style::side_bar_pos::bottom;
			elem_slice.insert_or_assign("side_bar_bottom", referenced_ptr<style::side_bar_style>{std::in_place, s});
		}
	}

	sm.register_style<style::slider2d_drawer>(referenced_ptr<style::default_slider2d_drawer>{std::in_place});

}


ui_outputs build_main_ui(backend::vulkan::context& ctx, scene& scene, loose_group& root){
	make_styles(scene);
	set_cursors(scene);

	scene.resources().pass_config = {
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
		};

	scene.set_native_communicator<backend::glfw::communicator>(ctx.window().get_handle());
	scene.get_communicator()->set_native_cursor_visibility(false);

	auto e = scene.create<scaling_stack>();
	e->set_fill_parent({true, true});
	auto& mroot = static_cast<scaling_stack&>(root.insert(0, std::move(e)));


	{
		referenced_ptr<style::round_scroll_bar_style> round_scroll_bar_style{std::in_place};
		round_scroll_bar_style->bar_shape = assets::builtin::get_separator_row_patch();
		round_scroll_bar_style->bar_palette = style::pal::white.border.copy().mul_rgb(.8f);

		style::global_scroll_pane_bar_drawer = round_scroll_bar_style;
	}

	ui_outputs result{};

	auto make_create_table = [&] -> std::vector<test_entry> {
		using function_signature = void(scroll_pane&);
		std::vector<test_entry> tests{
			test_entry{"csv", [](cpd::data_table& table){
				table._debug_identity = 114;
				table.get_item() = cpd::data_table_desc::from_csv(LR"(D:\projects\untitled\shader info.csv)");
				table.notify_isolated_layout_changed();
			}},
			test_entry{"sliders", [&](scroll_pane& pane){
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
				}}
		};
		std::vector<std::function<function_signature>> creators{

				[&](scroll_pane& pane){
					pane.create([&](sequence& sequence){
						sequence.template_cell.set_pad({4, 4});

						auto slider = sequence.emplace_back<gui::slider1d_with_output>();
						slider->set_smooth_scroll(true);
						slider->set_smooth_jump(false);
						slider->set_smooth_drag(true);
						slider.cell().set_size(60);

						auto& progNode = slider->get_provider();

						sequence.create_back([&](progress_bar& prog){
							prog.progress.set_state(progress_state::approach_smooth);
							prog.progress.set_speed(.0001f);
							auto& t = prog.request_receiver();
							react_flow::connect_chain(progNode, t);
						}).cell().set_size(60);

						sequence.create_back([&](progress_bar& prog){
							prog.progress.set_state(progress_state::approach_smooth);
							prog.progress.set_speed(.0001f);
							referenced_ptr<style::ring_progress> drawer{std::in_place};
							drawer->thickness = 32;
							prog.draw_config.color = {graphic::colors::white, graphic::colors::white};


							prog.set_self_boarder(gui::boarder{}.set(32));
							prog.drawer = std::move(drawer);
							prog.set_progress_state(progress_state::rough);
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

						struct ds : double_side<2>{
							[[nodiscard]] ds(gui::scene& scene, elem* parent)
								: double_side<2>(scene, parent){
								interactivity = interactivity_flag::intercept;
								set_expand_policy(layout::expand_policy::passive);
								set_style();

								create(0, [](gui::label& edit){
									edit.set_expand_policy(layout::expand_policy::passive);
									edit.set_text("OnLabel");
									edit.set_fit();
								});

								create(1, [](gui::text_edit& edit){
									edit.set_expand_policy(layout::expand_policy::passive);
									// edit.set_fit(true);
									edit.set_view_type(text_edit_view_type::dyn);
								});
							}

							void on_last_clicked_changed(bool isFocused) override{
								if(isFocused){
									switch_to(1);
									at<text_edit>(1).on_last_clicked_changed(true);
								}else{
									at<text_edit>(1).on_last_clicked_changed(false);
									switch_to(0);
								}
							}

							events::op_afterwards on_click(events::click event, std::span<elem* const> aboves) override{
								elem::on_click(event, aboves);
								if(get_current_active_index() == 1){
									auto& e = at<text_edit>(1);
									auto pos = e.transform_to_content_space(event.pos);
									event.pos = pos;
									e.on_click(event, {});
								}
								return events::op_afterwards::intercepted;
							}

							events::op_afterwards on_drag(events::drag event) override{
								auto& e = get_current_active();
								event.src = e.transform_to_content_space(event.src);
								event.dst = e.transform_to_content_space(event.dst);
								return e.on_drag(event);
							}

							gui::style::cursor_style get_cursor_type(math::vec2 cursor_pos_at_content_local) const noexcept override{
								auto& e = get_current_active();
								return e.get_cursor_type(e.transform_to_content_space(cursor_pos_at_content_local));
							}
						};

						sequence.create_back([&](ds& area){


						}).cell().set_size(160);
					});
				},
				[&](scroll_pane& pane){
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
					}, layout::layout_policy::vert_major);
					pane.set_layout_policy(layout::layout_policy::vert_major);
				},
				[&](scroll_pane& pane){
					auto& s = pane.emplace<gui::cpd::file_selector>();
					s.set_multiple_selection(true);
					auto& n = s.request_embedded_react_node(react_flow::make_listener([](std::span<const std::filesystem::path> paths){
						std::println(std::cerr, "{::?}", paths | std::views::transform([](const std::filesystem::path& p){return p.string();}));
					}));
					n.connect_predecessor(s.get_prov());
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

						{
							auto sep = table.emplace_back<row_separator>();
						   sep.cell().set_height(20).set_width_passive(.85f).saturate = true;
						   sep.cell().margin.set_vert(4);
						   sep.cell().set_end_line();
						}

						{
							auto sep = table.create_back([](overflow_sequence& seq){
								seq.set_layout_policy(layout::layout_policy::vert_major);
								seq.template_cell.set_size(120).set_pad({2, 2});
								auto [_, cell] = seq.create_overflow_elem([](icon_frame& i){
									i.set_style();
								}, gui::assets::builtin::shape_id::more);
								cell.set_size({layout::size_category::scaling});

								for(unsigned i = 0; i < 12; ++i){
									seq.create_back([&](label& l){
										l.set_text(std::format("{}", i));
										l.set_fit();
									});
								}
								seq.set_split_index(2);
							});
							sep.cell().set_height(80).saturate = true;
							sep.cell().margin.set_vert(4);
							sep.cell().set_end_line();
						}

						for(int i = 0; i < 4; ++i){
							table.emplace_back<elem>().cell().set_size({120, 120});
							table.emplace_back<elem>();
							table.end_line();
						}
					});
				},
				[](scroll_pane& pane){
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
					pane.set_layout_policy(layout::layout_policy::vert_major);
				},
				[](scroll_pane& pane){
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
								inner.create_head([](scroll_adaptor<label>& p){
									p.set_style();
									auto& l = p.get_elem();
									l.set_tokenizer_tag(typesetting::tokenize_tag::raw);
									l.set_expand_policy(layout::expand_policy::prefer);
									l.set_fit(false);
									l.set_text(test_text);

								});
								inner.create_body([](scroll_adaptor<label>& p){
									p.set_overlay_bar(true);
									// label.set_style();
									auto& l = p.get_elem();
									l.set_tokenizer_tag(typesetting::tokenize_tag::raw);
									l.set_expand_policy(layout::expand_policy::prefer);
									l.set_fit(false);
									l.set_text(test_text);
									p.set_layout_policy(layout::layout_policy::vert_major);
								});
							});


							table.create_body([](split_pane& inner){
								inner.set_expand_policy(layout::expand_policy::passive);
								inner.set_layout_policy(layout::layout_policy::hori_major);
								inner.create_head([](scroll_pane& label){
									label.set_style();
									label.create([](gui::label& l){
										l.set_expand_policy(layout::expand_policy::prefer);
										l.set_fit(false);
										l.set_typesetting_config(typesetting::layout_config{
												.direction = typesetting::layout_direction::rtl,
											});
										l.set_text(test_text);
									});
								});
								inner.create_body([](scroll_pane& label){
									label.set_overlay_bar(true);
									label.set_style();
									label.create([&](gui::label& l){
										l.set_expand_policy(layout::expand_policy::prefer);
										l.set_fit(false);
										l.set_typesetting_config(typesetting::layout_config{
												.direction = typesetting::layout_direction::btt
											});
										l.set_text(test_text);
									});
									label.set_layout_policy(layout::layout_policy::vert_major);
								});
							});
						});
					pane.set_layout_policy(layout::layout_policy::vert_major);
				},
				[](scroll_pane& pane){
					pane.create(
						[](table& table){
							table.set_expand_policy(layout::expand_policy::prefer);
							table.create_back([](cpd::rgb_picker& picker){
							}).cell().set_size({600, 600});
						});
					pane.set_layout_policy(layout::layout_policy::none);
				},
				[](scroll_pane& pane){
					pane.create(
						[](sequence& table){
							table.set_expand_policy(layout::expand_policy::prefer);
							table.create_back([](cpd::data_table& table){
								table.get_item() = cpd::data_table_desc::from_csv(LR"(D:\projects\untitled\shader info.csv)");
							});
						});
					pane.set_layout_policy(layout::layout_policy::none);
				},
				[](scroll_pane& pane){
					pane.create(
						[](sequence& table){
							table.set_expand_policy(layout::expand_policy::prefer);
							table.emplace_back<vp>();
						});
					pane.set_layout_policy(layout::layout_policy::none);
				}
			};

		return tests;
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
		menu_hdl->push_back(
			elem_ptr{
				menu_hdl->get_scene(), &menu_hdl.elem(), [&](label& label){
					label.set_text(std::format("Test[{}]: {}", idx, creator.name));
					label.text_entire_align = align::pos::center;
					label.interactivity = interactivity_flag::enabled;
					label.set_transform_config({
							.rotation = text_rotation::deg_270
						});
				}
			}, creator.creator(menu_hdl->get_scene(), &menu_hdl.elem()));
	}

	return result;
}
}
