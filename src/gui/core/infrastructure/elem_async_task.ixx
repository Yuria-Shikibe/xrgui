module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.infrastructure:async_task;

import std;
export import mo_yanxi.gui.util.task_queue;
import mo_yanxi.allocator_aware_unique_ptr;
import mo_yanxi.concurrent.mpsc_queue;
import mo_yanxi.log;
import :elem_ptr;

namespace mo_yanxi::gui{
std::thread::id exchange_scene_thread(scene& s, std::thread::id id);

export
struct async_task_context{
private:
	std::stop_token owner_stop_token_{};
	std::stop_token task_stop_token_{};
	async_operation_state* runtime_state_{};

public:
	[[nodiscard]] async_task_context() = default;

	[[nodiscard]] inline async_task_context(
		std::stop_token owner_stop_token,
		std::stop_token task_stop_token,
		async_operation_state* runtime_state) noexcept
		: owner_stop_token_(std::move(owner_stop_token)),
		  task_stop_token_(std::move(task_stop_token)),
		  runtime_state_(runtime_state){
	}

	[[nodiscard]] inline bool stop_requested() const noexcept{
		return owner_stop_token_.stop_requested() || task_stop_token_.stop_requested();
	}

	[[nodiscard]] inline std::stop_token owner_stop_token() const noexcept{
		return owner_stop_token_;
	}

	[[nodiscard]] inline std::stop_token task_stop_token() const noexcept{
		return task_stop_token_;
	}

	inline void report_progress(unsigned current, unsigned total) const noexcept{
		if(runtime_state_ == nullptr){
			return;
		}
		runtime_state_->report_progress(current, total);
	}
};

namespace scene_submodule{

template <typename Reply, typename Result, bool IsVoid = std::is_void_v<Result>>
struct async_reply_object_for_impl;

template <typename Reply, typename Result>
struct async_reply_object_for_impl<Reply, Result, false> : std::bool_constant<
	!std::is_reference_v<Result>
	&& requires(Reply& reply, Result result, std::exception_ptr exception){
		std::move(reply).set_value(std::move(result));
		std::move(reply).set_error(std::move(exception));
		std::move(reply).set_cancelled();
	}> {};

template <typename Reply, typename Result>
struct async_reply_object_for_impl<Reply, Result, true> : std::bool_constant<
	requires(Reply& reply, std::exception_ptr exception){
		std::move(reply).set_value();
		std::move(reply).set_error(std::move(exception));
		std::move(reply).set_cancelled();
	}> {};

template <typename Reply, typename Result>
concept async_reply_object_for = async_reply_object_for_impl<std::decay_t<Reply>, Result>::value;

template <typename ValueFn, typename E, typename Result, bool IsVoid = std::is_void_v<Result>>
struct gui_reply_value_callback_for_impl;

template <typename ValueFn, typename E, typename Result>
struct gui_reply_value_callback_for_impl<ValueFn, E, Result, false> : std::bool_constant<
	std::invocable<std::decay_t<ValueFn>&&, E&, Result>
	> {};

template <typename ValueFn, typename E, typename Result>
struct gui_reply_value_callback_for_impl<ValueFn, E, Result, true> : std::bool_constant<
	std::invocable<std::decay_t<ValueFn>&&, E&>
	> {};

template <typename ValueFn, typename E, typename Result>
concept gui_reply_value_callback_for = gui_reply_value_callback_for_impl<ValueFn, E, Result>::value;

template <typename ErrorFn, typename E>
concept gui_reply_error_callback_for =
	std::is_null_pointer_v<std::decay_t<ErrorFn>>
	|| std::invocable<std::decay_t<ErrorFn>&&, E&, std::exception_ptr>;

template <typename CancelFn, typename E>
concept gui_reply_cancel_callback_for =
	std::is_null_pointer_v<std::decay_t<CancelFn>>
	|| std::invocable<std::decay_t<CancelFn>&&, E&>;

template <typename ProcessFn>
using forked_scene_process_result_t = std::invoke_result_t<std::decay_t<ProcessFn>&, async_task_context&, scene&>;

struct scene_deleter{
	void operator()(scene* ptr) const noexcept;
};

}

export
struct async_operation_binding{
private:
	elem_ref<> owner_ref_{};
	std::stop_token owner_stop_token_{};
	async_operation_state_ptr runtime_state_{};

