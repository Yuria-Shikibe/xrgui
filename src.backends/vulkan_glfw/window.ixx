module;

#define GLFW_INCLUDE_VULKAN

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#else
#define GLFW_NATIVE_INCLUDE_NONE
#endif

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

export module mo_yanxi.backend.glfw.window;
import std;
import mo_yanxi.backend.glfw.window.callback;
import mo_yanxi.handle_wrapper;
import mo_yanxi.vk.util;

namespace mo_yanxi{
//TODO set according to user monitor
constexpr std::uint32_t WIDTH = 1280;
constexpr std::uint32_t HEIGHT = 720;

export namespace backend::glfw{
void initialize(){
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
}

void terminate(){
	glfwTerminate();
}
}

export enum class window_mode{
	windowed, // 普通窗口
	fullscreen, // 独占全屏 (Exclusive Fullscreen)
	borderless_windowed // 无边框全屏窗口 (Full Screen Borderless Window)
};

export struct window_instance{
	struct resize_event{
		VkExtent2D size{};

		[[nodiscard]] explicit resize_event(const VkExtent2D size)
			: size(size){
		}
	};

public:
	[[nodiscard]] window_instance() = default;

	[[nodiscard]] window_instance(const char* name, bool full_screen = false){
		// 初始化时先暂存一个默认位置，以防立即切换模式
		last_windowed_geo = {100, 100, static_cast<int>(WIDTH), static_cast<int>(HEIGHT)};

		handle = glfwCreateWindow(WIDTH, HEIGHT, name, nullptr, nullptr);

		// 获取实际生成的窗口位置和大小更新存档
		if(handle){
			glfwGetWindowPos(handle, &last_windowed_geo.x, &last_windowed_geo.y);
			glfwGetWindowSize(handle, &last_windowed_geo.w, &last_windowed_geo.h);
		}

		backend::glfw::set_call_back(handle, this);

		int x;
		int y;
		glfwGetWindowSize(handle, &x, &y);
		size = VkExtent2D{static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)};

		// 如果构造函数要求全屏 (简单处理为独占全屏)
		if(full_screen){
			set_mode(window_mode::fullscreen);
		}
	}

	~window_instance(){
		if(handle){
			glfwDestroyWindow(handle);
		}
	}

	[[nodiscard]] bool iconified() const noexcept{
		return glfwGetWindowAttrib(handle, GLFW_ICONIFIED) ||
			(size.width == 0 || size.height == 0);
	}

	[[nodiscard]] bool should_close() const noexcept{
		return glfwWindowShouldClose(handle);
	}


	void poll_events() const noexcept{
		glfwPollEvents();
	}

	void wait_event() const noexcept{
		glfwWaitEvents();
	}

	[[nodiscard]] VkSurfaceKHR create_surface(VkInstance instance) const{
		VkSurfaceKHR surface{};
		if(const auto rst = glfwCreateWindowSurface(instance, handle, nullptr, &surface)){
			throw vk::vk_error(rst, "Failed to create window surface!");
		}
		return surface;
	}

	[[nodiscard]] GLFWwindow* get_handle() const noexcept{ return handle; }

	[[nodiscard]] VkExtent2D get_size() const noexcept{
		return size;
	}

	static void native_resize_callback(GLFWwindow* window, int width, int height){
		const auto app = static_cast<window_instance*>(glfwGetWindowUserPointer(window));
		app->resize(width, height);
	}

	[[nodiscard]] constexpr bool check_resized() noexcept{
		return std::exchange(lazy_resized_check, false);
	}

	// --- 新增功能区域 Start ---

	// 设置窗口模式：窗口化 / 独占全屏 / 无边框全屏
	void set_mode(window_mode mode){
		if(current_mode == mode) return;

		// 如果当前是窗口模式，切换走之前，必须保存位置和大小
		if(current_mode == window_mode::windowed){
			glfwGetWindowPos(handle, &last_windowed_geo.x, &last_windowed_geo.y);
			glfwGetWindowSize(handle, &last_windowed_geo.w, &last_windowed_geo.h);
		}

		GLFWmonitor* monitor = glfwGetPrimaryMonitor(); // 简单起见，使用主显示器
		const GLFWvidmode* vid_mode = glfwGetVideoMode(monitor);

		switch(mode){
		case window_mode::fullscreen :
			// 独占全屏：指定 monitor，刷新率通常跟随显示器
			glfwSetWindowAttrib(handle, GLFW_DECORATED, GLFW_TRUE); // 全屏下Decorated通常被忽略，但恢复时需要
			glfwSetWindowMonitor(handle, monitor, 0, 0, vid_mode->width, vid_mode->height, vid_mode->refreshRate);
			break;

		case window_mode::borderless_windowed :
			// 无边框窗口：monitor 为 nullptr，去掉边框，位置(0,0)，大小等于屏幕大小
			glfwSetWindowAttrib(handle, GLFW_DECORATED, GLFW_FALSE);
			// 注意：这里使用 GLFW_DONT_CARE 作为刷新率，因为本质上还是窗口模式
			glfwSetWindowMonitor(handle, nullptr, 0, 0, vid_mode->width, vid_mode->height, GLFW_DONT_CARE);
			break;

		case window_mode::windowed :
			// 恢复窗口：monitor 为 nullptr，恢复边框，恢复之前的位置和大小
			glfwSetWindowAttrib(handle, GLFW_DECORATED, GLFW_TRUE);
			glfwSetWindowMonitor(handle, nullptr,
			                     last_windowed_geo.x, last_windowed_geo.y,
			                     last_windowed_geo.w, last_windowed_geo.h,
			                     GLFW_DONT_CARE);
			break;
		}

		current_mode = mode;
	}

	[[nodiscard]] window_mode get_mode() const noexcept{
		return current_mode;
	}

	// --- 新增功能区域 End ---

private:
	void resize(const int w, const int h){
		if(w == size.width && h == size.height) return;
		size = VkExtent2D{static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h)};
		lazy_resized_check = true;
	}

public:
	window_instance(const window_instance& other) = delete;
	window_instance& operator=(const window_instance& other) = delete;

	window_instance(window_instance&& other) noexcept
		:
		handle{std::move(other.handle)},
		size{std::move(other.size)},
		current_mode{other.current_mode},
		last_windowed_geo{other.last_windowed_geo}{
		if(handle) backend::glfw::set_call_back(handle, this);
	}

	window_instance& operator=(window_instance&& other) noexcept{
		if(this == &other) return *this;
		handle = std::move(other.handle);
		size = std::move(other.size);
		// 移动新增的状态
		current_mode = other.current_mode;
		last_windowed_geo = other.last_windowed_geo;

		if(handle) backend::glfw::set_call_back(handle, this);
		return *this;
	}

#ifdef _WIN32
	HWND get_native() const noexcept{
		return glfwGetWin32Window(handle);
	}
#endif

private:
	struct window_geometry{
		int x, y, w, h;
	};

	exclusive_handle_member<GLFWwindow*> handle{};
	VkExtent2D size{};
	bool lazy_resized_check{};

	window_mode current_mode{window_mode::windowed};
	window_geometry last_windowed_geo{};
};
}
