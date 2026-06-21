module;

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.util.task_queue;

import std;
import mo_yanxi.gui.alloc;
import mo_yanxi.concurrent.mpsc_double_buffer;
import mo_yanxi.concurrent.mpsc_queue;
import mo_yanxi.allocator_aware_unique_ptr;
import mo_yanxi.call_stream;

namespace mo_yanxi::gui{
export
enum struct async_operation_status : std::uint8_t{
	pending,
	completed,
	failed,
	cancelled
};

export
struct async_progress{
	unsigned current{};
	unsigned total{};
};

export
struct async_operation_state{
private:
	std::stop_source stop_source_{};
	std::atomic<std::uint64_t> progress_{};
	std::atomic<async_operation_status> status_{async_operation_status::pending};

	[[nodiscard]] static std::uint64_t pack_progress(unsigned current, unsigned total) noexcept{
		return (static_cast<std::uint64_t>(total) << 32u) | static_cast<std::uint64_t>(current);
	}

	[[nodiscard]] static async_progress unpack_progress(std::uint64_t value) noexcept{
		return {
			.current = static_cast<unsigned>(value & 0xffff'ffffull),
			.total = static_cast<unsigned>(value >> 32u)
		};
	}

	bool mark_status(async_operation_status next) noexcept{
		auto expected = async_operation_status::pending;
		return status_.compare_exchange_strong(
			expected,
			next,
			std::memory_order_acq_rel,
			std::memory_order_acquire);
	}

public:
	void request_stop() noexcept{
		stop_source_.request_stop();
	}

	[[nodiscard]] bool stop_requested() const noexcept{
		return stop_source_.stop_requested();
	}

	[[nodiscard]] std::stop_token stop_token() const noexcept{
		return stop_source_.get_token();
	}

	[[nodiscard]] bool is_pending() const noexcept{
		return this->status() == async_operation_status::pending;
	}

	void report_progress(unsigned current, unsigned total) noexcept{
		if(total == 0u){
			current = 0u;
		}else{
			current = std::min(current, total);
		}
		progress_.store(async_operation_state::pack_progress(current, total), std::memory_order_release);
	}

	[[nodiscard]] async_progress progress_snapshot() const noexcept{
		return async_operation_state::unpack_progress(progress_.load(std::memory_order_acquire));
	}

	[[nodiscard]] async_operation_status status() const noexcept{
		return status_.load(std::memory_order_acquire);
	}

	void mark_completed() noexcept{
		(void)this->mark_status(async_operation_status::completed);
	}

	void mark_failed() noexcept{
		(void)this->mark_status(async_operation_status::failed);
	}

	void mark_cancelled() noexcept{
		this->request_stop();
		(void)this->mark_status(async_operation_status::cancelled);
	}
};

export
struct async_operation_handle{
private:
	async_operation_state* state_{};

	[[nodiscard]] bool valid_state() const noexcept{
		return state_ != nullptr
			&& state_->is_pending();
	}

public:
	[[nodiscard]] async_operation_handle() = default;

	[[nodiscard]] explicit async_operation_handle(async_operation_state& state) noexcept
		: state_(std::addressof(state)){
	}

	void request_stop() const noexcept{
		if(this->valid_state()){
			state_->request_stop();
		}
	}

	[[nodiscard]] bool stop_requested() const noexcept{
		return this->valid_state() && state_->stop_requested();
	}

	[[nodiscard]] async_progress progress_snapshot() const noexcept{
		if(state_ == nullptr){
			return {};
		}
		return state_->progress_snapshot();
	}

	[[nodiscard]] unsigned progress() const noexcept{
		return this->progress_snapshot().current;
	}

	[[nodiscard]] unsigned progress_total() const noexcept{
		return this->progress_snapshot().total;
	}

	[[nodiscard]] float progress_ratio() const noexcept{
		const auto progress = this->progress_snapshot();
		if(progress.total == 0u){
			return 0.f;
		}
		return static_cast<float>(progress.current) / static_cast<float>(progress.total);
	}

