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

	void send_clipboard(std::string_view text) override{
		cache_text = text;
		//no other better way to provide a zero terminated string
		glfwSetClipboardString(window, cache_text.data());
	}
};

}