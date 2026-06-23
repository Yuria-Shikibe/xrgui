module;

#include <cassert>

#ifndef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
#include "plf_hive.h"
#endif

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.util.task_queue;

import std;
import mo_yanxi.gui.alloc;
import mo_yanxi.concurrent.mpsc_double_buffer;
import mo_yanxi.concurrent.mpsc_queue;
import mo_yanxi.allocator_aware_unique_ptr;
import mo_yanxi.call_stream;
import mo_yanxi.referenced_ptr;

#ifdef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
import <plf_hive.h>;
#endif

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
	std::uint32_t current{};
	std::uint32_t total{};
};

export
struct async_operation_state;

export
struct async_operation_state_pool;

struct async_operation_state_pool_owner_deleter{
	void operator()(mo_yanxi::referenced_object_atomic* pool) const noexcept;
};

export
using async_operation_state_pool_ptr = mo_yanxi::referenced_ptr<async_operation_state_pool>;

using async_operation_state_pool_owner_ptr = mo_yanxi::referenced_ptr<
	mo_yanxi::referenced_object_atomic,
	async_operation_state_pool_owner_deleter>;

export
struct async_operation_state_deleter{
	void operator()(async_operation_state* state) const noexcept;
};

export
using async_operation_state_ptr = mo_yanxi::referenced_ptr<
	async_operation_state,
	async_operation_state_deleter>;

export
struct async_operation_state : mo_yanxi::referenced_object_atomic{
	friend struct async_operation_state_pool;
	friend struct async_operation_state_deleter;

private:
	async_operation_state_pool_owner_ptr owner_pool_{};
	std::stop_source stop_source_{};
	std::atomic<std::uint64_t> progress_{};
	std::atomic<async_operation_status> status_{async_operation_status::pending};

	[[nodiscard]] inline static std::uint64_t pack_progress(std::uint32_t current, std::uint32_t total) noexcept{
		return (static_cast<std::uint64_t>(total) << 32u) | static_cast<std::uint64_t>(current);
	}

	[[nodiscard]] inline static async_progress unpack_progress(std::uint64_t value) noexcept{
		return {
			.current = static_cast<std::uint32_t>(value & 0xffff'ffffull),
			.total = static_cast<std::uint32_t>(value >> 32u)
		};
	}

	inline bool mark_status(async_operation_status next) noexcept{
		auto expected = async_operation_status::pending;
		return status_.compare_exchange_strong(
			expected,
			next,
			std::memory_order_acq_rel,
			std::memory_order_acquire);
	}

	void bind_owner_pool(async_operation_state_pool& pool) noexcept;

	[[nodiscard]] async_operation_state_pool_ptr owner_pool() const noexcept;

public:
	[[nodiscard]] async_operation_state() = default;
	~async_operation_state() noexcept;

	async_operation_state(const async_operation_state&) = delete;
	async_operation_state(async_operation_state&&) = delete;
	async_operation_state& operator=(const async_operation_state&) = delete;
	async_operation_state& operator=(async_operation_state&&) = delete;

	inline void request_stop() noexcept{
		stop_source_.request_stop();
	}

	[[nodiscard]] inline bool stop_requested() const noexcept{
		return stop_source_.stop_requested();
	}

	[[nodiscard]] inline std::stop_token stop_token() const noexcept{
		return stop_source_.get_token();
	}

	[[nodiscard]] inline bool is_pending() const noexcept{
		return this->status() == async_operation_status::pending;
	}

	[[nodiscard]] inline bool is_finished() const noexcept{
		return this->status() != async_operation_status::pending;
	}

	inline void report_progress(unsigned current, unsigned total) noexcept{
		if(total == 0u){
			current = 0u;
		}else{
			current = std::min(current, total);
		}
		progress_.store(async_operation_state::pack_progress(current, total), std::memory_order_release);
	}

	[[nodiscard]] inline async_progress progress_snapshot() const noexcept{
		return async_operation_state::unpack_progress(progress_.load(std::memory_order_acquire));
	}

	[[nodiscard]] inline async_operation_status status() const noexcept{
		return status_.load(std::memory_order_acquire);
	}

	inline void mark_completed() noexcept{
		(void)this->mark_status(async_operation_status::completed);
	}

	inline void mark_failed() noexcept{
		(void)this->mark_status(async_operation_status::failed);
	}

	inline void mark_cancelled() noexcept{
		this->request_stop();
		(void)this->mark_status(async_operation_status::cancelled);
	}
};

export
struct async_operation_state_pool : mo_yanxi::referenced_object_atomic{
private:
	using container = plf::hive<async_operation_state, mr::heap_allocator<async_operation_state>>;

	mutable std::mutex mutex_{};
	container states_{};

public:
	[[nodiscard]] async_operation_state_pool();
	[[nodiscard]] explicit async_operation_state_pool(const mr::heap_allocator<>& alloc);
	~async_operation_state_pool() noexcept;

