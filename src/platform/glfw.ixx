module;

#include <GLFW/glfw3.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <imm.h>
#endif

export module mo_yanxi.platform.glfw;

import std;
import mo_yanxi.unicode;

namespace mo_yanxi::platform {

export struct native_window_handle {
	void* value{};

	[[nodiscard]] explicit constexpr operator bool() const noexcept {
		return value != nullptr;
	}
};

export [[nodiscard]] native_window_handle get_native_window_handle(GLFWwindow* window) noexcept {
#ifdef _WIN32
	return native_window_handle{window != nullptr ? glfwGetWin32Window(window) : nullptr};
#else
	(void)window;
	return {};
#endif
}

export struct native_ime_rect {
	int left{};
	int top{};
	int right{1};
	int bottom{1};

	[[nodiscard]] constexpr bool empty() const noexcept {
		return right <= left || bottom <= top;
	}
};

export enum class native_ime_composition_event_type : std::uint8_t {
	begin,
	update,
	commit,
	cancel,
};

export struct native_ime_composition_event {
	native_ime_composition_event_type type{};
	std::u32string text{};
	std::uint32_t cursor{};
};

struct native_ime_controller_impl;

export class native_ime_controller {
public:
	using event_callback = std::move_only_function<void(native_ime_composition_event)>;

	[[nodiscard]] native_ime_controller() noexcept = default;
	[[nodiscard]] explicit native_ime_controller(GLFWwindow* window, event_callback callback);
	~native_ime_controller();

	native_ime_controller(const native_ime_controller&) = delete;
	native_ime_controller& operator=(const native_ime_controller&) = delete;
	native_ime_controller(native_ime_controller&&) noexcept = default;
	native_ime_controller& operator=(native_ime_controller&&) noexcept = default;

	void stop() noexcept;
	[[nodiscard]] bool is_stopped() const noexcept;
	void install();
	void uninstall_noexcept() noexcept;
	void set_enabled(bool enabled);
	void set_cursor_rect(native_ime_rect rect);

private:
	std::shared_ptr<native_ime_controller_impl> impl_{};
};

#ifdef _WIN32
using native_ime_controller_shared = std::shared_ptr<native_ime_controller_impl>;
LRESULT CALLBACK native_ime_window_proc(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param);

[[nodiscard]] std::wstring get_ime_composition_string(HIMC context, const DWORD kind) {
	const LONG byte_count = ImmGetCompositionStringW(context, kind, nullptr, 0);
	if(byte_count <= 0) {
		return {};
	}

	std::wstring result;
	result.resize(static_cast<std::size_t>(byte_count) / sizeof(wchar_t));
	if(result.empty()) {
		return {};
	}

	const LONG copied = ImmGetCompositionStringW(context, kind, result.data(), byte_count);
	if(copied <= 0) {
		return {};
	}

	result.resize(static_cast<std::size_t>(copied) / sizeof(wchar_t));
	return result;
}

[[nodiscard]] std::uint32_t get_ime_composition_cursor(HIMC context, const std::wstring_view composition) {
	const LONG cursor = ImmGetCompositionStringW(context, GCS_CURSORPOS, nullptr, 0);
	if(cursor < 0) {
		return static_cast<std::uint32_t>(unicode::utf32_length_from_utf16(composition));
	}

	const std::size_t code_unit_cursor =
		std::min<std::size_t>(static_cast<std::size_t>(cursor), composition.size());
	return static_cast<std::uint32_t>(
		unicode::utf32_length_from_utf16(composition.substr(0, code_unit_cursor)));
}
#endif

struct native_ime_controller_impl : std::enable_shared_from_this<native_ime_controller_impl> {
	GLFWwindow* window{};
	native_ime_controller::event_callback callback{};
	std::atomic_bool stopped{false};

#ifdef _WIN32
	HWND hwnd{};
	WNDPROC original_wnd_proc{};
	HIMC detached_ime_context{};
	bool hook_installed{};
	bool ime_enabled{};
	bool ime_composing{};
	native_ime_controller_shared* state_handle{};
	native_ime_rect caret_rect{};
	inline static constexpr wchar_t state_property[] = L"XRGUI_NATIVE_IME_STATE";
#endif

	[[nodiscard]] native_ime_controller_impl(
		GLFWwindow* target_window,
		native_ime_controller::event_callback target_callback)
		: window(target_window),
		  callback(std::move(target_callback))
#ifdef _WIN32
		, hwnd(target_window != nullptr ? glfwGetWin32Window(target_window) : nullptr)
#endif
	{
	}

	[[nodiscard]] bool is_stopped() const noexcept {
		return stopped.load(std::memory_order_acquire);
	}

	void stop() noexcept {
		stopped.store(true, std::memory_order_release);
	}

