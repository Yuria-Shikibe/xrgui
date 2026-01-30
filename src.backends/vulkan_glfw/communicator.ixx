module;

#include <GLFW/glfw3.h>

export module mo_yanxi.backend.communicator;

import mo_yanxi.gui.infrastructure;

namespace mo_yanxi::backend::glfw{
export
struct communicator : gui::native_communicator{
	GLFWwindow* window;
	std::string cache_text{};

	[[nodiscard]] explicit communicator(GLFWwindow* window)
	: window(window){
	}

	std::string_view get_clipboard() override{
		return {glfwGetClipboardString(window)};
	}

	void set_clipboard_impl(std::string_view text, bool zero_terminated) override{
		if(zero_terminated){
			glfwSetClipboardString(window, text.data());
		}else{
			cache_text = text;
			//no other better way to provide a zero terminated string
			glfwSetClipboardString(window, cache_text.data());
		}
	}

	void set_native_cursor_visibility(bool show) override{
		glfwSetInputMode(window, GLFW_CURSOR, show ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_HIDDEN);
	}
};

}