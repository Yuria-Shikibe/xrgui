module;

#include <cassert>
#include <GLFW/glfw3.h>

export module mo_yanxi.backend.communicator;

import mo_yanxi.gui.infrastructure;
import mo_yanxi.backend.glfw.window;
import mo_yanxi.input_handle.input_event_queue;
import mo_yanxi.platform.glfw;
import std;

namespace mo_yanxi::backend::glfw {
struct native_window_state;

[[nodiscard]] platform::native_ime_rect normalize_client_rect(
	GLFWwindow* window,
	const math::raw_frect region) {
	int window_w = 0;
	int window_h = 0;
	int framebuffer_w = 0;
	int framebuffer_h = 0;
	glfwGetWindowSize(window, &window_w, &window_h);
	glfwGetFramebufferSize(window, &framebuffer_w, &framebuffer_h);

	const float scale_x =
		framebuffer_w > 0 ? static_cast<float>(window_w) / static_cast<float>(framebuffer_w) : 1.0f;
	const float scale_y =
		framebuffer_h > 0 ? static_cast<float>(window_h) / static_cast<float>(framebuffer_h) : 1.0f;

	const math::vec2 p0 = region.vert_00() * math::vec2{scale_x, scale_y};
	const math::vec2 p1 = region.vert_11() * math::vec2{scale_x, scale_y};

	const int left = static_cast<int>(std::floor(std::min(p0.x, p1.x)));
	const int top = static_cast<int>(std::floor(std::min(p0.y, p1.y)));
	const int right = std::max(left + 1, static_cast<int>(std::ceil(std::max(p0.x, p1.x))));
	const int bottom = std::max(top + 1, static_cast<int>(std::ceil(std::max(p0.y, p1.y))));

	return platform::native_ime_rect{
		.left = left,
		.top = top,
		.right = right,
		.bottom = bottom
	};
}

[[nodiscard]] input_handle::ime_composition_event_type to_gui_ime_event_type(
	const platform::native_ime_composition_event_type type) noexcept {
	using platform_type = platform::native_ime_composition_event_type;
	using gui_type = input_handle::ime_composition_event_type;

	switch(type) {
	case platform_type::begin:
		return gui_type::begin;
	case platform_type::update:
		return gui_type::update;
	case platform_type::commit:
		return gui_type::commit;
	case platform_type::cancel:
		return gui_type::cancel;
	}

	return gui_type::cancel;
}

void push_ime_composition_event(GLFWwindow* window, platform::native_ime_composition_event event) {
	auto* instance = static_cast<window_instance*>(glfwGetWindowUserPointer(window));
	if(instance == nullptr || instance->get_input_sink() == nullptr) {
		return;
	}
	instance->get_input_sink()->push_ime_composition(input_handle::ime_composition_event{
		.type = to_gui_ime_event_type(event.type),
		.text = std::move(event.text),
		.cursor = event.cursor
	});
}

struct native_window_state {
	GLFWwindow* window{};
	gui::window_thread_dispatcher& dispatcher;
	std::atomic_bool stopped{false};
	platform::native_ime_controller ime_controller{};

	[[nodiscard]] explicit native_window_state(GLFWwindow* target_window, gui::window_thread_dispatcher& target_dispatcher)
		: window(target_window),
		  dispatcher(target_dispatcher),
		  ime_controller(target_window, [target_window](platform::native_ime_composition_event event) {
			  push_ime_composition_event(target_window, std::move(event));
		  }) {
	}

	[[nodiscard]] bool is_stopped() const noexcept {
		return stopped.load(std::memory_order_acquire);
	}

	void stop() noexcept {
		stopped.store(true, std::memory_order_release);
		ime_controller.stop();
	}

	void require_window_thread() const {
		if(is_stopped()) {
			throw std::runtime_error{"native window state is stopped"};
		}
		if(window == nullptr) {
			throw std::runtime_error{"native window state has no GLFW window"};
		}
		if(!dispatcher.is_window_thread()) {
			throw std::runtime_error{"native window API called from a non-window thread"};
		}
		assert(dispatcher.is_window_thread());
	}