	void emit(native_ime_composition_event event) {
		if(!is_stopped() && callback) {
			std::invoke(callback, std::move(event));
		}
	}

#ifdef _WIN32
	void install() {
		if(hook_installed || hwnd == nullptr) {
			return;
		}

		auto* new_state_handle = new native_ime_controller_shared{shared_from_this()};
		if(SetPropW(hwnd, state_property, new_state_handle) == 0) {
			delete new_state_handle;
			throw std::runtime_error{"failed to attach native IME window state"};
		}

		SetLastError(0);
		const LONG_PTR previous =
			SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&native_ime_window_proc));
		if(previous == 0 && GetLastError() != 0) {
			RemovePropW(hwnd, state_property);
			delete new_state_handle;
			throw std::runtime_error{"failed to install native IME window procedure"};
		}

		state_handle = new_state_handle;
		original_wnd_proc = reinterpret_cast<WNDPROC>(previous);
		hook_installed = true;
		disable_noexcept();
	}

	void release_state_handle_noexcept(HWND target_hwnd) noexcept {
		auto* removed_state_handle =
			static_cast<native_ime_controller_shared*>(RemovePropW(target_hwnd, state_property));
		if(removed_state_handle == nullptr && target_hwnd == hwnd) {
			removed_state_handle = state_handle;
		}

		if(removed_state_handle != nullptr) {
			if(removed_state_handle == state_handle) {
				state_handle = nullptr;
			}
			delete removed_state_handle;
		}
	}

	void restore_context_noexcept() noexcept {
		if(hwnd == nullptr || detached_ime_context == nullptr) {
			return;
		}
		ImmAssociateContext(hwnd, detached_ime_context);
		detached_ime_context = nullptr;
	}

	void cancel_composition_noexcept() const noexcept {
		if(hwnd == nullptr) {
			return;
		}

		if(HIMC context = ImmGetContext(hwnd)) {
			ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
			ImmNotifyIME(context, NI_CLOSECANDIDATE, 0, 0);
			ImmReleaseContext(hwnd, context);
		}
	}

	void disable_noexcept() noexcept {
		ime_enabled = false;
		if(ime_composing) {
			try {
				emit(native_ime_composition_event{.type = native_ime_composition_event_type::cancel});
			}catch(...) {
			}
			ime_composing = false;
		}

		cancel_composition_noexcept();
		if(hwnd == nullptr || detached_ime_context != nullptr) {
			return;
		}
		detached_ime_context = ImmAssociateContext(hwnd, nullptr);
	}

	void uninstall_noexcept() noexcept {
		if(hwnd == nullptr) {
			return;
		}

		disable_noexcept();
		restore_context_noexcept();

		if(hook_installed) {
			const auto current = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_WNDPROC));
			if(current == &native_ime_window_proc && original_wnd_proc != nullptr) {
				SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_wnd_proc));
			}
			release_state_handle_noexcept(hwnd);
		}

		hook_installed = false;
		original_wnd_proc = nullptr;
	}

	void set_enabled(const bool enabled) {
		install();

		if(enabled == ime_enabled) {
			if(enabled) {
				apply_window_position();
			}
			return;
		}

		if(enabled) {
			restore_context_noexcept();
			ime_enabled = true;
			apply_window_position();
		}else {
			disable_noexcept();
		}
	}

	void set_cursor_rect(const native_ime_rect rect) {
		install();
		caret_rect = rect;
		if(ime_enabled) {
			apply_window_position();
		}
	}

	void apply_window_position() const noexcept {
		if(hwnd == nullptr || caret_rect.empty()) {
			return;
		}

		HIMC context = ImmGetContext(hwnd);
		if(context == nullptr) {
			return;
		}

		COMPOSITIONFORM composition_form{};
		composition_form.dwStyle = CFS_FORCE_POSITION;
		composition_form.ptCurrentPos.x = caret_rect.left;
		composition_form.ptCurrentPos.y = caret_rect.top;
		ImmSetCompositionWindow(context, &composition_form);

		CANDIDATEFORM candidate_form{};
		candidate_form.dwIndex = 0;
		candidate_form.dwStyle = CFS_EXCLUDE;
		candidate_form.ptCurrentPos.x = caret_rect.left;
		candidate_form.ptCurrentPos.y = caret_rect.bottom;
		candidate_form.rcArea.left = caret_rect.left;
		candidate_form.rcArea.top = caret_rect.top;
		candidate_form.rcArea.right = caret_rect.right;
		candidate_form.rcArea.bottom = caret_rect.bottom;
		ImmSetCandidateWindow(context, &candidate_form);

		ImmReleaseContext(hwnd, context);
	}

	[[nodiscard]] LRESULT call_original(
		HWND target_hwnd,
		const UINT msg,
		const WPARAM w_param,
		const LPARAM l_param) const noexcept {
		if(original_wnd_proc != nullptr) {
			return CallWindowProcW(original_wnd_proc, target_hwnd, msg, w_param, l_param);
		}
		return DefWindowProcW(target_hwnd, msg, w_param, l_param);
	}

	[[nodiscard]] LRESULT handle_window_message(
		HWND target_hwnd,
		const UINT msg,
		const WPARAM w_param,
		const LPARAM l_param) {
		if(msg == WM_NCDESTROY) {
			release_state_handle_noexcept(target_hwnd);
			hook_installed = false;
			const LRESULT result = call_original(target_hwnd, msg, w_param, l_param);
			stop();
			hwnd = nullptr;
			window = nullptr;
			original_wnd_proc = nullptr;
			detached_ime_context = nullptr;
			return result;
		}

		if(is_stopped() || !ime_enabled) {
			return call_original(target_hwnd, msg, w_param, l_param);
		}

		LPARAM forwarded_l_param = l_param;

		switch(msg) {
		case WM_IME_SETCONTEXT:
			forwarded_l_param &= ~ISC_SHOWUICOMPOSITIONWINDOW;
			forwarded_l_param &= ~ISC_SHOWUIGUIDELINE;
			apply_window_position();
			break;
		case WM_IME_STARTCOMPOSITION:
			ime_composing = true;
			emit(native_ime_composition_event{.type = native_ime_composition_event_type::begin});
			apply_window_position();
			break;
		case WM_IME_COMPOSITION:
			apply_window_position();
			if((l_param & GCS_RESULTSTR) != 0) {
				if(HIMC context = ImmGetContext(target_hwnd)) {
					auto result = get_ime_composition_string(context, GCS_RESULTSTR);
					ImmReleaseContext(target_hwnd, context);
					ime_composing = false;
					auto text = unicode::utf16_to_utf32(result);
					const auto cursor = static_cast<std::uint32_t>(text.size());
					emit(native_ime_composition_event{
						.type = native_ime_composition_event_type::commit,
						.text = std::move(text),
						.cursor = cursor
					});
				}
				return 0;
			}
			if((l_param & GCS_COMPSTR) != 0) {
				if(HIMC context = ImmGetContext(target_hwnd)) {
					auto composition = get_ime_composition_string(context, GCS_COMPSTR);
					const auto cursor = get_ime_composition_cursor(context, composition);
					auto text = unicode::utf16_to_utf32(composition);
					ImmReleaseContext(target_hwnd, context);
					ime_composing = true;
					emit(native_ime_composition_event{
						.type = native_ime_composition_event_type::update,
						.text = std::move(text),
						.cursor = cursor
					});
				}
				return 0;
			}
			break;
		case WM_IME_ENDCOMPOSITION:
			if(ime_composing) {
				emit(native_ime_composition_event{.type = native_ime_composition_event_type::cancel});
				ime_composing = false;
			}
			break;
		case WM_KILLFOCUS:
			if(ime_composing) {
				emit(native_ime_composition_event{.type = native_ime_composition_event_type::cancel});
				ime_composing = false;
			}
			cancel_composition_noexcept();
			break;
		default:
			break;
		}

		return call_original(target_hwnd, msg, w_param, forwarded_l_param);
	}