	[[nodiscard]] async_operation_status status() const noexcept{
		if(state_ == nullptr){
			return async_operation_status::cancelled;
		}
		return state_->status();
	}
};

namespace detail{

inline constexpr std::size_t inline_bytes = 48;
inline constexpr std::size_t inline_align = alignof(std::max_align_t);

struct noop_callback{
	template <typename... Args>
	constexpr void operator()(Args&&...) const noexcept{
	}
};

template <typename Fn>
using normalized_callback_t = std::conditional_t<
	std::same_as<std::decay_t<Fn>, std::nullptr_t>,
	noop_callback,
	std::decay_t<Fn>>;

template <typename Fn>
[[nodiscard]] decltype(auto) normalize_callback(Fn&& fn){
	if constexpr(std::same_as<std::decay_t<Fn>, std::nullptr_t>){
		(void)fn;
		return detail::noop_callback{};
	}else{
		return std::forward<Fn>(fn);
	}
}

template <typename T, typename ValueFn, typename ErrorFn, typename CancelFn>
struct split_reply_handler{
	static_assert(std::is_void_v<T> || !std::is_reference_v<T>, "async_reply<T> cannot store reference results");

	ADAPTED_NO_UNIQUE_ADDRESS ValueFn value_fn_;
	ADAPTED_NO_UNIQUE_ADDRESS ErrorFn error_fn_;
	ADAPTED_NO_UNIQUE_ADDRESS CancelFn cancel_fn_;

	template <typename ValueArg, typename ErrorArg, typename CancelArg>
		requires std::constructible_from<ValueFn, ValueArg&&>
		      && std::constructible_from<ErrorFn, ErrorArg&&>
		      && std::constructible_from<CancelFn, CancelArg&&>
	[[nodiscard]] explicit split_reply_handler(ValueArg&& value_fn, ErrorArg&& error_fn, CancelArg&& cancel_fn)
		: value_fn_(std::forward<ValueArg>(value_fn)),
		  error_fn_(std::forward<ErrorArg>(error_fn)),
		  cancel_fn_(std::forward<CancelArg>(cancel_fn)){
	}

	void value() && requires std::is_void_v<T>{
		std::invoke(std::move(value_fn_));
	}

	template <typename U>
		requires (!std::is_void_v<T> && std::constructible_from<T, U&&>)
	void value(U&& value) &&{
		std::invoke(std::move(value_fn_), std::forward<U>(value));
	}

	void error(std::exception_ptr exception) &&{
		std::invoke(std::move(error_fn_), std::move(exception));
	}

	void cancel() &&{
		std::invoke(std::move(cancel_fn_));
	}
};

template <typename T, typename Handler>
struct reply_model{
	ADAPTED_NO_UNIQUE_ADDRESS Handler handler_;

	template <typename... Args>
		requires std::constructible_from<Handler, Args&&...>
	[[nodiscard]] explicit reply_model(Args&&... args)
		: handler_(std::forward<Args>(args)...){
	}

	void set_value(void* value){
		if constexpr(std::is_void_v<T>){
			std::move(handler_).value();
		}else{
			std::move(handler_).value(std::move(*static_cast<T*>(value)));
		}
	}

	void set_error(std::exception_ptr exception){
		std::move(handler_).error(std::move(exception));
	}

