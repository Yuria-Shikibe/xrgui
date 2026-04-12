module;

#include <GLFW/glfw3.h>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <windows.h>
#include <imm.h>
#endif

#include <GLFW/glfw3native.h>

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

	GLFWwindow* window;
	std::string cache_text{};

	[[nodiscard]] explicit communicator(GLFWwindow* window)
	: window(window){
		HWND hwnd = glfwGetWin32Window(window);
		if (!hwnd) return;

		// 替换窗口过程，并保存原来的指针
		// g_OriginalWndProc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)CustomWndProc);
	}

	std::string_view get_clipboard() override{
		if(auto rst = glfwGetClipboardString(window))return {rst};
		return {};
	}

protected:
	void set_clipboard_impl(std::string&& text) override{
		cache_text = std::move(text);
		glfwSetClipboardString(window, cache_text.data());
	}

public:
	void set_native_cursor_visibility(bool show) override{
		glfwSetInputMode(window, GLFW_CURSOR, show ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_HIDDEN);
	}

	void set_ime_cursor_rect(const math::raw_frect region) override{
		UpdateIMEWindowPosition(window, math::irect{tags::from_extent, region.src.round<int>(), region.extent.round<int>()});
	}
};

}