	void install_ime_hook() {
		require_window_thread();
		ime_controller.install();
	}

	void uninstall_ime_hook_noexcept() noexcept {
		ime_controller.uninstall_noexcept();
	}

	void set_ime_enabled_native(const bool enabled) {
		require_window_thread();
		ime_controller.set_enabled(enabled);
	}

	void set_ime_cursor_rect_native(const math::raw_frect region) {
		require_window_thread();
		ime_controller.set_cursor_rect(normalize_client_rect(window, region));
	}
};

export struct communicator : gui::native_communicator {
	native_window_state state_;

	[[nodiscard]] explicit communicator(GLFWwindow* window, gui::window_thread_dispatcher& dispatcher)
		: state_(window, dispatcher) {
		communicator::post_native(
			state_,
			[](native_window_state& native_state) {
				native_state.install_ime_hook();
		});
	}

	~communicator() override {
		state_.stop();
		assert(state_.dispatcher.is_closed());
		assert(state_.dispatcher.empty());
	}

	template <typename Fn>
		requires std::invocable<Fn&&, native_window_state&>
	static bool post_native(
		native_window_state& native_state,
		Fn&& task) {
		return communicator::post_native(
			native_state,
			gui::async_operation_binding{},
			std::forward<Fn>(task));
	}

	template <typename Fn>
		requires std::invocable<Fn&&, native_window_state&>
	static bool post_native(
		native_window_state& native_state,
		gui::async_operation_binding binding,
		Fn&& task) {
		if(native_state.is_stopped()) {
			binding.mark_cancelled();
			return false;
		}
		return gui::async_send(native_state.dispatcher, std::move(binding), [
			native_state = std::addressof(native_state),
			task = std::forward<Fn>(task)
		](gui::async_operation_binding& binding, gui::async_task_context&) mutable {
			if(native_state->is_stopped()) {
				binding.mark_cancelled();
				return;
			}
			native_state->require_window_thread();
			std::invoke(std::move(task), *native_state);
		});
	}

protected:
	void begin_shutdown_impl() noexcept override {
		state_.stop();
		if(state_.dispatcher.is_window_thread()) {
			state_.uninstall_ime_hook_noexcept();
			return;
		}
		(void)gui::async_send(
			state_.dispatcher,
			gui::async_operation_binding{},
			[native_state = std::addressof(state_)] {
				native_state->uninstall_ime_hook_noexcept();
			});
	}

	void set_clipboard_impl(std::string&& text) override {
		communicator::post_native(
			state_,
			[text = std::move(text)](native_window_state& native_state) mutable {
				glfwSetClipboardString(native_state.window, text.c_str());
			});
	}

	void request_clipboard_impl(gui::native_clipboard_request&& request) override {
		if(state_.is_stopped()) {
			std::move(request).set_cancelled();
			return;
		}

		(void)gui::async_request(
			state_.dispatcher,
			std::move(request.binding),
			[native_state = std::addressof(state_)] {
				native_state->require_window_thread();
				std::string text;
				if(const auto value = glfwGetClipboardString(native_state->window); value != nullptr) {
					text = value;
				}
				return text;
			},
			std::move(request.reply));
	}

public:
	void set_native_cursor_visibility(bool show) override {
		communicator::post_native(
			state_,
			[show](native_window_state& native_state) {
				glfwSetInputMode(native_state.window, GLFW_CURSOR, show ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_HIDDEN);
			});
	}

	void set_ime_enabled(bool enabled) override {
		communicator::post_native(
			state_,
			[enabled](native_window_state& native_state) {
				native_state.set_ime_enabled_native(enabled);
			});
	}

	void set_ime_cursor_rect(const math::raw_frect region) override {
		communicator::post_native(
			state_,
			[region](native_window_state& native_state) {
				native_state.set_ime_cursor_rect_native(region);
			});
	}
};

}
