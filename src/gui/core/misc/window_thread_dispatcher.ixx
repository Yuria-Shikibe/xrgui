export module mo_yanxi.gui.window_thread_dispatcher;

import std;

namespace mo_yanxi::gui{
export
class window_thread_dispatcher{
private:
	std::thread::id window_thread_id_{std::this_thread::get_id()};
	std::mutex mutex_{};
	std::deque<std::move_only_function<void()>> pending_tasks_{};
	bool stopped_{false};

public:
	[[nodiscard]] window_thread_dispatcher() = default;

	window_thread_dispatcher(const window_thread_dispatcher&) = delete;
	window_thread_dispatcher& operator=(const window_thread_dispatcher&) = delete;

	[[nodiscard]] bool is_window_thread() const noexcept{
		return std::this_thread::get_id() == window_thread_id_;
	}

	void post(std::move_only_function<void()>&& task){
		if(!task){
			throw std::runtime_error{"window thread dispatcher received an empty task"};
		}

		std::scoped_lock lock{mutex_};
		if(stopped_){
			throw std::runtime_error{"window thread dispatcher is stopped"};
		}
		pending_tasks_.push_back(std::move(task));
	}

	void drain(){
		if(!is_window_thread()){
			throw std::runtime_error{"window thread dispatcher drained from a non-window thread"};
		}

		while(true){
			std::move_only_function<void()> task{};
			{
				std::scoped_lock lock{mutex_};
				if(pending_tasks_.empty()){
					return;
				}
				task = std::move(pending_tasks_.front());
				pending_tasks_.pop_front();
			}
			std::invoke(std::move(task));
		}
	}

	void stop() noexcept{
		std::scoped_lock lock{mutex_};
		stopped_ = true;
	}
};
}