	void set_cancelled(){
		std::move(handler_).cancel();
	}
};

template <typename T, typename Handler>
struct reply_handler_for_impl : std::bool_constant<requires(Handler& handler, T&& value, std::exception_ptr exception){
	std::move(handler).value(std::forward<T>(value));
	std::move(handler).error(exception);
	std::move(handler).cancel();
}> {};

template <typename Handler>
struct reply_handler_for_impl<void, Handler> : std::bool_constant<requires(Handler& handler, std::exception_ptr exception){
	std::move(handler).value();
	std::move(handler).error(exception);
	std::move(handler).cancel();
}> {};

template <typename T, typename Handler>
concept reply_handler_for = reply_handler_for_impl<T, std::remove_cvref_t<Handler>>::value;

enum struct reply_lifecycle_op : std::uint8_t{
	destroy_inline,
	delete_heap,
	relocate_inline
};

enum struct reply_complete_op : std::uint8_t{
	value,
	error,
	cancelled
};

struct reply_ops{
	void (*lifecycle)(reply_lifecycle_op, void*, void*) noexcept;
	void (*complete)(reply_complete_op, void*, void*);
};

template <typename Model>
inline constexpr reply_ops reply_ops_for{
	.lifecycle = +[](reply_lifecycle_op op, void* target, void* source) noexcept{
		switch(op){
		case reply_lifecycle_op::destroy_inline:
			std::destroy_at(std::launder(static_cast<Model*>(target)));
			break;
		case reply_lifecycle_op::delete_heap:
			delete std::launder(static_cast<Model*>(target));
			break;
		case reply_lifecycle_op::relocate_inline:
			{
				auto* typed_source = std::launder(static_cast<Model*>(source));
				std::construct_at(static_cast<Model*>(target), std::move(*typed_source));
				std::destroy_at(typed_source);
			}
			break;
		}
	},
	.complete = +[](reply_complete_op op, void* object, void* payload){
		auto* typed_object = std::launder(static_cast<Model*>(object));
		switch(op){
		case reply_complete_op::value:
			std::invoke(&Model::set_value, *typed_object, payload);
			break;
		case reply_complete_op::error:
			std::invoke(&Model::set_error, *typed_object, std::move(*static_cast<std::exception_ptr*>(payload)));
			break;
		case reply_complete_op::cancelled:
			std::invoke(&Model::set_cancelled, *typed_object);
			break;
		}
	}
};

template <typename Model>
inline constexpr bool stores_inline_v =
	sizeof(Model) <= detail::inline_bytes
	&& alignof(Model) <= detail::inline_align
	&& std::is_nothrow_move_constructible_v<Model>;

union storage{
	void* heap_object_;
	alignas(detail::inline_align) std::byte inline_object_[detail::inline_bytes];

	constexpr storage() noexcept : heap_object_(nullptr){
	}
};

}

export
template <typename T>
struct async_reply{
private:
	using ops_type = detail::reply_ops;

	static_assert(std::is_void_v<T> || std::is_object_v<T>);

	detail::storage storage_{};
	const ops_type* ops_{};
	bool heap_storage_{false};

	[[nodiscard]] void* inline_object() noexcept{
		return storage_.inline_object_;
	}

	[[nodiscard]] const void* inline_object() const noexcept{
		return storage_.inline_object_;
	}

	[[nodiscard]] void* object() noexcept{
		return heap_storage_ ? storage_.heap_object_ : this->inline_object();
	}

	struct pending_model{
		const ops_type* ops{};
		void* object{};
		bool heap_storage{};

		[[nodiscard]] pending_model() = default;

		[[nodiscard]] pending_model(const ops_type* target_ops, void* target_object, bool target_heap_storage) noexcept
			: ops(target_ops),
			  object(target_object),
			  heap_storage(target_heap_storage){
		}

		pending_model(const pending_model&) = delete;
		pending_model& operator=(const pending_model&) = delete;

		[[nodiscard]] pending_model(pending_model&& other) noexcept
			: ops(std::exchange(other.ops, nullptr)),
			  object(std::exchange(other.object, nullptr)),
			  heap_storage(std::exchange(other.heap_storage, false)){
		}

		pending_model& operator=(pending_model&& other) noexcept{
			if(this != std::addressof(other)){
				this->reset();
				ops = std::exchange(other.ops, nullptr);
				object = std::exchange(other.object, nullptr);
				heap_storage = std::exchange(other.heap_storage, false);
			}
			return *this;
		}

		~pending_model() noexcept{
			this->reset();
		}

		void reset() noexcept{
			if(ops == nullptr){
				return;
			}
			if(heap_storage){
				ops->lifecycle(detail::reply_lifecycle_op::delete_heap, object, nullptr);
			}else{
				ops->lifecycle(detail::reply_lifecycle_op::destroy_inline, object, nullptr);
			}
			ops = nullptr;
			object = nullptr;
			heap_storage = false;
		}

