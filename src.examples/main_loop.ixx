//
// Created by Matrix on 2026/4/6.
//

export module mo_yanxi.gui.examples.main_loop;

import std;
import mo_yanxi.gui.global;
import mo_yanxi.gui.renderer.frontend;

import mo_yanxi.gui.fx.instruction_extension;

import mo_yanxi.backend.vulkan.renderer;
import mo_yanxi.backend.vulkan.context;


import mo_yanxi.graphic.trail;
import mo_yanxi.math.rand;
import mo_yanxi.math.interpolation;
import mo_yanxi.concurrent.mpsc_double_buffer;
import mo_yanxi.math;

import mo_yanxi.gui.examples;

namespace mo_yanxi::gui::example{
export
struct main_loop{
	backend::vulkan::renderer* renderer_ptr{};
	backend::vulkan::context* ctx_ptr{};
	std::move_only_function<void(const ui_outputs&)> external_scene_relative_init_fn{};
	ccur::mpsc_double_buffer<input_handle::input_event_variant> unhandled_events{};

private:
	std::binary_semaphore begin_semaphore{0};
	std::atomic_bool end_flag{};

	graphic::uniformed_trail trail{60, .75f};

	std::jthread exec_thread;
	scene* target_scene;
public:
	[[nodiscard]] main_loop(backend::vulkan::renderer& renderer_ptr, backend::vulkan::context& ctx_ptr,
	                        std::move_only_function<void(const ui_outputs&)>&& external_scene_relative_init_fn)
		: renderer_ptr(&renderer_ptr),
		  ctx_ptr(&ctx_ptr),
		  external_scene_relative_init_fn(std::move(external_scene_relative_init_fn)), exec_thread{
			  [](std::stop_token stoptoken, main_loop& self, std::thread::id owner_thread_id){
				  self.init();
				  while(true){
					  if(self.sync_main_loop(stoptoken, owner_thread_id)){
						  break;
					  }
				  }
			  },
			  std::ref(*this), std::this_thread::get_id()
		  }{
	}

	scene& get_scene() const noexcept{
		return *target_scene;
	}

	void request_stop() noexcept{
		exec_thread.request_stop();
		permit_burst();
	}

	void join() noexcept{
		exec_thread.request_stop();
		permit_burst();
		exec_thread.join();
	}

	void permit_burst() noexcept {
		begin_semaphore.release();
	}

	void wait_term() const noexcept {
		end_flag.wait(false, std::memory_order_acquire);
	}

	void wait_until_idle() noexcept {
		if(begin_semaphore.try_acquire()){
			begin_semaphore.release();
			//not begin yet, return
			(void)end_flag.load(std::memory_order_acquire);
			return;
		}
		wait_term();
	}

	void wait_term_and_reset() noexcept {
		end_flag.wait(false, std::memory_order_acquire);
		reset_term();
	}

	void reset_term() noexcept {
		end_flag.store(false, std::memory_order_relaxed);
	}

	[[nodiscard]] main_loop(){
		trail.shrink_interval *= 2.f;
	}

	//TODO save exception ptr
	template <typename T>
	auto term(T&& fn){
		begin_semaphore.acquire();
		using ret_ty = std::invoke_result_t<T&&>;
		if constexpr (std::is_void_v<ret_ty>){
			try{
				fn();
			}catch(const std::exception& e){
				end_flag.store(true, std::memory_order_release);
				end_flag.notify_one();
				std::println(std::cerr, "{}", e.what());
				throw;
			}
			end_flag.store(true, std::memory_order_release);
			end_flag.notify_one();
		}else{
			ret_ty ret;
			try{
				ret = fn();
			}catch(const std::exception& e){
				end_flag.store(true, std::memory_order_release);
				end_flag.notify_one();
				std::println(std::cerr, "{}", e.what());
				throw;
			}
			end_flag.store(true, std::memory_order_release);
			end_flag.notify_one();
			return ret;
		}

	}

	void init(){
		term([this]{
			const auto rst = build_main_ui(*ctx_ptr, renderer_ptr->create_frontend());
			target_scene = rst.scene_ptr;
			if(external_scene_relative_init_fn)external_scene_relative_init_fn(rst);
		});
	}

	void main_loop_exec(){
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

		r.update_state(fx::pipeline_config{});
		r.update_state(fx::batch_draw_mode::def);
		r.update_state(fx::blend::pma::standard);
		r.update_state(fx::make_blend_write_mask(true), 0);
		r.update_state(r.get_full_screen_scissor());
		r.update_state(r.get_full_screen_viewport());

		if(true){
			using namespace graphic::draw::instruction;

			constexpr float size = 80;

			const auto [X_count, Y_count] = (r.get_region().extent() / size).ceil().as<int>();
			math::rand rand{54767963};
			for(int x = 0; x < X_count; ++x){
				for(int y = 0; y < Y_count; ++y){
					r.push(rect_aabb{
							.generic = {
								.mode = std::to_underlying(((x + y) % 3 == 0)
									                           ? fx::primitive_draw_mode::draw_slide_line
									                           : fx::primitive_draw_mode::none)
							},
							.v00 = {x * size, y * size},
							.v11 = {x * size + size, y * size + size},
							.vert_color = {
								graphic::color{rand(.5f, 1.f), rand(.5f, 1.f), rand(.5f, 1.f), rand(.5f, 1.f)}
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
					{
						.src = {},
						.extent = math::vector2{r.get_region().extent()}.round<int>()
					},
					{.pipeline_index = 1, .inout_define_index = 0}
				});


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
						              } |
						              math::interp::reverse, math::idx_to_factor(5U, math::max(total, 8U)), 1.f);

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
					{
						.src = {},
						.extent = math::vector2{r.get_region().extent()}.round<int>()
					},
					{.pipeline_index = 1}
				});
		}

		current_focus.draw();
		renderer.batch_host.end_rendering();
		renderer.upload();
		renderer.create_command();
	}

	bool sync_main_loop(const std::stop_token& stoptoken, std::thread::id owner_thread_id){
		return term([&, this]{
			if(stoptoken.stop_requested()){
				clear_main_ui();
				return true;
			}
			main_loop_exec();
			return false;
		});
	}
};
}