	inline void cancel_pending_noexcept_() noexcept{
		if(runtime_state_ && runtime_state_->is_pending()){
			runtime_state_->mark_cancelled();
		}
	}

public:
	[[nodiscard]] async_operation_binding() = default;

	[[nodiscard]] inline async_operation_binding(
		elem_ref<> owner_ref,
		std::stop_token owner_stop_token,
		async_operation_state_ptr runtime_state = {}) noexcept
		: owner_ref_(std::move(owner_ref)),
		  owner_stop_token_(std::move(owner_stop_token)),
		  runtime_state_(std::move(runtime_state)){
	}

	inline ~async_operation_binding() noexcept{
		cancel_pending_noexcept_();
	}

	async_operation_binding(const async_operation_binding&) = delete;
	async_operation_binding& operator=(const async_operation_binding&) = delete;

	[[nodiscard]] async_operation_binding(async_operation_binding&& other) noexcept = default;

	inline async_operation_binding& operator=(async_operation_binding&& other) noexcept{
		if(this != std::addressof(other)){
			cancel_pending_noexcept_();
			owner_ref_ = std::move(other.owner_ref_);
			owner_stop_token_ = std::move(other.owner_stop_token_);
			runtime_state_ = std::move(other.runtime_state_);
		}
		return *this;
	}

	[[nodiscard]] inline elem* owner() const noexcept{
		return owner_ref_.get_live();
	}

	[[nodiscard]] inline bool owned_by(const elem* owner) const noexcept{
		return owner_ref_.get_retained() == owner;
	}

	[[nodiscard]] inline bool stop_requested() const noexcept{
		return owner_stop_token_.stop_requested()
			|| (runtime_state_ && runtime_state_->stop_requested());
	}

	[[nodiscard]] inline async_operation_handle handle() const noexcept{
		return async_operation_handle{runtime_state_};
	}

	[[nodiscard]] inline async_task_context make_context() const noexcept{
		return async_task_context{
			owner_stop_token_,
			runtime_state_ ? runtime_state_->stop_token() : std::stop_token{},
			runtime_state_.get()
		};
	}

	inline void mark_completed() noexcept{
		if(runtime_state_ && runtime_state_->is_pending()){
			runtime_state_->mark_completed();
		}
	}

	inline void mark_failed() noexcept{
		if(runtime_state_ && runtime_state_->is_pending()){
			runtime_state_->mark_failed();
		}
	}

