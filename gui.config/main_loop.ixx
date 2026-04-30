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

namespace mo_yanxi::gui::example{

class thread_sync_controller{
private:
	static constexpr std::uint32_t state_idle = 0;
	static constexpr std::uint32_t state_permitted = 1;
	static constexpr std::uint32_t state_running = 2;
	static constexpr std::uint32_t state_done = 3;

	std::atomic<std::uint32_t> sync_state_{state_idle};

public:
	void allow_b() noexcept{
		sync_state_.store(state_permitted, std::memory_order_release);
		sync_state_.notify_one();
	}

	void wait_for_b_done() const noexcept{
		std::uint32_t val = sync_state_.load(std::memory_order_acquire);
		while(val != state_done){
			sync_state_.wait(val, std::memory_order_acquire);
			val = sync_state_.load(std::memory_order_acquire);
		}
	}

	void wait_for_b_done_and_reset() noexcept{
		std::uint32_t val = sync_state_.load(std::memory_order_acquire);
		while(val != state_done){
			sync_state_.wait(val, std::memory_order_acquire);
			val = sync_state_.load(std::memory_order_acquire);
		}
		sync_state_.store(state_idle, std::memory_order_release);
	}

	void reset_done() noexcept{
		sync_state_.store(state_idle, std::memory_order_release);
	}

	bool is_permitted_but_not_running() const noexcept{
		return sync_state_.load(std::memory_order_acquire) == state_permitted;
	}

	bool not_permitted_yet() const noexcept{
		return sync_state_.load(std::memory_order_acquire) == state_idle;
	}

	void wait_for_permission_and_run() noexcept{
		std::uint32_t val = sync_state_.load(std::memory_order_acquire);
		while(val != state_permitted){
			sync_state_.wait(val, std::memory_order_acquire);
			val = sync_state_.load(std::memory_order_acquire);
		}
		// 获得放行后，立即切换到运行状态
		sync_state_.store(state_running, std::memory_order_release);
	}

	void set_done() noexcept{
		sync_state_.store(state_done, std::memory_order_release);
		sync_state_.notify_one();
	}
};

export
struct main_loop_init_return_t{
	scene* main_scene;
};

export
template <typename Payload>
struct main_loop{
	struct functions{
		std::move_only_function<main_loop_init_return_t(main_loop&)> init_fn{};
		std::move_only_function<void(main_loop&)> main_loop_fn{};
		std::move_only_function<void(main_loop&)> exit_fn{};
	};

private:
	backend::vulkan::renderer renderer_;
	backend::vulkan::context* ctx_ptr{};
	functions functions_{};

public:
	ccur::mpsc_double_buffer<input_handle::input_event_variant> unhandled_events{};

	Payload payload{};

private:
	thread_sync_controller sync_ctrl{};
	std::exception_ptr captured_exception_{nullptr};
	std::jthread exec_thread;
	scene* target_scene{};

public:
	[[nodiscard]] main_loop(
		backend::vulkan::renderer&& renderer,
		backend::vulkan::context& ctx_ptr,
		functions&& functions, Payload payload = {})
		: renderer_(std::move(renderer)),
		  ctx_ptr(&ctx_ptr),
		  functions_(std::move(functions)), payload(std::move(payload)), exec_thread{
			  [](std::stop_token stoptoken, main_loop& self){
				  self.init();
				  while(true){
					  if(self.sync_main_loop(stoptoken)){
						  break;
					  }
				  }
			  },
			  std::ref(*this)
		  }{
		mo_yanxi::platform::set_thread_attributes(exec_thread, {
			                                          .name = "xrgui ui thread",
			                                          .priority = platform::thread_priority::realtime
		                                          });

		permit_burst();
		wait_term();
	}

	void resize(math::usize2 extent){
		wait_until_idle();

		get_renderer().resize({extent.x, extent.y});

		auto& focus = get_scene();
		auto exec_thread = std::exchange(focus.ui_main_thread_id, std::this_thread::get_id());
		focus.resize(math::rect_ortho{tags::from_extent, {}, extent}.as<float>());
		focus.ui_main_thread_id = exec_thread;
	}

	[[nodiscard]] backend::vulkan::renderer& get_renderer() noexcept{
		return renderer_;
	}

	[[nodiscard]] backend::vulkan::context& get_ctx() const noexcept{
		return *ctx_ptr;
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

	void permit_burst() noexcept{
		sync_ctrl.allow_b();
	}

public:
	void wait_term(){
		// 移除 const 和 noexcept
		sync_ctrl.wait_for_b_done();
		propagate_exception();
	}

	void wait_until_idle(){
		// 移除 const 和 noexcept
		if(sync_ctrl.not_permitted_yet()){
			return;
		}
		wait_term();
	}

	void wait_term_and_reset(){
		// 移除 noexcept
		sync_ctrl.wait_for_b_done_and_reset();
		propagate_exception();
	}

private:
	void propagate_exception(){
		if(captured_exception_){
			try{
				std::rethrow_exception(std::exchange(captured_exception_, nullptr));
			} catch(const std::exception& e){
				std::println(std::cerr, "{}", e.what());
				throw;
			}
		}
	}

public:
	void reset_term() noexcept{
		sync_ctrl.reset_done();
	}

private:
	template <typename T>
	auto term(T&& fn){
		sync_ctrl.wait_for_permission_and_run();

		using ret_ty = std::invoke_result_t<T&&>;
		if constexpr(std::is_void_v<ret_ty>){
			try{
				fn();
			} catch(...){
				captured_exception_ = std::current_exception();
				sync_ctrl.set_done();
				return;
			}
			sync_ctrl.set_done();
		} else{
			ret_ty ret{};
			try{
				ret = fn();
			} catch(...){
				captured_exception_ = std::current_exception();
				sync_ctrl.set_done();
				return ret;
			}
			sync_ctrl.set_done();
			return ret;
		}
	}

	void init(){
		this->term([this]{
			if(functions_.init_fn){
				const main_loop_init_return_t rst = functions_.init_fn(*this);
				target_scene = rst.main_scene;
			}
		});
	}

	// void main_loop_exec();

	bool sync_main_loop(const std::stop_token& stoptoken){
		bool should_stop = this->term([&, this]{
			if(stoptoken.stop_requested()){
				//TODO clear main loop payload

				if(functions_.exit_fn)functions_.exit_fn(*this);
				return true;
			}

			if(functions_.main_loop_fn) functions_.main_loop_fn(*this);
			return false;
		});

		return should_stop || captured_exception_ != nullptr;
	}

	void done_(){
		auto& renderer = get_renderer();

		renderer.batch_host.end_rendering();
		renderer.upload();
		renderer.create_command();
	}
};
}
