module;

#include <GLFW/glfw3.h>

export module mo_yanxi.backend.glfw.window.callback;

namespace mo_yanxi::backend::glfw{
	using Wptr = GLFWwindow*;
	export void set_call_back(Wptr window, void* user);
}
