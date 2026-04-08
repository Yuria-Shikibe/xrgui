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

import mo_yanxi.platform.thread;
import mo_yanxi.gui.examples;

namespace mo_yanxi::gui::example {

// ==========================================
// 4 状态同步控制器封装 (替代 Semaphore + atomic_bool)
// ==========================================
class thread_sync_controller {
private:
	// 状态机常量定义：
	// 0: A 持有控制权 / 初始或重置后状态
	// 1: A 已放行，但 B 尚未真正开始执行
	// 2: B 正在执行任务中
	// 3: B 已完成执行，等待 A 确认与重置
	static constexpr std::uint32_t state_idle = 0;
	static constexpr std::uint32_t state_permitted = 1;
	static constexpr std::uint32_t state_running = 2;
	static constexpr std::uint32_t state_done = 3;

	// mutable 允许在 const 成员函数中调用同步等待
	mutable std::atomic<std::uint32_t> sync_state_{state_idle};

public:
	// --------------------------------------
	// 供线程 A (main_loop 控制者) 调用
	// --------------------------------------
	void allow_b() noexcept {
		sync_state_.store(state_permitted, std::memory_order_release);
		sync_state_.notify_one();
	}

	void wait_for_b_done() const noexcept {
		std::uint32_t val = sync_state_.load(std::memory_order_acquire);
		while (val != state_done) {
			sync_state_.wait(val, std::memory_order_acquire);
			val = sync_state_.load(std::memory_order_acquire);
		}
	}

	void wait_for_b_done_and_reset() noexcept {
		std::uint32_t val = sync_state_.load(std::memory_order_acquire);
		while (val != state_done) {
			sync_state_.wait(val, std::memory_order_acquire);
			val = sync_state_.load(std::memory_order_acquire);
		}
		sync_state_.store(state_idle, std::memory_order_release);
		sync_state_.notify_one();
	}

	void reset_done() noexcept {
		sync_state_.store(state_idle, std::memory_order_release);
	}

	bool is_permitted_but_not_running() const noexcept {
		return sync_state_.load(std::memory_order_acquire) == state_permitted;
	}

	// --------------------------------------
	// 供线程 B (执行线程 term 函数) 调用
	// --------------------------------------
	void wait_for_permission_and_run() noexcept {
		std::uint32_t val = sync_state_.load(std::memory_order_acquire);
		while (val != state_permitted) {
			sync_state_.wait(val, std::memory_order_acquire);
			val = sync_state_.load(std::memory_order_acquire);
		}
		// 获得放行后，立即切换到运行状态
		sync_state_.store(state_running, std::memory_order_release);
	}

	void set_done() noexcept {
		sync_state_.store(state_done, std::memory_order_release);
		sync_state_.notify_one();
	}
};

export
struct main_loop {
	backend::vulkan::renderer* renderer_ptr{};
	backend::vulkan::context* ctx_ptr{};
	std::move_only_function<void(const ui_outputs&)> external_scene_relative_init_fn{};
	ccur::mpsc_double_buffer<input_handle::input_event_variant> unhandled_events{};

private:
	// 使用单一控制器替代旧的 begin_semaphore 和 end_flag
	thread_sync_controller sync_ctrl{};

	graphic::uniformed_trail trail{60, .75f};

	std::jthread exec_thread;
	scene* target_scene;

public:
	[[nodiscard]] main_loop(backend::vulkan::renderer& renderer_ptr, backend::vulkan::context& ctx_ptr,
	                        std::move_only_function<void(const ui_outputs&)>&& external_scene_relative_init_fn)
		: renderer_ptr(&renderer_ptr),
		  ctx_ptr(&ctx_ptr),
		  external_scene_relative_init_fn(std::move(external_scene_relative_init_fn)), exec_thread{
			  [](std::stop_token stoptoken, main_loop& self, std::thread::id owner_thread_id) {
				  self.init();
				  while(true){
					  if(self.sync_main_loop(stoptoken, owner_thread_id)){
						  break;
					  }
				  }
			  },
			  std::ref(*this), std::this_thread::get_id()
		  } {
		mo_yanxi::platform::set_thread_attributes(exec_thread, {
			.name = "xrgui ui thread",
			.priority = platform::thread_priority::realtime
		});
	}

	scene& get_scene() const noexcept {
		return *target_scene;
	}

	void request_stop() noexcept {
		exec_thread.request_stop();
		permit_burst();
	}

	void join() noexcept {
		exec_thread.request_stop();
		permit_burst();
		exec_thread.join();
	}

	void permit_burst() noexcept {
		sync_ctrl.allow_b();
	}

	void wait_term() const noexcept {
		sync_ctrl.wait_for_b_done();
	}

	void wait_until_idle() noexcept {
		// 如果状态处于 state_permitted，等价于旧代码中的 try_acquire() 成功。
		// 意味着已经给信号了，但尚未执行，此时可直接返回。
		if(sync_ctrl.is_permitted_but_not_running()) {
			return;
		}
		wait_term();
	}

	void wait_term_and_reset() noexcept {
		sync_ctrl.wait_for_b_done_and_reset();
	}

	void reset_term() noexcept {
		sync_ctrl.reset_done();
	}

	[[nodiscard]] main_loop() {
		trail.shrink_interval *= 2.f;
	}

	template <typename T>
	auto term(T&& fn) {
		sync_ctrl.wait_for_permission_and_run();

		using ret_ty = std::invoke_result_t<T&&>;
		if constexpr (std::is_void_v<ret_ty>) {
			try {
				fn();
			} catch(const std::exception& e) {
				sync_ctrl.set_done();
				std::println(std::cerr, "{}", e.what());
				throw;
			}
			sync_ctrl.set_done();
		} else {
			ret_ty ret;
			try {
				ret = fn();
			} catch(const std::exception& e) {
				sync_ctrl.set_done();
				std::println(std::cerr, "{}", e.what());
				throw;
			}
			sync_ctrl.set_done();
			return ret;
		}
	}

	void init() {
		term([this] {
			const auto rst = build_main_ui(*ctx_ptr, renderer_ptr->create_frontend());
			target_scene = rst.scene_ptr;
			if(external_scene_relative_init_fn)external_scene_relative_init_fn(rst);
		});
	}

	void main_loop_exec() {
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

					graphic::hsv hsv{yBHsv.h + rand(.05f), yBHsv.s + rand(.071f), yBHsv.v + rand(.051f)};
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

	bool sync_main_loop(const std::stop_token& stoptoken, std::thread::id owner_thread_id) {
		return term([&, this] {
			if(stoptoken.stop_requested()) {
				clear_main_ui();
				return true;
			}
			main_loop_exec();
			return false;
		});
	}
};

} // namespace mo_yanxi::gui::example