#else
	void install() {}
	void uninstall_noexcept() noexcept {}
	void set_enabled(bool) {}
	void set_cursor_rect(native_ime_rect) {}
#endif
};

#ifdef _WIN32
LRESULT CALLBACK native_ime_window_proc(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param) {
	auto* state_handle =
		static_cast<native_ime_controller_shared*>(GetPropW(hwnd, native_ime_controller_impl::state_property));
	if(state_handle == nullptr || !*state_handle) {
		return DefWindowProcW(hwnd, msg, w_param, l_param);
	}

	auto state = *state_handle;
	try {
		return state->handle_window_message(hwnd, msg, w_param, l_param);
	}catch(...) {
		return state->call_original(hwnd, msg, w_param, l_param);
	}
}
#endif

native_ime_controller::native_ime_controller(GLFWwindow* window, event_callback callback)
	: impl_(std::make_shared<native_ime_controller_impl>(window, std::move(callback))) {
}

native_ime_controller::~native_ime_controller() {
	stop();
}

void native_ime_controller::stop() noexcept {
	if(impl_ != nullptr) {
		impl_->stop();
	}
}

bool native_ime_controller::is_stopped() const noexcept {
	return impl_ == nullptr || impl_->is_stopped();
}

void native_ime_controller::install() {
	if(impl_ != nullptr) {
		impl_->install();
	}
}

void native_ime_controller::uninstall_noexcept() noexcept {
	if(impl_ != nullptr) {
		impl_->uninstall_noexcept();
	}
}

void native_ime_controller::set_enabled(const bool enabled) {
	if(impl_ != nullptr) {
		impl_->set_enabled(enabled);
	}
}

void native_ime_controller::set_cursor_rect(const native_ime_rect rect) {
	if(impl_ != nullptr) {
		impl_->set_cursor_rect(rect);
	}
}

}
