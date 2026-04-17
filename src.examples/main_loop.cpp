module mo_yanxi.gui.examples.main_loop;

import mo_yanxi.gui.examples.constants;

namespace mo_yanxi::gui::example{
void main_loop::main_loop_exec(){
	auto& current_focus = *target_scene;
	auto deltatime = global::consume_current_input(current_focus, [this](input_handle::input_event_variant e){
		unhandled_events.push(e);
	});

	current_focus.layout();

	auto& renderer = *renderer_ptr;
	auto& r = current_focus.renderer();

	trail.update(deltatime.count() * 60, current_focus.get_cursor_pos(), 2);
	renderer.batch_host.begin_rendering();
	renderer.batch_host.get_data_group_non_vertex_info().push_default(fx::ui_state(
		r.get_region().extent(),
		current_focus.get_current_time() / 60.
	));
	renderer.batch_host.get_data_group_non_vertex_info().push_default(fx::slide_line_config{});

	r.init_projection();


	r.update_state(r.get_full_screen_scissor());
	r.update_state(r.get_full_screen_viewport());
	r.update_state(fx::pipeline_config{.pipeline_index = gpip_idx::def});
	r.update_state(fx::batch_draw_mode::def);
	r.update_state(fx::blend::pma::standard);
	r.update_state(fx::make_blend_write_mask(true), 0);

	if(true){
		using namespace graphic::draw::instruction;
		constexpr float size = 80;

		const auto [X_count, Y_count] = (r.get_region().extent() / size).ceil().as<int>();
		math::rand rand{54767963};

		constexpr auto tl = graphic::color{}.from_rgb888(0X65A5F0).to_hsv();
		constexpr auto tr = graphic::color{}.from_rgb888(0X9DE6D1).to_hsv();
		constexpr auto bl = graphic::color{}.from_rgb888(0XD9C5F0).to_hsv();
		constexpr auto br = graphic::color{}.from_rgb888(0XF0969D).to_hsv();

		r.push(rect_aabb{
				.generic = {},
				.v00 = {},
				.v11 = r.get_region().extent(),
				.vert_color = {graphic::colors::gray.create_lerp(graphic::colors::ROYAL, .5f)}
			});
		for(int x = 0; x < X_count; ++x){
			const auto xProg = static_cast<float>(x) / static_cast<float>(X_count);
			graphic::hsv xTHsv{math::lerp(tl, tr, xProg)};
			graphic::hsv xBHsv{math::lerp(bl, br, xProg)};
			for(int y = 0; y < Y_count; ++y){
				const auto yProg = static_cast<float>(y) / static_cast<float>(Y_count);

				graphic::hsv yBHsv{math::lerp(xTHsv, xBHsv, yProg)};

				graphic::hsv hsv{math::clamp(yBHsv.h + rand(.05f)), yBHsv.s + rand(.071f), yBHsv.v + rand(.051f)};
				r.push(rect_aabb{
						.generic = {
							.mode = std::to_underlying(rand.chance(.15f)
								                           ? fx::primitive_draw_mode::draw_slide_line
								                           : fx::primitive_draw_mode::none)
						},
						.v00 = {x * size, y * size},
						.v11 = {x * size + size, y * size + size},
						.vert_color = {
							graphic::color{}.from_hsv(hsv).set_a(1)
						}
					});
			}
		}

		if(false){
			{
				state_guard g{r, fx::blend::multiply};
				r.push(poly{
						.pos = current_focus.get_cursor_pos().add_x(-150),
						.segments = 16,
						.radius = {0, 64},
						.color = {graphic::colors::gray, graphic::colors::white}
					});
			}

			{
				state_guard g{r, fx::blend::pma::screen};
				r.push(poly{
						.pos = current_focus.get_cursor_pos().add_x(150),
						.segments = 16,
						.radius = {0, 64},
						.color = {graphic::colors::gray, graphic::colors::white}
					});
			}

			{
				state_guard g{r, fx::blend::pma::additive};
				r.push(poly{
						.pos = current_focus.get_cursor_pos().add_y(150),
						.segments = 16,
						.radius = {0, 64},
						.color = {graphic::colors::gray, graphic::colors::white}
					});
			}

			{
				state_guard g{r, fx::blend::pma::subtractive};
				r.push(poly{
						.pos = current_focus.get_cursor_pos().add_y(-150),
						.segments = 16,
						.radius = {0, 64},
						.color = {graphic::colors::gray, graphic::colors::white}
					});
			}

			r.push(poly{
					.pos = current_focus.get_cursor_pos(),
					.segments = 16,
					.radius = {0, 64},
					.color = {graphic::colors::gray, graphic::colors::white}
				});
		}

		r.update_state(fx::batch_draw_mode::msdf);
		r << fx::nine_patch_draw_vert_color{
				.patch = &assets::builtin::default_round_square_boarder,
				.region = {200, 200, 600, 600},
				.color = {
					graphic::colors::white, graphic::colors::CYAN, graphic::colors::ROYAL, graphic::colors::GREEN
				}
			};

		r.update_state(fx::blit_config{
				fx::blit_config::full_screen_region,
				{.pipeline_index = cpip_idx::blend, .inout_define_index = cpip_bind_idx::to_background}
			});

		{
			r.update_state(fx::push_mask{});

			r.update_state(fx::pipeline_config{.pipeline_index = gpip_idx::mask_draw});
			r.update_state(fx::push_constant{fx::batch_draw_mode::msdf, fx::mask_write_type::ignore_last});



			r << fx::nine_patch_draw_vert_color{
				.patch = &assets::builtin::default_round_square_base,
				.region = {200, 200, 600, 600},
				.color = {
					graphic::colors::white, graphic::colors::gray, graphic::colors::gray, graphic::colors::black
				}
			};

			r.update_state(fx::push_mask{});
			r.update_state(fx::push_constant{fx::batch_draw_mode::msdf, fx::mask_write_type::mul_last});

				r << fx::nine_patch_draw_vert_color{
					.patch = &assets::builtin::default_round_square_base,
					.region = {300, 300, 600, 600},
					.color = {
						graphic::colors::white, graphic::colors::gray, graphic::colors::gray, graphic::colors::black
					}
				};


			r.update_state(fx::pipeline_config{.pipeline_index = gpip_idx::mask_apply});
			r.update_state(fx::push_constant{fx::batch_draw_mode::msdf, fx::mask_read_mode::def});

			r << fx::circle{
				.pos = {current_focus.get_cursor_pos()},
				.radius = {0, 300},
				.color = {graphic::colors::white, graphic::colors::white}
			};

			r.update_state(fx::pop_mask{});
			r.update_state(fx::pop_mask{});
			r.update_state(fx::blit_config{
					fx::blit_config::full_screen_region,
					{
						.pipeline_index = cpip_idx::blend,
						.inout_define_index = cpip_bind_idx::to_background
					}
				});

			r.update_state(fx::pipeline_config{.pipeline_index = gpip_idx::def});
			r.update_state(fx::batch_draw_mode::def);

		}


		// r.update_state(fx::pipeline_config{.pipeline_index = gpip_idx::def});
		// r.update_state(fx::batch_draw_mode::def);

		{
			struct trail_node_data : graphic::trail::node_type{
				float idx_scale;
				graphic::color color;
				[[nodiscard]] float get_width() const noexcept{
					return idx_scale * scale;
				}
			};

			trail.iterate(1.f,
			              [last = trail.head_pos_or({})](
			              const graphic::trail::node_type& node, const unsigned idx, const unsigned total) mutable{
				              using namespace graphic;
				              math::rand rand{std::bit_cast<std::uintptr_t>(&node)};

				              const float factor_global = math::idx_to_factor(idx, total);

				              const auto fac = factor_global | math::interp::pow2Out;
				              const auto off = rand.range(1.f) * fac * math::curve(factor_global, .05f, .2f);
				              const auto tan = (node.pos - last).rotate_rt_counter_clockwise() * off;

				              last = node.pos;
				              auto n = node;

				              const auto color = math::lerp(colors::black, colors::aqua.to_light(2.5f),
				                                            factor_global);
				              return trail_node_data{
						              n,
						              factor_global | math::interp::pow2In | math::interp::interp_func{
							              math::interp::spec::concave_curve_fixed{.1f}
						              } | math::interp::reverse,
						              color
					              };
			              }, [&](std::span<const trail_node_data, 4> sspn){
				              using namespace graphic;
				              using namespace graphic::draw;

				              const auto appr = sspn[1].pos - sspn[2].pos;
				              const auto apprLen = appr.length();
				              const auto seg = math::clamp(static_cast<unsigned>(apprLen / 16.f), 2U, 8U);

				              r.push(parametric_curve{
						              .param = curve_trait_mat::b_spline * (sspn | std::views::transform(
							              &trail_node_data::pos)),
						              .stroke = math::range{sspn[1].get_width(), sspn[2].get_width()} * 10.f,
						              .segments = seg,
						              .color = {colors::aqua.to_light(2.5f)},
					              });
				              r.push(parametric_curve{
						              .param = curve_trait_mat::b_spline * (sspn | std::views::transform(
							              &trail_node_data::pos)),
						              .stroke = math::range{sspn[1].get_width(), sspn[2].get_width()} * 5.f,
						              .segments = seg,
						              .color = {colors::black},
					              });
			              });
			trail.iterate(1.f,
			              [last = trail.head_pos_or({})](
			              const graphic::trail::node_type& node, const unsigned idx, const unsigned total) mutable{
				              using namespace graphic;
				              math::rand rand{std::bit_cast<std::uintptr_t>(&node)};

				              float factor_global = math::idx_to_factor(idx, math::max(total, 8U));

				              const auto fac = factor_global | math::interp::pow2Out;
				              const auto off = rand.range(1.f) * fac * math::curve(factor_global, .05f, .5f);
				              const auto tan = (node.pos - last).rotate_rt_counter_clockwise() * off;

				              last = node.pos;
				              auto n = node;

				              n.pos += tan;
				              const auto color = math::lerp(colors::aqua.to_light(2.5f),
				                                            colors::pale_green.to_light(1.5f),
				                                            factor_global);

				              factor_global = math::curve(
					              factor_global | math::interp::interp_func{
						              math::interp::spec::concave_curve_fixed{.1f}
					              } | math::interp::reverse, math::idx_to_factor(5U, math::max(total, 8U)), 1.f);

				              return trail_node_data{n, factor_global, color};
			              }, [&](std::span<const trail_node_data, 4> sspn){
				              using namespace graphic;
				              using namespace graphic::draw;

				              const auto appr = sspn[1].pos - sspn[2].pos;
				              const auto apprLen = appr.length();
				              const auto seg = math::clamp(static_cast<unsigned>(apprLen / 16.f), 4U, 12U);

				              r.push(parametric_curve{
						              .param = curve_trait_mat::b_spline * (sspn | std::views::transform(
							              &trail_node_data::pos)),
						              .stroke = math::range{sspn[1].get_width(), sspn[2].get_width()} * 8.f,
						              .segments = seg,
						              .color = {sspn[1].color, sspn[1].color, sspn[2].color, sspn[2].color},
					              });
			              });
		}

		r.push(triangle{});
		r.update_state(fx::blit_config{
			fx::blit_config::full_screen_region,
				{.pipeline_index = cpip_idx::blend}
			});
	}


	// current_focus.draw();
	renderer.batch_host.end_rendering();
	renderer.upload();
	renderer.create_command();
}
}