	async_operation_state_pool(const async_operation_state_pool&) = delete;
	async_operation_state_pool(async_operation_state_pool&&) = delete;
	async_operation_state_pool& operator=(const async_operation_state_pool&) = delete;
	async_operation_state_pool& operator=(async_operation_state_pool&&) = delete;

	[[nodiscard]] async_operation_state_ptr create_operation();
	void cancel_all() noexcept;
	void erase_state(async_operation_state* state) noexcept;
};

inline void async_operation_state::bind_owner_pool(async_operation_state_pool& pool) noexcept{
	owner_pool_.reset(static_cast<mo_yanxi::referenced_object_atomic*>(std::addressof(pool)));
}

inline async_operation_state_pool_ptr async_operation_state::owner_pool() const noexcept{
	return async_operation_state_pool_ptr{static_cast<async_operation_state_pool*>(owner_pool_.get())};
}

inline async_operation_state::~async_operation_state() noexcept = default;

inline async_operation_state_pool::async_operation_state_pool()
	: async_operation_state_pool(mr::get_default_heap_allocator()){
}

inline async_operation_state_pool::async_operation_state_pool(const mr::heap_allocator<>& alloc)
	: states_(mr::heap_allocator<async_operation_state>{alloc}){
}

inline async_operation_state_pool::~async_operation_state_pool() noexcept{
	this->cancel_all();
}

inline void async_operation_state_pool_owner_deleter::operator()(mo_yanxi::referenced_object_atomic* pool) const noexcept{
	delete static_cast<async_operation_state_pool*>(pool);
}

inline async_operation_state_ptr async_operation_state_pool::create_operation(){
	std::scoped_lock lock{mutex_};
	auto itr = states_.emplace();
	auto& state = *itr;
	state.bind_owner_pool(*this);
	state.report_progress(0u, 0u);
	return async_operation_state_ptr{std::addressof(state)};
}

inline void async_operation_state_pool::cancel_all() noexcept{
	std::scoped_lock lock{mutex_};
	for(auto& state : states_){
		state.mark_cancelled();
	}
}

inline void async_operation_state_pool::erase_state(async_operation_state* state) noexcept{
	if(state == nullptr){
		return;
	}

	async_operation_state_pool_ptr keep_alive{this};
	std::scoped_lock lock{mutex_};
	auto itr = states_.get_iterator(state);
	if(itr != states_.end()){
		states_.erase(itr);
	}
}

inline void async_operation_state_deleter::operator()(async_operation_state* state) const noexcept{
	if(state == nullptr){
		return;
	}
	auto pool = state->owner_pool();
	assert(pool && "async_operation_state must be owned by an async_operation_state_pool");
	if(pool){
		pool->erase_state(state);
	}
}

export
struct async_operation_handle{
private:
	async_operation_state_ptr state_{};

	[[nodiscard]] bool valid_state() const noexcept{
		return state_ && state_->is_pending();
	}

public:
	[[nodiscard]] async_operation_handle() = default;

	[[nodiscard]] explicit async_operation_handle(async_operation_state_ptr state) noexcept
		: state_(std::move(state)){
	}

	inline void request_stop() const noexcept{
		if(this->valid_state()){
			state_->request_stop();
		}
	}

	[[nodiscard]] inline bool stop_requested() const noexcept{
		return this->valid_state() && state_->stop_requested();
	}

	[[nodiscard]] inline async_progress progress_snapshot() const noexcept{
		if(!state_){
			return {};
		}
		return state_->progress_snapshot();
	}

	[[nodiscard]] inline unsigned progress() const noexcept{
		return this->progress_snapshot().current;
	}

	[[nodiscard]] inline unsigned progress_total() const noexcept{
		return this->progress_snapshot().total;
	}

	[[nodiscard]] inline float progress_ratio() const noexcept{
		const auto progress = this->progress_snapshot();
		if(progress.total == 0u){
			return 0.f;
		}
		return static_cast<float>(progress.current) / static_cast<float>(progress.total);
	}

	[[nodiscard]] inline async_operation_status status() const noexcept{
		if(!state_){
			return async_operation_status::cancelled;
		}
		return state_->status();
	}
};

namespace detail{

inline constexpr std::size_t inline_bytes = 128-16;
inline constexpr std::size_t inline_align = alignof(std::max_align_t);

struct noop_callback{
	template <typename... Args>
	constexpr void operator()(Args&&...) const noexcept{
	}
};

template <typename Fn>
using normalized_callback_t = std::conditional_t<
	std::is_null_pointer_v<std::remove_reference_t<Fn>>,
	noop_callback,
	std::decay_t<Fn>>;

template <typename Fn>
[[nodiscard]] decltype(auto) normalize_callback(Fn&& fn){
	if constexpr(std::is_null_pointer_v<std::remove_reference_t<Fn>>){
		(void)fn;
		return detail::noop_callback{};
	}else{
		return std::forward<Fn>(fn);
	}
}

template <typename T, typename ValueFn, typename ErrorFn, typename CancelFn>
struct split_reply_handler{
	static_assert(std::is_void_v<T> || std::is_object_v<T>, "async_reply<T> cannot store reference results");

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

