module;

#include <cassert>
#include <GLFW/glfw3.h>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <imm.h>
#endif


export module mo_yanxi.backend.communicator;

import mo_yanxi.gui.infrastructure;
import std;

void UpdateIMEWindowPosition(GLFWwindow* window, const mo_yanxi::math::irect& caretRectScreen) {
	if (!window) return;

	HWND hwnd = glfwGetWin32Window(window);
	if (!hwnd) return;

	// 使用你封装的 is_zero_area 进行早期无效数据拦截
	if (caretRectScreen.is_zero_area()) return;

	// 1. 坐标获取与转换：屏幕坐标 -> 客户端坐标
	POINT ptTopLeft = { caretRectScreen.get_src_x(), caretRectScreen.get_src_y() };
	POINT ptBottomRight = { caretRectScreen.get_end_x(), caretRectScreen.get_end_y() };

	ScreenToClient(hwnd, &ptTopLeft);
	ScreenToClient(hwnd, &ptBottomRight);

	// 2. 操作输入法上下文
	HIMC himc = ImmGetContext(hwnd);
	if (himc) {
		// --- 设置组合窗口位置（正在输入的未确认拼音字符） ---
		COMPOSITIONFORM compForm = {};
		compForm.dwStyle = CFS_FORCE_POSITION;
		compForm.ptCurrentPos.x = -1000;//ptTopLeft.x;
		compForm.ptCurrentPos.y = -1000;//ptTopLeft.y;
		ImmSetCompositionWindow(himc, &compForm);

		// --- 设置候选词窗口位置（弹出的中文备选项） ---
		CANDIDATEFORM candForm = {};
		candForm.dwIndex = 0;
		candForm.dwStyle = CFS_EXCLUDE;

		// 设置基准建议位置为光标区域的左下角
		candForm.ptCurrentPos.x = ptTopLeft.x;
		candForm.ptCurrentPos.y = ptBottomRight.y;

		// 设置输入法必须避开的光标/文字包围盒区域
		candForm.rcArea.left = ptTopLeft.x;
		candForm.rcArea.top = ptTopLeft.y;
		candForm.rcArea.right = ptBottomRight.x;
		candForm.rcArea.bottom = ptBottomRight.y;

		ImmSetCandidateWindow(himc, &candForm);

		// 释放上下文，防止内存和句柄泄漏
		ImmReleaseContext(hwnd, himc);
	}
}

namespace mo_yanxi::backend::glfw{
struct native_window_state{
	GLFWwindow* window{};
	gui::window_thread_dispatcher* dispatcher{};
	std::atomic_bool stopped{false};

	[[nodiscard]] explicit native_window_state(GLFWwindow* target_window, gui::window_thread_dispatcher& target_dispatcher)
		: window(target_window), dispatcher(std::addressof(target_dispatcher)){
	}

	[[nodiscard]] bool is_stopped() const noexcept{
		return stopped.load(std::memory_order_acquire);
	}

	void stop() noexcept{
		stopped.store(true, std::memory_order_release);
	}

	void require_window_thread() const{
		if(is_stopped()){
			throw std::runtime_error{"native window state is stopped"};
		}
		if(window == nullptr){
			throw std::runtime_error{"native window state has no GLFW window"};
		}
		if(dispatcher == nullptr){
			throw std::runtime_error{"native window state has no dispatcher"};
		}
		if(!dispatcher->is_window_thread()){
			throw std::runtime_error{"native window API called from a non-window thread"};
		}
		assert(dispatcher->is_window_thread());
	}
};

export
struct communicator : gui::native_communicator{
	//
	inline static WNDPROC g_OriginalWndProc = nullptr;

	static LRESULT CALLBACK CustomWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
		if (uMsg == WM_IME_SETCONTEXT) {
			// 关键点：清除显示组合窗口的标志位
			// 这样系统自带的输入拼音的那个小框就不会出现了
			lParam &= ~ISC_SHOWUICOMPOSITIONWINDOW;
			lParam &= ~ISC_SHOWUIGUIDELINE;
			// 注意：如果你连候选词窗口（选中文的那个框）也不想要，
			// 可以继续清除 ISC_SHOWUICANDIDATEWINDOW 标志
			// lParam &= ~ISC_SHOWUICANDIDATEWINDOW;
		}

		// 将其他消息交还给 GLFW 原始的处理函数
		return CallWindowProc(g_OriginalWndProc, hwnd, uMsg, wParam, lParam);
	}

	std::shared_ptr<native_window_state> state{};

	[[nodiscard]] explicit communicator(GLFWwindow* window, gui::window_thread_dispatcher& dispatcher)
		: state(std::make_shared<native_window_state>(window, dispatcher)){
	}

	~communicator() override{
		state->stop();
	}

	static void post_native(
		std::shared_ptr<native_window_state> native_state,
		std::move_only_function<void(native_window_state&)>&& task){
		if(native_state->is_stopped()){
			throw std::runtime_error{"native window state is stopped"};
		}
		if(native_state->dispatcher == nullptr){
			throw std::runtime_error{"native window state has no dispatcher"};
		}
		native_state->dispatcher->post([
			native_state = std::move(native_state),
			task = std::move(task)
		]() mutable {
			native_state->require_window_thread();
			std::invoke(std::move(task), *native_state);
		});
	}

protected:
	void set_clipboard_impl(std::string&& text) override{
		communicator::post_native(
			state,
			[text = std::move(text)](native_window_state& native_state) mutable {
				glfwSetClipboardString(native_state.window, text.c_str());
			});
	}

	void request_clipboard_impl(gui::native_clipboard_request&& request) override{
		communicator::post_native(
			state,
			[request = std::move(request)](native_window_state& native_state) mutable {
				std::string text{};
				if(auto rst = glfwGetClipboardString(native_state.window)){
					text = rst;
				}
				std::move(request).set_value(std::move(text));
			});
	}

public:
	void set_native_cursor_visibility(bool show) override{
		communicator::post_native(
			state,
			[show](native_window_state& native_state) {
				glfwSetInputMode(native_state.window, GLFW_CURSOR, show ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_HIDDEN);
			});
	}

	void set_ime_cursor_rect(const math::raw_frect region) override{
		communicator::post_native(
			state,
			[region](native_window_state& native_state) {
				UpdateIMEWindowPosition(
					native_state.window,
					math::irect{tags::from_extent, region.src.round<int>(), region.extent.round<int>()});
			});
	}
};

}