		[[nodiscard]] explicit operator bool() const noexcept{
			return ops != nullptr;
		}
	};

	[[nodiscard]] pending_model release_pending() noexcept{
		if(ops_ == nullptr){
			return {};
		}
		pending_model result{ops_, this->object(), heap_storage_};
		if(heap_storage_){
			storage_.heap_object_ = nullptr;
		}
		ops_ = nullptr;
		heap_storage_ = false;
		return result;
	}

	template <typename Fn>
	void consume_and_invoke(Fn&& fn){
		auto pending = this->release_pending();
		if(!pending){
			return;
		}
		std::invoke(std::forward<Fn>(fn), *pending.ops, pending.object);
	}

	template <typename Handler, typename... Args>
	void emplace_model(Args&&... args){
		using model_type = detail::reply_model<T, Handler>;
		static_assert(std::constructible_from<model_type, Args&&...>);
		if constexpr(detail::stores_inline_v<model_type>){
			std::construct_at(static_cast<model_type*>(this->inline_object()), std::forward<Args>(args)...);
			heap_storage_ = false;
		}else{
			storage_.heap_object_ = new model_type(std::forward<Args>(args)...);
			heap_storage_ = true;
		}
		ops_ = std::addressof(detail::reply_ops_for<model_type>);
	}

	void cancel_pending_noexcept() noexcept{
		try{
			std::move(*this).set_cancelled();
		}catch(...){
		}
	}

	void move_from_(async_reply&& other) noexcept{
		if(other.ops_ == nullptr){
			return;
		}

		ops_ = other.ops_;
		heap_storage_ = other.heap_storage_;
		if(heap_storage_){
			storage_.heap_object_ = other.storage_.heap_object_;
			other.storage_.heap_object_ = nullptr;
		}else{
			ops_->lifecycle(detail::reply_lifecycle_op::relocate_inline, this->inline_object(), other.inline_object());
		}
		other.ops_ = nullptr;
		other.heap_storage_ = false;
	}

public:
	[[nodiscard]] async_reply() = default;
	~async_reply() noexcept{
		this->cancel_pending_noexcept();
	}

	async_reply(const async_reply&) = delete;
	async_reply& operator=(const async_reply&) = delete;

	template <typename Handler>
		requires detail::reply_handler_for<T, Handler>
	[[nodiscard]] explicit async_reply(Handler&& handler){
		this->template emplace_model<std::remove_cvref_t<Handler>>(std::forward<Handler>(handler));
	}

	template <typename Handler, typename... Args>
		requires detail::reply_handler_for<T, Handler>
		      && std::constructible_from<Handler, Args&&...>
	[[nodiscard]] explicit async_reply(std::in_place_type_t<Handler>, Args&&... args){
		this->template emplace_model<Handler>(std::forward<Args>(args)...);
	}

	[[nodiscard]] async_reply(async_reply&& other) noexcept{
		this->move_from_(std::move(other));
	}

	async_reply& operator=(async_reply&& other) noexcept{
		if(this != std::addressof(other)){
			this->cancel_pending_noexcept();
			this->move_from_(std::move(other));
		}
		return *this;
	}

	template <typename Value>
	void set_value(Value&& value) && requires (!std::is_void_v<T> && std::constructible_from<T, Value&&>){
		T stored_value{std::forward<Value>(value)};
		this->consume_and_invoke([&](const ops_type& target_ops, void* target_object){
			target_ops.complete(detail::reply_complete_op::value, target_object, std::addressof(stored_value));
		});
	}

	void set_value() && requires std::is_void_v<T>{
		this->consume_and_invoke([](const ops_type& target_ops, void* target_object){
			target_ops.complete(detail::reply_complete_op::value, target_object, nullptr);
		});
	}

	void set_error(std::exception_ptr exception) &&{
		this->consume_and_invoke([&](const ops_type& target_ops, void* target_object){
			target_ops.complete(detail::reply_complete_op::error, target_object, std::addressof(exception));
		});
	}

