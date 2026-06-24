module;

#include <GLFW/glfw3.h>
#include <cassert>

module mo_yanxi.backend.glfw.window.callback;

import mo_yanxi.typesetting.util;

import mo_yanxi.backend.glfw.window;

import mo_yanxi.input_handle;
import mo_yanxi.input_handle.input_event_queue;
import mo_yanxi.math.vector2;


import std;

using namespace mo_yanxi::backend;

namespace{
[[nodiscard]] mo_yanxi::window_instance* get_window_instance(GLFWwindow* window) noexcept{
	return static_cast<mo_yanxi::window_instance*>(glfwGetWindowUserPointer(window));
}

void push_input_event(GLFWwindow* window, mo_yanxi::input_handle::raw_input_event event){
	if(auto* instance = get_window_instance(window)){
		instance->push_input_event(std::move(event));
	}
}

[[nodiscard]] mo_yanxi::input_handle::act to_input_action(const int action) noexcept{
	using mo_yanxi::input_handle::act;
	switch(action){
	case GLFW_RELEASE:
		return act::release;
	case GLFW_PRESS:
		return act::press;
	case GLFW_REPEAT:
		return act::repeat;
	default:
		return act::ignore;
	}
}

[[nodiscard]] mo_yanxi::input_handle::mode to_input_mode(const int mods) noexcept{
	using mo_yanxi::input_handle::mode;
	mode result = mode::none;
	if((mods & GLFW_MOD_SHIFT) != 0) result |= mode::shift;
	if((mods & GLFW_MOD_CONTROL) != 0) result |= mode::ctrl;
	if((mods & GLFW_MOD_ALT) != 0) result |= mode::alt;
	if((mods & GLFW_MOD_SUPER) != 0) result |= mode::super;
	if((mods & GLFW_MOD_CAPS_LOCK) != 0) result |= mode::cap_lock;
	if((mods & GLFW_MOD_NUM_LOCK) != 0) result |= mode::num_lock;
	return result;
}
}

GLFWmonitor* GetMonitorFromWindow(GLFWwindow* window){
	if(!window) return nullptr;

	// 1. 获取窗口的位置和尺寸
	int wx, wy;
	glfwGetWindowPos(window, &wx, &wy);
	int ww, wh;
	glfwGetWindowSize(window, &ww, &wh);

	int bestOverlap = 0;
	GLFWmonitor* bestMonitor = nullptr;

	// 2. 获取所有连接的显示器
	int count;
	GLFWmonitor** monitors = glfwGetMonitors(&count);

	if(count == 1){
		return monitors[0];
	}

	for(int i = 0; i < count; ++i){
		GLFWmonitor* monitor = monitors[i];

		// 3. 获取显示器的位置和当前视频模式（分辨率）
		int mx, my;
		glfwGetMonitorPos(monitor, &mx, &my);
		const GLFWvidmode* mode = glfwGetVideoMode(monitor);

		// 显示器的边界定义为 [mx, my] 到 [mx + mode->width, my + mode->height]

		// 计算重叠区域
		// 重叠矩形的左上角坐标 (Intersection)
		int intersectX1 = std::max(wx, mx);
		int intersectY1 = std::max(wy, my);

		// 重叠矩形的右下角坐标
		int intersectX2 = std::min(wx + ww, mx + mode->width);
		int intersectY2 = std::min(wy + wh, my + mode->height);

		// 计算重叠的宽度和高度
		int overlapW = std::max(0, intersectX2 - intersectX1);
		int overlapH = std::max(0, intersectY2 - intersectY1);

		// 计算重叠面积
		int currentOverlap = overlapW * overlapH;

		// 4. 选出重叠面积最大的显示器
		if(currentOverlap > bestOverlap){
			bestOverlap = currentOverlap;
			bestMonitor = monitor;
		}
	}

	return bestMonitor;
}

