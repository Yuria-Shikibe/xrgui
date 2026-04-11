

export module mo_yanxi.gui.util.task_queue;

import std;
import mo_yanxi.gui.alloc;
import mo_yanxi.concurrent.mpsc_double_buffer;
import mo_yanxi.concurrent.mpsc_queue;
import mo_yanxi.allocator_aware_unique_ptr;
import mo_yanxi.call_stream;

namespace mo_yanxi::gui{
export
struct associated_async_sync_task_queue_base{
protected:
	struct task_entry{
		void* e;
		std::move_only_function<void(void*)> func;

		void exec(){
			func(e);
		}
	};

	using container = mr::heap_vector<task_entry>;

	ccur::mpsc_double_buffer<task_entry, container> async_tasks_{};

public:
	[[nodiscard]] explicit associated_async_sync_task_queue_base(const container::allocator_type& alloc)
		: async_tasks_(alloc){
	}

	void consume(){
		if(auto ts = async_tasks_.fetch()){
			for (auto&& t : *ts){
				t.exec();
			}
		}
	}

	void clear(){
		async_tasks_.clear();
	}

protected:
	void merge(associated_async_sync_task_queue_base&& other){
		async_tasks_.merge(std::move(other).async_tasks_);
	}
};

export
template <typename T>
struct associated_async_sync_task_queue : associated_async_sync_task_queue_base{
	using owner_type = T;

	using associated_async_sync_task_queue_base::associated_async_sync_task_queue_base;

	template <std::derived_from<owner_type> E, std::invocable<E&> Fn>
	void post(E& e, Fn&& fn){
		async_tasks_.emplace(std::addressof(e), [f = std::forward<Fn>(fn)](void* e) mutable {
			std::invoke(f, *static_cast<E*>(e));
		});
	}

	template <std::derived_from<owner_type> E, std::invocable<> Fn>
	void post(E& e, Fn&& fn){
		async_tasks_.emplace(std::addressof(e), [f = std::forward<Fn>(fn)](void* e) mutable {
			std::invoke(f);
		});
	}

	void erase(const owner_type* e) noexcept {
		async_tasks_.modify([&](container& c) noexcept {
			std::erase_if(c, [&](const container::value_type& v) noexcept {
				return v.e == e;
			});
		});
	}

	using associated_async_sync_task_queue::merge;

};


export
template <typename ...CtxArgs>
struct async_sync_task_queue{
private:
	using func = std::move_only_function<void(CtxArgs...)>;
	using container = mr::heap_vector<func>;
	ccur::mpsc_double_buffer<func, container> async_tasks_{};

public:
	[[nodiscard]] explicit async_sync_task_queue(const container::allocator_type& alloc) : async_tasks_(alloc){
	}

	template <std::invocable<CtxArgs...> Fn>
	void post(Fn&& fn){
		async_tasks_.emplace([f = std::forward<Fn>(fn)](CtxArgs... args){
			std::invoke(f, std::forward<CtxArgs>(args)...);
		});
	}

	void consume(CtxArgs... args){
		if(auto ts = async_tasks_.fetch()){
			for (auto&& t : *ts){
				t(std::forward<CtxArgs>(args)...);
			}
		}
	}
};

export
struct call_stream_task_queue{
private:
	ccur::mpsc_double_buffer_heterogeneous<call_stream<mr::unvs_allocator<std::byte>>> async_tasks_{};

public:
	[[nodiscard]] call_stream_task_queue() = default;

	template <typename Fn, typename ...Args>
		requires (std::invocable<Fn&&, Args&&...>)
	void post(Fn&& fn, Args&&... args){
		async_tasks_.emplace_back(std::forward<Fn>(fn), std::forward<Args>(args)...);
	}

	void consume(){
		if(auto ts = async_tasks_.fetch()){
			ts->execute();
		}
	}

	void merge(call_stream_task_queue&& other){
		async_tasks_.merge(std::move(other).async_tasks_);
	}
};

}