	void set_cancelled() &&{
		this->consume_and_invoke([](const ops_type& target_ops, void* target_object){
			target_ops.complete(detail::reply_complete_op::cancelled, target_object, nullptr);
		});
	}
};

static_assert(!std::copy_constructible<async_reply<int>>);
static_assert(!std::is_copy_assignable_v<async_reply<int>>);
static_assert(std::is_nothrow_move_constructible_v<async_reply<int>>);
static_assert(std::is_nothrow_move_assignable_v<async_reply<int>>);
static_assert(std::is_nothrow_move_constructible_v<async_reply<void>>);
static_assert([]{
	struct handler{
		void value(int){
		}

		void error(std::exception_ptr){
		}

		void cancel(){
		}
	};

	return detail::reply_handler_for<int, handler>
		&& std::constructible_from<async_reply<int>, handler>;
}());
static_assert([]{
	struct handler{
		void value(){
		}

		void error(std::exception_ptr){
		}

		void cancel(){
		}
	};

	return detail::reply_handler_for<void, handler>
		&& std::constructible_from<async_reply<void>, handler>;
}());

export
template <typename T, typename ValueFn, typename ErrorFn = std::nullptr_t, typename CancelFn = std::nullptr_t>
[[nodiscard]] async_reply<T> make_async_reply(
	ValueFn&& value_fn,
	ErrorFn&& error_fn = nullptr,
	CancelFn&& cancel_fn = nullptr){
	using handler_type = detail::split_reply_handler<
		T,
		detail::normalized_callback_t<ValueFn>,
		detail::normalized_callback_t<ErrorFn>,
		detail::normalized_callback_t<CancelFn>>;
	return async_reply<T>{
		std::in_place_type<handler_type>,
		detail::normalize_callback(std::forward<ValueFn>(value_fn)),
		detail::normalize_callback(std::forward<ErrorFn>(error_fn)),
		detail::normalize_callback(std::forward<CancelFn>(cancel_fn))
	};
}

export
template <typename Endpoint, typename Fn>
concept async_endpoint_for = requires(Endpoint& endpoint, Fn&& fn){
	{ endpoint.try_post(std::forward<Fn>(fn)) } -> std::convertible_to<bool>;
};

export
template <typename Endpoint, typename Fn>
	requires async_endpoint_for<Endpoint, Fn>
[[nodiscard]] bool async_send(Endpoint& endpoint, Fn&& fn){
	return endpoint.try_post(std::forward<Fn>(fn));
}

export
template <typename Endpoint, typename Work, typename Reply>
[[nodiscard]] bool async_request(Endpoint& endpoint, Work&& work, Reply&& reply){
	return ::mo_yanxi::gui::async_send(endpoint, [
		work = std::forward<Work>(work),
		reply = std::forward<Reply>(reply)
	](auto&&... args) mutable {
		using result_type = std::invoke_result_t<decltype(work)&, decltype(args)...>;
		if constexpr(std::is_void_v<result_type>){
			try{
				std::invoke(work, std::forward<decltype(args)>(args)...);
			}catch(...){
				std::move(reply).set_error(std::current_exception());
				return;
			}
			std::move(reply).set_value();
		}else{
			static_assert(!std::is_reference_v<result_type>);
			std::optional<result_type> result{};
			try{
				result.emplace(std::invoke(work, std::forward<decltype(args)>(args)...));
			}catch(...){
				std::move(reply).set_error(std::current_exception());
				return;
			}
			std::move(reply).set_value(std::move(*result));
		}
	});
}

export
struct associated_async_sync_task_queue_base{
protected:
	struct task_entry{
		void* owner{};
		std::move_only_function<bool(void*)> is_live;
		std::move_only_function<void(void*)> func;
		std::move_only_function<void()> keep_alive;

		[[nodiscard]] task_entry(
			void* owner,
			std::move_only_function<bool(void*)>&& is_live,
			std::move_only_function<void(void*)>&& func,
			std::move_only_function<void()>&& keep_alive)
			: owner(owner),
			  is_live(std::move(is_live)),
			  func(std::move(func)),
			  keep_alive(std::move(keep_alive)){
		}