	// Per-model static ops keep the reply object small while avoiding per-instance function-pointer copies.
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

	void reset_pending_noexcept() noexcept{
		auto pending = this->release_pending();
		(void)pending;
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
		this->reset_pending_noexcept();
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
			this->reset_pending_noexcept();
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

		inline void exec(){
			if(!is_live || !func){
				throw std::runtime_error{"associated task entry is empty"};
			}
			if(std::invoke(is_live, owner)){
				std::invoke(func, owner);
			}
		}

		[[nodiscard]] inline bool owned_by(const void* element) const noexcept{
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

	inline void consume(){
		if(closed_.load(std::memory_order_acquire)){
			return;
		}
		if(auto ts = async_tasks_.fetch()){
			for (auto&& t : *ts){
				t.exec();
			}
		}
	}

	inline void clear(){
		async_tasks_.clear();
	}

	inline void close() noexcept{
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

	template <std::derived_from<owner_type> E, typename Fn>
		requires (std::invocable<Fn&&, E&> || std::invocable<Fn&&>)
	[[nodiscard]] bool try_post(E& e, Fn&& fn){
		auto owner_ref = e.ref();
		return this->try_emplace(
			std::addressof(e),
			[](void* owner){
				return static_cast<E*>(owner)->is_live();
			},
			[f = std::forward<Fn>(fn)](void* owner) mutable {
				if constexpr(std::invocable<Fn&, E&>){
					std::invoke(f, *static_cast<E*>(owner));
				}else{
					std::invoke(f);
				}
			},
			[owner_ref = std::move(owner_ref)]{}
		);
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

	mutable std::mutex consumer_thread_mutex_{};
	std::thread::id consumer_thread_id_{};

	inline void ensure_consumer_thread_(){
		const auto current_thread = std::this_thread::get_id();
		std::scoped_lock lock{consumer_thread_mutex_};
		if(consumer_thread_id_ == std::thread::id{}){
			consumer_thread_id_ = current_thread;
			return;
		}
#ifndef NDEBUG
		assert(consumer_thread_id_ == current_thread && "call_stream_task_queue consumed from a different thread");
#endif
	}

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
			this->clear();
			return false;
		}
		return true;
	}

	inline void consume(){
		this->ensure_consumer_thread_();
		if(closed_.load(std::memory_order_acquire)){
			return;
		}
		if(auto ts = async_tasks_.fetch()){
			ts->execute();
		}
	}

	[[nodiscard]] inline bool is_consumer_thread() const{
		std::scoped_lock lock{consumer_thread_mutex_};
		return consumer_thread_id_ == std::this_thread::get_id();
	}

	inline void merge(call_stream_task_queue&& other){
		if(closed_.load(std::memory_order_acquire)){
			other.clear();
			return;
		}
		async_tasks_.merge(std::move(other).async_tasks_);
	}

	inline void clear() noexcept{
		async_tasks_.modify([](auto& tasks) noexcept {
			tasks.clear();
		});
		if(auto* tasks = async_tasks_.fetch()){
			tasks->clear();
		}
	}

	inline void close() noexcept{
		closed_.store(true, std::memory_order_release);
		this->clear();
	}
};

// Wraps async_reply<T> so that set_* dispatches to the UI thread via a call_stream_task_queue,
// eliminating the need for callers to block the worker thread waiting for UI acknowledgement.
export
template <typename T>
struct deferred_reply{
	async_reply<T>          inner_;
	call_stream_task_queue* ui_queue_{};   // non-owning; points to scene::gui_inbox_

	// satisfies async_reply_object_for<deferred_reply<T>, T>
	void set_value(T val) && requires (!std::is_void_v<T>){
		ui_queue_->try_post([r = std::move(inner_), v = std::move(val)]() mutable{
			std::move(r).set_value(std::move(v));
		});
	}
	void set_value() && requires (std::is_void_v<T>){
		ui_queue_->try_post([r = std::move(inner_)]() mutable{ std::move(r).set_value(); });
	}
	void set_error(std::exception_ptr e) &&{
		ui_queue_->try_post([r = std::move(inner_), e = std::move(e)]() mutable{
			std::move(r).set_error(std::move(e));
		});
	}
	void set_cancelled() &&{
		ui_queue_->try_post([r = std::move(inner_)]() mutable{ std::move(r).set_cancelled(); });
	}
};

}