	inline void mark_cancelled() noexcept{
		if(runtime_state_ && runtime_state_->is_pending()){
			runtime_state_->mark_cancelled();
		}
	}
};

namespace detail{

template <typename Work, typename... Args>
decltype(auto) invoke_async_bound_work(
	Work& work,
	async_operation_binding& binding,
	async_task_context& context,
	Args&&... args){
	if constexpr(std::invocable<Work&, async_operation_binding&, async_task_context&, Args&&...>){
		return std::invoke(work, binding, context, std::forward<Args>(args)...);
	}else if constexpr(std::invocable<Work&, async_task_context&, Args&&...>){
		return std::invoke(work, context, std::forward<Args>(args)...);
	}else{
		return std::invoke(work, std::forward<Args>(args)...);
	}
}

template <typename Reply>
void cancel_async_reply(async_operation_binding& binding, Reply& reply){
	binding.mark_cancelled();
	std::move(reply).set_cancelled();
}

template <typename Reply>
void fail_async_reply(async_operation_binding& binding, Reply& reply){
	binding.mark_failed();
	std::move(reply).set_error(std::current_exception());
}

template <typename Reply>
bool try_complete_async_reply(async_operation_binding& binding, async_task_context& context, Reply& reply){
	if(context.stop_requested()){
		detail::cancel_async_reply(binding, reply);
		return false;
	}

	binding.mark_completed();
	return true;
}

template <typename Work, typename Reply, typename... Args>
void complete_async_request(
	async_operation_binding& binding,
	async_task_context& context,
	Work& work,
	Reply& reply,
	Args&&... args){
	using result_type = decltype(detail::invoke_async_bound_work(
		work,
		binding,
		context,
		std::forward<Args>(args)...));

	if constexpr(std::is_void_v<result_type>){
		try{
			detail::invoke_async_bound_work(work, binding, context, std::forward<Args>(args)...);
		}catch(...){
			detail::fail_async_reply(binding, reply);
			return;
		}

		if(detail::try_complete_async_reply(binding, context, reply)){
			std::move(reply).set_value();
		}
	}else{
		static_assert(!std::is_reference_v<result_type>);
		std::optional<result_type> result{};
		try{
			result.emplace(detail::invoke_async_bound_work(work, binding, context, std::forward<Args>(args)...));
		}catch(...){
			detail::fail_async_reply(binding, reply);
			return;
		}

		if(detail::try_complete_async_reply(binding, context, reply)){
			std::move(reply).set_value(std::move(*result));
		}
	}
}

}

export
template <typename Endpoint, typename Fn>
	requires (
		std::invocable<std::decay_t<Fn>&, async_operation_binding&, async_task_context&>
		|| std::invocable<std::decay_t<Fn>&, async_task_context&>
		|| std::invocable<std::decay_t<Fn>&>)
[[nodiscard]] bool async_send(Endpoint& endpoint, async_operation_binding binding, Fn&& fn){
	if(binding.stop_requested()){
		binding.mark_cancelled();
		return false;
	}

	return ::mo_yanxi::gui::async_send(endpoint, [
		binding = std::move(binding),
		fn = std::forward<Fn>(fn)
	]() mutable {
		auto context = binding.make_context();
		if(context.stop_requested()){
			binding.mark_cancelled();
			return;
		}

		try{
			if constexpr(std::invocable<std::decay_t<Fn>&, async_operation_binding&, async_task_context&>){
				std::invoke(fn, binding, context);
			}else if constexpr(std::invocable<std::decay_t<Fn>&, async_task_context&>){
				std::invoke(fn, context);
			}else{
				std::invoke(fn);
			}
		}catch(...){
			binding.mark_failed();
			throw;
		}

		if(context.stop_requested()){
			binding.mark_cancelled();
		}else{
			binding.mark_completed();
		}
	});
}

export
template <typename Endpoint, typename Work, typename Reply>
[[nodiscard]] bool async_request(Endpoint& endpoint, async_operation_binding binding, Work&& work, Reply&& reply){
	if(binding.stop_requested()){
		detail::cancel_async_reply(binding, reply);
		return false;
	}

	return ::mo_yanxi::gui::async_send(endpoint, [
		binding = std::move(binding),
		work = std::forward<Work>(work),
		reply = std::forward<Reply>(reply)
	]<typename... Ty>(Ty&&... args) mutable {
		auto context = binding.make_context();
		if(context.stop_requested()){
			detail::cancel_async_reply(binding, reply);
			return;
		}

		detail::complete_async_request(binding, context, work, reply, std::forward<Ty>(args)...);
	});
}

export
/**
 * @brief One-shot request object used by native clipboard implementations.
 *
 * Window-thread code receives this object, obtains the clipboard text, then
 * calls `std::move(request).set_value(text)`. The completion is routed back to
 * the GUI scene through an owner-bound callback.
 */
struct native_clipboard_request{
	async_operation_binding binding{};
	async_reply<std::string> reply{};

	[[nodiscard]] inline async_operation_handle handle() const noexcept{
		return binding.handle();
	}

	inline void set_value(std::string text) &&{
		if(binding.stop_requested()){
			binding.mark_cancelled();
			std::move(reply).set_cancelled();
			return;
		}
		binding.mark_completed();
		std::move(reply).set_value(std::move(text));
	}

	inline void set_error(std::exception_ptr exception) &&{
		binding.mark_failed();
		std::move(reply).set_error(std::move(exception));
	}

	inline void set_cancelled() &&{
		binding.mark_cancelled();
		std::move(reply).set_cancelled();
	}
};

struct forked_scene_task_base{
private:
	async_operation_binding binding_{};
	std::exception_ptr exception_{};

protected:
	virtual void process_impl(async_task_context& context, scene& async_scene) = 0;

public:
	virtual ~forked_scene_task_base() = default;

	[[nodiscard]] forked_scene_task_base() = default;

	forked_scene_task_base(const forked_scene_task_base&) = delete;
	forked_scene_task_base& operator=(const forked_scene_task_base&) = delete;
	[[nodiscard]] forked_scene_task_base(forked_scene_task_base&&) noexcept = default;
	forked_scene_task_base& operator=(forked_scene_task_base&&) noexcept = default;

	inline void bind_async_operation(async_operation_binding binding) noexcept{
		binding_ = std::move(binding);
	}

	[[nodiscard]] inline elem* owner() const noexcept{
		return binding_.owner();
	}

	[[nodiscard]] inline bool owned_by(const elem* owner) const noexcept{
		return binding_.owned_by(owner);
	}