		void exec(){
			if(!is_live || !func){
				throw std::runtime_error{"associated task entry is empty"};
			}
			if(std::invoke(is_live, owner)){
				std::invoke(func, owner);
			}
		}

		[[nodiscard]] bool owned_by(const void* element) const noexcept{
			return owner == element;
		}
	};

	using container = mr::heap_vector<task_entry>;

	ccur::mpsc_double_buffer<task_entry, container> async_tasks_{};
	std::atomic_bool closed_{false};

public:
	[[nodiscard]] explicit associated_async_sync_task_queue_base(const container::allocator_type& alloc)
		: async_tasks_(alloc){
	}

	void consume(){
		if(closed_.load(std::memory_order_acquire)){
			return;
		}
		if(auto ts = async_tasks_.fetch()){
			for (auto&& t : *ts){
				t.exec();
			}
		}
	}

	void clear(){
		async_tasks_.clear();
	}

	void close() noexcept{
		closed_.store(true, std::memory_order_release);
		async_tasks_.clear();
	}

protected:
	template <typename... Args>
	bool try_emplace(Args&&... args){
		if(closed_.load(std::memory_order_acquire)){
			return false;
		}
		async_tasks_.emplace(std::forward<Args>(args)...);
		if(closed_.load(std::memory_order_acquire)){
			async_tasks_.clear();
			return false;
		}
		return true;
	}

	void merge(associated_async_sync_task_queue_base&& other){
		if(closed_.load(std::memory_order_acquire)){
			other.clear();
			return;
		}
		async_tasks_.merge(std::move(other).async_tasks_);
	}
};

export
template <typename T>
struct associated_async_sync_task_queue : associated_async_sync_task_queue_base{
	using owner_type = T;

	using associated_async_sync_task_queue_base::associated_async_sync_task_queue_base;

	template <std::derived_from<owner_type> E, std::invocable<E&> Fn>
	[[nodiscard]] bool try_post(E& e, Fn&& fn){
		auto owner_ref = e.ref();
		return this->try_emplace(
			std::addressof(e),
			[](void* owner){
				return static_cast<E*>(owner)->is_live();
			},
			[f = std::forward<Fn>(fn)](void* owner) mutable {
				std::invoke(f, *static_cast<E*>(owner));
			},
			[owner_ref = std::move(owner_ref)]{}
		);
	}

	template <std::derived_from<owner_type> E, std::invocable<> Fn>
	[[nodiscard]] bool try_post(E& e, Fn&& fn){
		auto owner_ref = e.ref();
		return this->try_emplace(
			std::addressof(e),
			[](void* owner){
				return static_cast<E*>(owner)->is_live();
			},
			[f = std::forward<Fn>(fn)](void*) mutable {
				std::invoke(f);
			},
			[owner_ref = std::move(owner_ref)]{}
		);
	}

	template <std::derived_from<owner_type> E, std::invocable<E&> Fn>
	void post(E& e, Fn&& fn){
		(void)this->try_post(e, std::forward<Fn>(fn));
	}

	template <std::derived_from<owner_type> E, std::invocable<> Fn>
	void post(E& e, Fn&& fn){
		(void)this->try_post(e, std::forward<Fn>(fn));
	}

