module;
#include <cassert>

export module mo_yanxi.thread_pool;

import std;
import mo_yanxi.concurrent.mpsc_queue;

//TODO this is complete an AI trash slop...

namespace mo_yanxi{

export
class thread_pool{
	ccur::mpsc_queue<std::move_only_function<void()>> queue_;
	std::vector<std::jthread> workers_;

public:
	[[nodiscard]] explicit thread_pool(
		std::size_t n = std::max(std::size_t{1}, static_cast<std::size_t>(std::thread::hardware_concurrency()) - 1)){
		workers_.reserve(n);
		for(std::size_t i = 0; i < n; ++i){
			workers_.emplace_back([this](std::stop_token stop){
				while(!stop.stop_requested()){
					if(auto fn = queue_.consume([&]{ return stop.stop_requested(); })){
						std::invoke(*fn);
					}
				}
			});
		}
	}

	~thread_pool(){
		for(auto& w : workers_) w.request_stop();
		for(std::size_t i = 0; i < workers_.size(); ++i) queue_.notify();
	}

	// satisfies async_endpoint_for concept (task_queue.ixx)
	[[nodiscard]] bool try_post(std::move_only_function<void()> fn){
		queue_.push(std::move(fn));
		return true;
	}
};

}
