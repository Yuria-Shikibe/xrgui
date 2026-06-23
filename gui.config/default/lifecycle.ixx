export module mo_yanxi.gui.cfg.builtin.lifecycle;

import std;
import mo_yanxi.log;
import mo_yanxi.platform;
import mo_yanxi.font;
import mo_yanxi.backend.glfw.window;

namespace mo_yanxi::gui::cfg::builtin{

export
class teardown_stack{
private:
	struct entry{
		std::string name;
		std::move_only_function<void()> cleanup;
	};

	std::vector<entry> stack_;
	bool cleanup_started_{false};

public:
	teardown_stack() = default;

	teardown_stack(const teardown_stack&) = delete;
	teardown_stack& operator=(const teardown_stack&) = delete;
	teardown_stack(teardown_stack&&) = default;
	teardown_stack& operator=(teardown_stack&&) = default;

	~teardown_stack() noexcept{
		run_all();
	}

	void push(std::string name, std::move_only_function<void()> cleanup){
		if(cleanup_started_){
			log::error({"Lifecycle"}, "Cannot push '{}' to teardown_stack after cleanup has started", name);
			return;
		}
		log::debug({"Lifecycle"}, "Registered teardown handler: {}", name);
		stack_.push_back(entry{std::move(name), std::move(cleanup)});
	}

	void run_all() noexcept{
		if(cleanup_started_){
			return;
		}
		cleanup_started_ = true;

		while(!stack_.empty()){
			auto& e = stack_.back();
			log::info({"Lifecycle"}, "Teardown: {}", e.name);
			try{
				e.cleanup();
				log::debug({"Lifecycle"}, "Teardown completed: {}", e.name);
			} catch(const std::exception& ex){
				log::error({"Lifecycle"}, "Exception during teardown of '{}': {}", e.name, ex.what());
			} catch(...){
				log::error({"Lifecycle"}, "Unknown exception during teardown of '{}'", e.name);
			}
			stack_.pop_back();
		}
	}

	[[nodiscard]] bool empty() const noexcept{
		return stack_.empty();
	}

	[[nodiscard]] std::size_t size() const noexcept{
		return stack_.size();
	}
};

export
class platform_runtime{
private:
	bool platform_initialized_{false};
	bool font_initialized_{false};
	bool glfw_initialized_{false};
	bool shutdown_done_{false};

public:
	platform_runtime(){
		log::info({"Lifecycle"}, "Initializing platform subsystems");

		log::debug({"Lifecycle"}, "Platform initialize");
		platform::initialize();
		platform_initialized_ = true;

		log::debug({"Lifecycle"}, "Font initialize");
		font::initialize();
		font_initialized_ = true;

		log::debug({"Lifecycle"}, "GLFW initialize");
		backend::glfw::initialize();
		glfw_initialized_ = true;

		log::info({"Lifecycle"}, "Platform subsystems initialized");
	}

	~platform_runtime() noexcept{
		shutdown();
	}

	platform_runtime(const platform_runtime&) = delete;
	platform_runtime& operator=(const platform_runtime&) = delete;
	platform_runtime(platform_runtime&&) = delete;
	platform_runtime& operator=(platform_runtime&&) = delete;

	void shutdown() noexcept{
		if(shutdown_done_){
			return;
		}
		shutdown_done_ = true;

		log::info({"Lifecycle"}, "Shutting down platform subsystems");

		if(glfw_initialized_){
			log::debug({"Lifecycle"}, "GLFW terminate");
			try{
				backend::glfw::terminate();
			} catch(...){
				log::error({"Lifecycle"}, "Exception during GLFW termination");
			}
			glfw_initialized_ = false;
		}

		if(font_initialized_){
			log::debug({"Lifecycle"}, "Font terminate");
			try{
				font::terminate();
			} catch(...){
				log::error({"Lifecycle"}, "Exception during font termination");
			}
			font_initialized_ = false;
		}

		if(platform_initialized_){
			log::debug({"Lifecycle"}, "Platform terminate");
			try{
				platform::terminate();
			} catch(...){
				log::error({"Lifecycle"}, "Exception during platform termination");
			}
			platform_initialized_ = false;
		}

		log::info({"Lifecycle"}, "Platform subsystems shutdown complete");
	}
};

}
