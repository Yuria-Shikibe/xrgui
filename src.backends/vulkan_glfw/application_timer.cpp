module;

#include <GLFW/glfw3.h>

module mo_yanxi.backend.application_timer;


double mo_yanxi::backend::app_get_time() noexcept{
	return (glfwGetTime());
}

void mo_yanxi::backend::app_reset_time(const double t) noexcept{
	glfwSetTime(t);
}

double mo_yanxi::backend::app_get_delta(const double last) noexcept{
	return (glfwGetTime()) - last;
}