	[[nodiscard]] inline bool stop_requested() const noexcept{
		return binding_.stop_requested();
	}

	[[nodiscard]] inline bool has_exception() const noexcept{
		return exception_ != nullptr;
	}

	[[nodiscard]] inline std::exception_ptr exception() const noexcept{
		return exception_;
	}

	inline void mark_finished() noexcept{
		if(this->stop_requested()){
			binding_.mark_cancelled();
		}else if(this->has_exception()){
			binding_.mark_failed();
		}else{
			binding_.mark_completed();
		}
	}

	inline void mark_cancelled() noexcept{
		binding_.mark_cancelled();
	}

	inline void process(scene& async_scene){
		async_task_context context = binding_.make_context();
		if(context.stop_requested()){
			return;
		}
		try{
			this->process_impl(context, async_scene);
		}catch(...){
			exception_ = std::current_exception();
		}
	}

	virtual void on_done(elem& owner, scene& async_scene) = 0;

	virtual bool on_error(elem& owner, scene& async_scene, std::exception_ptr exception){
		(void)owner;
		(void)async_scene;
		(void)exception;
		return false;
	}
};

namespace scene_submodule{

template <typename Result>
struct forked_scene_task_result{
	static_assert(std::is_object_v<Result>);

private:
	std::optional<Result> result_{};

public:
	template <typename ProcessFn>
	void process(ProcessFn& process_fn, async_task_context& context, scene& async_scene){
		result_.emplace(std::invoke(process_fn, context, async_scene));
	}

	template <typename Reply>
	void complete(Reply& reply){
		if(result_.has_value()){
			std::move(reply).set_value(std::move(*result_));
		}else{
			std::move(reply).set_cancelled();
		}
	}
};

template <>
struct forked_scene_task_result<void>{
private:
	bool processed_{false};

public:
	template <typename ProcessFn>
	void process(ProcessFn& process_fn, async_task_context& context, scene& async_scene){
		std::invoke(process_fn, context, async_scene);
		processed_ = true;
	}

	template <typename Reply>
	void complete(Reply& reply){
		if(processed_){
			std::move(reply).set_value();
		}else{
			std::move(reply).set_cancelled();
		}
	}
};

template <
	typename ProcessFn,
	typename Reply,
	typename Result = std::invoke_result_t<ProcessFn&, async_task_context&, scene&>>
struct forked_scene_request_task : forked_scene_task_base{
	ADAPTED_NO_UNIQUE_ADDRESS ProcessFn process_fn_;
	ADAPTED_NO_UNIQUE_ADDRESS Reply reply_;
	forked_scene_task_result<Result> result_{};

	[[nodiscard]] forked_scene_request_task(ProcessFn process_fn, Reply reply)
		: process_fn_(std::move(process_fn)),
		  reply_(std::move(reply)){
	}

	void process_impl(async_task_context& context, scene& async_scene) override{
		result_.process(process_fn_, context, async_scene);
	}

	void on_done(elem&, scene&) override{
		result_.complete(reply_);
	}

	bool on_error(elem&, scene&, std::exception_ptr exception) override{
		std::move(reply_).set_error(std::move(exception));
		return true;
	}
};

struct forked_scene_worker{
	using async_task_ptr = allocator_aware_poly_unique_ptr<forked_scene_task_base, mr::heap_allocator<forked_scene_task_base>>;

private:
	ccur::mpsc_queue<async_task_ptr, std::deque<
			async_task_ptr, mr::heap_allocator<async_task_ptr>
		>> async_tasks_pending_{};
	mr::heap_allocator<forked_scene_task_base> task_allocator_{};

	std::atomic<forked_scene_task_base*> current_done_task_{};
	async_operation_state_pool_ptr task_runtime_state_pool_{};
	std::unique_ptr<scene, scene_deleter> forked_scene_{};
	std::jthread async_task_thread_{};

public:
	inline scene& get_scene(){
		return *forked_scene_;
	}

	[[nodiscard]] inline forked_scene_worker(const mr::heap_allocator<async_task_ptr>& alloc,
	                                     std::unique_ptr<scene, scene_deleter>&& scene)
		:
		async_tasks_pending_(alloc),
		task_allocator_(alloc),
		task_runtime_state_pool_(std::in_place, mr::heap_allocator<>{alloc}),
		forked_scene_((assert(scene != nullptr), std::move(scene))),
		async_task_thread_([this](std::stop_token&& stop_token){
			exchange_scene_thread(*forked_scene_, std::this_thread::get_id());
			async_tasks_process(std::move(stop_token));
		}){
	}