// --- 主要函数：计算 PPI ---
mo_yanxi::math::vec2 CalculateWindowPPI(GLFWwindow* window) {
    GLFWmonitor* monitor = GetMonitorFromWindow(window);

    if (!monitor) {
        return {102, 102};
    }

    // 1. 获取物理尺寸 (毫米)
    int widthMM, heightMM;
    glfwGetMonitorPhysicalSize(monitor, &widthMM, &heightMM);

    // 2. 获取当前分辨率 (像素)
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    const auto widthPx = mode->width;
    const auto heightPx = mode->height;

    // 3. 计算 PPI (确保物理尺寸有效)
    if (widthMM > 0 && heightMM > 0) {
        // 1 英寸 = 25.4 毫米
        float ppiX = static_cast<float>(widthPx) / (static_cast<float>(widthMM) / 25.4f);
        float ppiY = static_cast<float>(heightPx) / (static_cast<float>(heightMM) / 25.4f);
    	return {ppiX, ppiY};
    }

	return {102, 102};
}

void charInputCallback(glfw::Wptr window, unsigned codepoint){
	push_input_event(window, mo_yanxi::input_handle::raw_input_event{
		.type = mo_yanxi::input_handle::input_event_type::input_u32,
		.input_char = static_cast<char32_t>(codepoint)
	});
}

void mouseBottomCallBack(glfw::Wptr window, const int button, const int action, const int mods){
	using namespace mo_yanxi::input_handle;
	push_input_event(window, raw_input_event{
		.type = input_event_type::input_mouse,
		.input_key = key_set{static_cast<std::uint16_t>(button), to_input_action(action), to_input_mode(mods)}
	});
}

void cursorPosCallback(glfw::Wptr window, const double xPos, const double yPos){
	push_input_event(window, mo_yanxi::input_handle::raw_input_event{
		.type = mo_yanxi::input_handle::input_event_type::cursor_move,
		.cursor = mo_yanxi::math::vector2{xPos, yPos}.as<float>()
	});
}

void cursorEnteredCallback(glfw::Wptr window, const int entered){
	push_input_event(window, mo_yanxi::input_handle::raw_input_event{
		.type = mo_yanxi::input_handle::input_event_type::cursor_inbound,
		.is_inbound = entered != 0
	});
}

void scrollCallback(glfw::Wptr window, const double xOffset, const double yOffset){
	push_input_event(window, mo_yanxi::input_handle::raw_input_event{
		.type = mo_yanxi::input_handle::input_event_type::input_scroll,
		.cursor = mo_yanxi::math::vector2{xOffset, yOffset}.as<float>()
	});
}

void keyCallback(glfw::Wptr window, const int key, const int scanCode, const int action, const int mods){
	if(key >= 0 && key < GLFW_KEY_LAST){
		using namespace mo_yanxi::input_handle;
		push_input_event(window, raw_input_event{
			.type = input_event_type::input_key,
			.input_key = key_set{
				static_cast<std::uint16_t>(key),
				to_input_action(action),
				to_input_mode(mods)
			}
		});
	}
	(void)scanCode;
}

void windowFocusCallback(glfw::Wptr window, const int focused){
	if(focused == GLFW_FALSE){
		push_input_event(window, mo_yanxi::input_handle::raw_input_event{
			.type = mo_yanxi::input_handle::input_event_type::focus_lost
		});
	}
}



void glfw::set_call_back(Wptr window, void* user){
	glfwSetWindowUserPointer(window, user);
	glfwSetFramebufferSizeCallback(window, window_instance::native_resize_callback);
	glfwSetScrollCallback(window, scrollCallback);
	glfwSetMouseButtonCallback(window, mouseBottomCallBack);
	glfwSetCursorPosCallback(window, cursorPosCallback);
	glfwSetCursorEnterCallback(window, cursorEnteredCallback);
	glfwSetWindowFocusCallback(window, windowFocusCallback);
	glfwSetKeyCallback(window, keyCallback);
	glfwSetCharCallback(window, charInputCallback);

	typesetting::glyph_size::screen_ppi = CalculateWindowPPI(window);
}