	void erase(const owner_type* e) noexcept {
		async_tasks_.modify([&](container& c) noexcept {
			std::erase_if(c, [&](const container::value_type& v) noexcept {
				return v.owned_by(e);
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
	std::atomic_bool closed_{false};

public:
	[[nodiscard]] explicit async_sync_task_queue(const container::allocator_type& alloc) : async_tasks_(alloc){
	}

	template <std::invocable<CtxArgs...> Fn>
	[[nodiscard]] bool try_post(Fn&& fn){
		if(closed_.load(std::memory_order_acquire)){
			return false;
		}
		async_tasks_.emplace([f = std::forward<Fn>(fn)](CtxArgs... args) mutable {
			std::invoke(f, std::forward<CtxArgs>(args)...);
		});
		if(closed_.load(std::memory_order_acquire)){
			async_tasks_.clear();
			return false;
		}
		return true;
	}

	template <std::invocable<CtxArgs...> Fn>
	void post(Fn&& fn){
		(void)this->try_post(std::forward<Fn>(fn));
	}

	void consume(CtxArgs... args){
		if(closed_.load(std::memory_order_acquire)){
			return;
		}
		if(auto ts = async_tasks_.fetch()){
			for (auto&& t : *ts){
				t(std::forward<CtxArgs>(args)...);
			}
		}
	}

	void clear(){
		async_tasks_.clear();
	}

	void close() noexcept{
		closed_.store(true, std::memory_order_release);
		async_tasks_.clear();
	}
};

export
struct call_stream_task_queue{
private:
	ccur::mpsc_double_buffer_heterogeneous<call_stream<mr::unvs_allocator<std::byte>>> async_tasks_{};
	std::atomic_bool closed_{false};

public:
	[[nodiscard]] call_stream_task_queue() = default;

	template <typename Fn, typename ...Args>
		requires (std::invocable<Fn&&, Args&&...>)
	[[nodiscard]] bool try_post(Fn&& fn, Args&&... args){
		if(closed_.load(std::memory_order_acquire)){
			return false;
		}
		async_tasks_.emplace_back(std::forward<Fn>(fn), std::forward<Args>(args)...);
		if(closed_.load(std::memory_order_acquire)){
			return false;
		}
		return true;
	}

	template <typename Fn, typename ...Args>
		requires (std::invocable<Fn&&, Args&&...>)
	void post(Fn&& fn, Args&&... args){
		(void)this->try_post(std::forward<Fn>(fn), std::forward<Args>(args)...);
	}

	void consume(){
		if(closed_.load(std::memory_order_acquire)){
			return;
		}
		if(auto ts = async_tasks_.fetch()){
			ts->execute();
		}
	}

	void merge(call_stream_task_queue&& other){
		if(closed_.load(std::memory_order_acquire)){
			return;
		}
		async_tasks_.merge(std::move(other).async_tasks_);
	}

	void close() noexcept{
		closed_.store(true, std::memory_order_release);
	}
};

export
template <typename ...CtxArgs>
struct async_sync_endpoint_ref{
	async_sync_task_queue<CtxArgs...>* queue{};

	template <std::invocable<CtxArgs...> Fn>
	[[nodiscard]] bool try_post(Fn&& fn) const{
		return queue != nullptr && queue->try_post(std::forward<Fn>(fn));
	}

	void close() const noexcept{
		if(queue != nullptr){
			queue->close();
		}
	}
};

template <typename ...CtxArgs>
async_sync_endpoint_ref(async_sync_task_queue<CtxArgs...>&) -> async_sync_endpoint_ref<CtxArgs...>;

export
template <typename Owner>
struct associated_async_endpoint_ref{
	associated_async_sync_task_queue<Owner>* queue{};

	template <std::derived_from<Owner> E, typename Fn>
		requires (std::invocable<Fn&&, E&> || std::invocable<Fn&&>)
	[[nodiscard]] bool try_post(E& owner, Fn&& fn) const{
		return queue != nullptr && queue->try_post(owner, std::forward<Fn>(fn));
	}

	void close() const noexcept{
		if(queue != nullptr){
			queue->close();
		}
	}
};

template <typename Owner>
associated_async_endpoint_ref(associated_async_sync_task_queue<Owner>&) -> associated_async_endpoint_ref<Owner>;

export
struct call_stream_endpoint_ref{
	call_stream_task_queue* queue{};

	template <typename Fn, typename ...Args>
		requires (std::invocable<Fn&&, Args&&...>)
	[[nodiscard]] bool try_post(Fn&& fn, Args&&... args) const{
		return queue != nullptr && queue->try_post(std::forward<Fn>(fn), std::forward<Args>(args)...);
	}

	void close() const noexcept{
		if(queue != nullptr){
			queue->close();
		}
	}
};

}