	[[nodiscard]] inline std::jthread& get_async_task_thread() noexcept{
		return async_task_thread_;
	}

	inline ~forked_scene_worker(){
		if(task_runtime_state_pool_){
			task_runtime_state_pool_->cancel_all();
		}
		async_task_thread_.request_stop();
		async_tasks_pending_.notify();

		current_done_task_.store(nullptr, std::memory_order_relaxed);
		current_done_task_.notify_one();

		if(async_task_thread_.joinable()
			&& async_task_thread_.get_id() != std::this_thread::get_id()){
			async_task_thread_.join();
		}
		async_tasks_pending_.clear();
		current_done_task_.store(nullptr, std::memory_order_relaxed);
		exchange_scene_thread(*forked_scene_, std::this_thread::get_id());
	}

private:
	inline static void log_async_task_exception(std::exception_ptr exception){
		if(exception == nullptr){
			return;
		}
		try{
			std::rethrow_exception(std::move(exception));
		}catch(const std::exception& e){
			log::error({"GUI"}, "elem async task failed: {}", e.what());
		}catch(...){
			log::error({"GUI"}, "elem async task failed with an unknown exception");
		}
	}

	[[nodiscard]] inline async_operation_state_ptr create_task_runtime_state(){
		if(!task_runtime_state_pool_){
			throw std::runtime_error{"async operation state pool is unavailable"};
		}
		return task_runtime_state_pool_->create_operation();
	}

	inline void async_tasks_process(std::stop_token stop_token){
		while(!stop_token.stop_requested()){
			auto task = async_tasks_pending_.consume([&] noexcept {
				return stop_token.stop_requested();
			});

			if(task){
				(*task)->process(*forked_scene_);
				auto addr = std::to_address(*task);
				current_done_task_.store(addr, std::memory_order_release);
				if(stop_token.stop_requested())break;
				current_done_task_.wait(addr, std::memory_order_relaxed);
			}
		}
	}

public:
	inline void process_done(){
		if(auto task = current_done_task_.load(std::memory_order_acquire)){
			auto thread = std::this_thread::get_id();
			auto last = exchange_scene_thread(*forked_scene_, thread);
			std::exception_ptr ui_exception{};
			try{
				elem* owner = task->owner();
				if(owner != nullptr && !task->stop_requested()){
					if(task->has_exception()){
						if(!task->on_error(*owner, *forked_scene_, task->exception())){
							log_async_task_exception(task->exception());
						}
					}else{
						task->on_done(*owner, *forked_scene_);
					}
				}else if(task->has_exception()){
					log_async_task_exception(task->exception());
				}
			}catch(...){
				ui_exception = std::current_exception();
			}
			task->mark_finished();
			exchange_scene_thread(*forked_scene_, last);
			current_done_task_.store(nullptr, std::memory_order_relaxed);
			current_done_task_.notify_one();
			if(ui_exception != nullptr){
				std::rethrow_exception(ui_exception);
			}
		}
	}

	inline void cancel_owner(const elem* owner) noexcept{
		if(owner == nullptr){
			return;
		}
		async_tasks_pending_.erase_if([owner](const async_task_ptr& task) noexcept {
			if(!task || !task->owned_by(owner)){
				return false;
			}
			task->mark_cancelled();
			return true;
		});
	}

	template <std::derived_from<elem> E, std::derived_from<forked_scene_task_base> Task>
	async_operation_handle post(E& e, Task&& submitted_task){
		using task_type = std::remove_cvref_t<Task>;
		auto task = task_type{std::forward<Task>(submitted_task)};
		elem_ref<> owner_ref{e};
		async_operation_state_ptr task_runtime_state{};
		try{
			task_runtime_state = this->create_task_runtime_state();
			task.bind_async_operation(async_operation_binding{
				std::move(owner_ref),
				elem_ref_access::stop_token(std::addressof(e)),
				task_runtime_state
			});
			async_tasks_pending_.push(
				mo_yanxi::make_allocate_aware_poly_unique<task_type, forked_scene_task_base>(
					task_allocator_, std::move(task)));
		}catch(...){
			if(task_runtime_state){
				task_runtime_state->mark_cancelled();
			}
			throw;
		}
		return async_operation_handle{std::move(task_runtime_state)};
	}
};

}

}
