module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>


export module mo_yanxi.call_stream;

import std;
import :call_stream_buffer;

#define USE_TAIL_DISPATCH
#ifdef USE_TAIL_DISPATCH
#if defined(_MSC_VER) && !defined(__clang__)
#if defined(_MSC_VER) && (_MSC_VER < 1950)
#error"You need at least VS 2026 / PlatformToolset v145 for tail calling."
#endif
#define MUST_TAIL [[msvc::musttail]]

//currently msvc's musttail is too weak
#undef USE_TAIL_DISPATCH

#elifdef __clang__
#define MUST_TAIL [[clang::musttail]]
#elifdef __GNUC__
#define MUST_TAIL [[gnu::musttail]]
#else
#define MUST_TAIL
#undef USE_TAIL_DISPATCH
#endif
#endif

#ifdef _MSC_VER
#define VM_CALL_CONV __fastcall
#else
#define VM_CALL_CONV
#endif

namespace mo_yanxi{
template <typename T, typename... Args>
concept has_static_call_operator = requires(Args... args){
	T::operator()(std::forward<Args>(args)...);
};

template <typename T, typename Ret, typename... Args>
concept has_compatible_static_call_operator =
	has_static_call_operator<T, Args...> &&
	(std::is_void_v<Ret> || requires(Args... args){
		{ T::operator()(std::forward<Args>(args)...) } -> std::convertible_to<Ret>;
	});

template <typename Ret, typename Fn, typename... Args>
concept call_stream_invocable =
	(std::is_void_v<Ret> && std::invocable<Fn, Args...>) ||
	(!std::is_void_v<Ret> && std::is_invocable_r_v<Ret, Fn, Args...>);

template <typename T>
concept call_stream_stop_predicate = requires(T&& value){
	bool(std::forward<T>(value));
};

template <typename Callback, typename Ret>
concept call_stream_result_callback =
	!std::is_void_v<Ret> &&
	std::invocable<Callback&, Ret&&> &&
	(std::same_as<std::invoke_result_t<Callback&, Ret&&>, void> ||
	 call_stream_stop_predicate<std::invoke_result_t<Callback&, Ret&&>>);

using invoke_fn_return_type =
#ifdef USE_TAIL_DISPATCH
void
#else
std::byte*
#endif
;

using invoker_fn = invoke_fn_return_type(*)(std::byte* current_instr_base, const std::byte* end, void* invoke_args_ptr);


struct alignas(16) instr_header{
	invoker_fn invoker;
	std::uint32_t prev_res_offset; // 4 bytes
	std::uint32_t flags_or_padding; // 4 bytes
};


export template <typename Allocator = std::allocator<std::byte>, typename... Args>
class basic_call_stream;

export template <typename Fn>
struct cmd_call;

template <typename Allocator, typename Ret, typename... Args>
class basic_call_stream_impl;

template <typename Allocator, typename... Args>
struct basic_call_stream_base{
	using type = basic_call_stream_impl<Allocator, void, Args...>;
};

template <typename Allocator, typename Ret, typename... Args>
struct basic_call_stream_base<Allocator, Ret(Args...)>{
	using type = basic_call_stream_impl<Allocator, Ret, Args...>;
};

template <typename Ret, typename ArgsTuple, bool = std::is_void_v<Ret>>
struct call_stream_result_context{};

template <typename Ret, typename ArgsTuple>
struct call_stream_result_context<Ret, ArgsTuple, false>{
	using result_handle_fn = bool(*)(void* callback, Ret&& result);

	ArgsTuple args;
	void* callback;
	result_handle_fn handle_result;
	bool stop{false};
};

template <typename T>
struct is_cmd_call : std::false_type{};

template <typename Fn>
struct is_cmd_call<cmd_call<Fn>> : std::true_type{};

template <typename T>
concept not_cmd_call = !is_cmd_call<std::remove_cvref_t<T>>::value;

FORCE_INLINE constexpr std::size_t align_forward(std::size_t offset, std::size_t alignment) noexcept{
	assert(std::has_single_bit(alignment));
	std::size_t aligned = (offset + alignment - 1) & ~(alignment - 1);
	assert(aligned >= offset);
	return aligned;
}

template <typename Allocator, typename Ret, typename... Args>
class basic_call_stream_impl{
public:
	static_assert(std::is_same_v<typename std::allocator_traits<Allocator>::value_type, std::byte>,
	              "allocator value type must be std::byte");

	using allocator_type = Allocator;
	using return_type = Ret;
	using invoke_args = std::tuple<Args...>;
	using result_context = call_stream_result_context<Ret, invoke_args>;

	// Handler 现已无缝嵌入在 Stream 内部，直接接收 old_base 与 new_base 以完成就地搬迁或析构
	using resource_handle_fn = void(*)(basic_call_stream_impl& stream, void* old_base, void* new_base) noexcept;

private:
	call_stream_buffer<Allocator> buffer_;
	std::size_t ip_{0};

	// 侵入式链表尾指针，~0U 代表终点（空）
	std::uint32_t last_res_offset_{~0U};

	void clear_res_() noexcept{
		std::uint32_t curr = last_res_offset_;
		while(curr != ~0U){
			auto* header = std::launder(reinterpret_cast<instr_header*>(buffer_.data() + curr));
			auto handler = *std::launder(
				reinterpret_cast<resource_handle_fn*>(buffer_.data() + curr + sizeof(instr_header)));
			std::uint32_t next = header->prev_res_offset;

			handler(*this, buffer_.data() + curr, nullptr);
			curr = next;
		}
		last_res_offset_ = ~0U;
	}

	auto get_relocate_cb() noexcept{
		return [this](std::byte* old_base, std::byte* new_base) noexcept{
			std::uint32_t curr = last_res_offset_;
			while(curr != ~0U){
				auto* header = std::launder(reinterpret_cast<instr_header*>(old_base + curr));
				auto handler = *std::launder(
					reinterpret_cast<resource_handle_fn*>(old_base + curr + sizeof(instr_header)));

				handler(*this, old_base + curr, new_base + curr);
				curr = header->prev_res_offset;
			}
		};
	}

	// 确保写入新的 instruction 之前，当前的 buffer offset 满足 header 对齐要求
	void ensure_header_alignment(){
		std::size_t current_size = buffer_.size();
		std::size_t padding = align_forward(current_size, alignof(instr_header)) - current_size;
		if(padding > 0){
			buffer_.append_zeros(padding, get_relocate_cb());
		}
	}

	template <typename Fn, std::size_t... Is>
	static decltype(auto) invoke_with_args_(Fn&& fn, invoke_args& args, std::index_sequence<Is...>){
		return std::invoke(std::forward<Fn>(fn), std::forward<Args>(std::get<Is>(args))...);
	}

	template <typename Callback, typename R = Ret>
		requires call_stream_result_callback<Callback, R>
	static bool handle_result_(void* callback_ptr, R&& result){
		using CallbackT = std::remove_reference_t<Callback>;
		using CallbackResult = std::invoke_result_t<CallbackT&, R&&>;

		auto& callback = *static_cast<CallbackT*>(callback_ptr);
		if constexpr(std::same_as<CallbackResult, void>){
			std::invoke(callback, std::forward<R>(result));
			return false;
		} else{
			return bool(std::invoke(callback, std::forward<R>(result)));
		}
	}

	template <typename Value>
	static bool dispatch_result_(result_context& context, Value&& value) requires(!std::is_void_v<Ret>){
		context.stop = context.handle_result(context.callback, std::forward<Value>(value));
		return context.stop;
	}

	template <typename Fn>
	static void invoke_payload_(Fn&& fn, void* invoke_args_ptr) requires(std::is_void_v<Ret>){
		if constexpr(sizeof...(Args) == 0){
			(void)std::invoke(std::forward<Fn>(fn));
		} else{
			auto& args = *static_cast<invoke_args*>(invoke_args_ptr);
			(void)basic_call_stream_impl::invoke_with_args_(std::forward<Fn>(fn), args, std::index_sequence_for<Args...>{});
		}
	}

	template <typename Fn>
	static bool invoke_payload_(Fn&& fn, void* context_ptr) requires(!std::is_void_v<Ret>){
		auto& context = *static_cast<result_context*>(context_ptr);
		if constexpr(sizeof...(Args) == 0){
			return basic_call_stream_impl::dispatch_result_(context, std::invoke(std::forward<Fn>(fn)));
		} else{
			return basic_call_stream_impl::dispatch_result_(
				context,
				basic_call_stream_impl::invoke_with_args_(
					std::forward<Fn>(fn),
					context.args,
					std::index_sequence_for<Args...>{}));
		}
	}

	template <typename T, std::size_t... Is>
	static decltype(auto) invoke_static_with_args_(invoke_args& args, std::index_sequence<Is...>){
		return T::operator()(std::forward<Args>(std::get<Is>(args))...);
	}

	template <typename T>
	static void invoke_static_payload_(void* invoke_args_ptr) requires(std::is_void_v<Ret>){
		if constexpr(sizeof...(Args) == 0){
			(void)T::operator()();
		} else{
			auto& args = *static_cast<invoke_args*>(invoke_args_ptr);
			(void)basic_call_stream_impl::template invoke_static_with_args_<T>(args, std::index_sequence_for<Args...>{});
		}
	}

	template <typename T>
	static bool invoke_static_payload_(void* context_ptr) requires(!std::is_void_v<Ret>){
		auto& context = *static_cast<result_context*>(context_ptr);
		if constexpr(sizeof...(Args) == 0){
			return basic_call_stream_impl::dispatch_result_(context, T::operator()());
		} else{
			return basic_call_stream_impl::dispatch_result_(
				context,
				basic_call_stream_impl::template invoke_static_with_args_<T>(
					context.args,
					std::index_sequence_for<Args...>{}));
		}
	}

	template <typename FnTy, bool AllowStaticDispatch, typename... CtorArgs>
		requires(std::constructible_from<FnTy, CtorArgs&&...> && call_stream_invocable<Ret, FnTy&, Args...>)
	void emplace_call_(CtorArgs&&... args);

	FORCE_INLINE void execute_context_(void* context_ptr){
		std::byte* ptr = buffer_.data() + ip_;
		const std::byte* end = buffer_.data() + buffer_.size();

		try{
#ifdef USE_TAIL_DISPATCH
			// Clang/GCC：纯尾调用驱动，一旦启动直到遇到 end 才会返回
			auto invoker = std::launder(reinterpret_cast<instr_header*>(ptr))->invoker;
			invoker(ptr, end, context_ptr);
#else
			// MSVC：稳健的 while 循环指针追逐，绝对不会爆栈
			while(ptr < end){
				auto invoker = std::launder(reinterpret_cast<instr_header*>(ptr))->invoker;
				CHECKED_ASSUME(invoker != nullptr);
				ptr = invoker(ptr, end, context_ptr);
			}
#endif
			ip_ = buffer_.size();
		} catch(...){
			ip_ = buffer_.size();
			throw;
		}
	}

public:
	explicit basic_call_stream_impl(const allocator_type& alloc = allocator_type())
		: buffer_(alloc){
	}

	basic_call_stream_impl(const basic_call_stream_impl&) = delete;
	basic_call_stream_impl& operator=(const basic_call_stream_impl&) = delete;

	basic_call_stream_impl(basic_call_stream_impl&& other) noexcept
		: buffer_(std::move(other.buffer_)),
		  ip_(std::exchange(other.ip_, 0)),
		  last_res_offset_(std::exchange(other.last_res_offset_, ~0U)){
	}

	basic_call_stream_impl& operator=(basic_call_stream_impl&& other) noexcept{
		if(this == &other) return *this;
		clear_res_();
		buffer_ = std::move(other.buffer_);
		ip_ = std::exchange(other.ip_, 0);
		last_res_offset_ = std::exchange(other.last_res_offset_, ~0U);
		return *this;
	}

	~basic_call_stream_impl(){
		clear_res_();
	}

	void reserve(std::size_t size){
		buffer_.reserve(size, get_relocate_cb());
	}

	allocator_type get_allocator() const noexcept{ return buffer_.get_allocator(); }

	// 平凡类型指令的底层发射接口 (无生命周期管理)
	// 平凡类型指令的底层发射接口
	void emit_instruction(invoker_fn invoker, const void* payload, std::size_t size, std::size_t alignment){
		ensure_header_alignment();
		std::size_t payload_offset = align_forward(sizeof(instr_header), alignment);
		std::size_t total_bytes = align_forward(payload_offset + size, alignof(instr_header));

		if(!std::in_range<std::uint32_t>(total_bytes)){
			throw std::bad_alloc();
		}

		auto* raw_mem = buffer_.allocate_uninitialized(total_bytes, get_relocate_cb());
		new(raw_mem) instr_header{
				.invoker = invoker,
				.prev_res_offset = 0
			};

		if(size > 0 && payload != nullptr){
			std::memcpy(raw_mem + payload_offset, payload, size);
		}
	}

	// 堆分配降级策略的发射接口
	void emit_non_trivial_call_heap(invoker_fn invoker, void* heap_ptr, resource_handle_fn handler){
		assert(invoker != nullptr);
		assert(heap_ptr != nullptr);
		assert(handler != nullptr);

		const auto checkpoint = buffer_.size();

		try{
			ensure_header_alignment();
			std::size_t current_size = buffer_.size();

			std::size_t handler_offset = sizeof(instr_header);
			std::size_t payload_offset = align_forward(handler_offset + sizeof(resource_handle_fn), alignof(void*));
			std::size_t total_bytes = align_forward(payload_offset + sizeof(void*), alignof(instr_header));

			if(!std::in_range<std::uint32_t>(total_bytes)){
				throw std::bad_alloc();
			}

			auto* raw_mem = buffer_.allocate_uninitialized(total_bytes, get_relocate_cb());
			new(raw_mem) instr_header{
					.invoker = invoker,
					.prev_res_offset = last_res_offset_
				};

			last_res_offset_ = static_cast<std::uint32_t>(current_size);

			new(raw_mem + handler_offset) resource_handle_fn(handler);

			void* obj_ptr = raw_mem + payload_offset;
			std::memcpy(obj_ptr, &heap_ptr, sizeof(void*));
		} catch(...){
			buffer_.rollback_size(checkpoint);
			throw;
		}
	}

	// 内联非平凡对象的发射接口
	template <typename T, typename... CtorArgs>
		requires std::is_nothrow_move_constructible_v<T>
	void emit_non_trivial_call_inline(invoker_fn invoker, CtorArgs&&... args){
		assert(invoker != nullptr);
		const auto checkpoint = buffer_.size();

		try{
			ensure_header_alignment();
			std::size_t current_size = buffer_.size();
			std::size_t handler_offset = sizeof(instr_header);

			std::size_t payload_offset = align_forward(handler_offset + sizeof(resource_handle_fn), alignof(T));
			std::size_t total_bytes = align_forward(payload_offset + sizeof(T), alignof(instr_header));

			if(!std::in_range<std::uint32_t>(total_bytes)){
				throw std::bad_alloc();
			}

			auto* raw_mem = buffer_.allocate_uninitialized(total_bytes, get_relocate_cb());
			new(raw_mem) instr_header{
					.invoker = invoker,
					.prev_res_offset = last_res_offset_
				};

			last_res_offset_ = static_cast<std::uint32_t>(current_size);

			static constexpr auto res_handler = +[](basic_call_stream_impl&, void* old_base, void* new_base) noexcept{
				constexpr std::size_t p_offset = align_forward(sizeof(instr_header) + sizeof(resource_handle_fn),
				                                               alignof(T));
				auto* typed_src = std::launder(reinterpret_cast<T*>(static_cast<std::byte*>(old_base) + p_offset));

				if(new_base){
					auto* typed_dst = reinterpret_cast<T*>(static_cast<std::byte*>(new_base) + p_offset);
					new(typed_dst) T(std::move(*typed_src));
					typed_src->~T();
				} else{
					typed_src->~T();
				}
			};

			new(raw_mem + handler_offset) resource_handle_fn(res_handler);

			void* obj_ptr = raw_mem + payload_offset;
			new(obj_ptr) T(std::forward<CtorArgs>(args)...);
		} catch(...){
			buffer_.rollback_size(checkpoint);
			throw;
		}
	}

	void emit_noop(){
		this->emit_instruction(+[](std::byte* base, const std::byte* end, void* invoke_args_ptr) static -> invoke_fn_return_type{
			std::byte* next_ptr = base + sizeof(instr_header);

#ifdef USE_TAIL_DISPATCH
			if(next_ptr < end){
				auto next_invoker = std::launder(reinterpret_cast<instr_header*>(next_ptr))->invoker;
				MUST_TAIL return next_invoker(next_ptr, end, invoke_args_ptr);
			}
#else
			return next_ptr;
#endif
		}, nullptr, 0, 1);
	}

	// 极速 VM 调度引擎 - 智能双模驱动
	FORCE_INLINE void execute(Args... args) requires(std::is_void_v<Ret>){
		if(empty()) return;

		if constexpr(sizeof...(Args) == 0){
			this->execute_context_(nullptr);
		} else{
			invoke_args packed_args(std::forward<Args>(args)...);
			this->execute_context_(std::addressof(packed_args));
		}
	}

	template <typename Callback>
		requires call_stream_result_callback<std::remove_reference_t<Callback>, Ret>
	FORCE_INLINE void execute(Args... args, Callback&& callback) requires(!std::is_void_v<Ret>){
		if(empty()) return;

		using CallbackT = std::remove_reference_t<Callback>;
		if constexpr(std::is_function_v<CallbackT>){
			auto* callback_ptr = std::addressof(callback);
			result_context context{
				invoke_args(std::forward<Args>(args)...),
				std::addressof(callback_ptr),
				&basic_call_stream_impl::template handle_result_<decltype(callback_ptr)>,
				false
			};
			this->execute_context_(std::addressof(context));
		} else{
			result_context context{
				invoke_args(std::forward<Args>(args)...),
				const_cast<void*>(static_cast<const void*>(std::addressof(callback))),
				&basic_call_stream_impl::template handle_result_<CallbackT>,
				false
			};
			this->execute_context_(std::addressof(context));
		}
	}

	void reset_ip(std::size_t new_ip = 0) noexcept{
		assert(new_ip <= buffer_.size());
		ip_ = new_ip;
	}

	[[nodiscard]] std::size_t current_ip() const noexcept{ return ip_; }
	[[nodiscard]] bool empty() const noexcept{ return buffer_.empty(); }
	[[nodiscard]] std::size_t size() const noexcept{ return buffer_.size(); }

	void clear() noexcept{
		clear_res_();
		buffer_.clear();
		ip_ = 0;
	}

	void merge(basic_call_stream_impl&& other) {
		if (this == &other || other.empty()) return;

		if (this->empty()) {
			*this = std::move(other);
			return;
		}

		ensure_header_alignment();

		const std::size_t base_offset = buffer_.size();
		const std::size_t other_size = other.buffer_.size();

		if (!std::in_range<std::uint32_t>(base_offset + other_size)) {
			throw std::bad_alloc();
		}

		auto* dest = buffer_.allocate_uninitialized(other_size, get_relocate_cb());

		std::memcpy(dest, other.buffer_.data(), other_size);
		std::uint32_t curr = other.last_res_offset_;

		while (curr != ~0U) {
			auto* old_header = std::launder(reinterpret_cast<instr_header*>(other.buffer_.data() + curr));
			auto* new_header = std::launder(reinterpret_cast<instr_header*>(dest + curr));

			auto handler = *std::launder(reinterpret_cast<resource_handle_fn*>(other.buffer_.data() + curr + sizeof(instr_header)));

			handler(*this, other.buffer_.data() + curr, dest + curr);

			std::uint32_t next = old_header->prev_res_offset;

			if (next != ~0U) {
				new_header->prev_res_offset = static_cast<std::uint32_t>(base_offset + next);
			} else {
				new_header->prev_res_offset = last_res_offset_;
			}

			curr = next;
		}

		if (other.last_res_offset_ != ~0U) {
			last_res_offset_ = static_cast<std::uint32_t>(base_offset + other.last_res_offset_);
		}

		other.last_res_offset_ = ~0U;
		other.buffer_.clear();
		other.ip_ = 0;
	}

	template <typename Fn>
		requires(std::constructible_from<Fn, const Fn&> && call_stream_invocable<Ret, Fn&, Args...>)
	void push_back(const cmd_call<Fn>& call);

	template <typename Fn>
		requires(std::constructible_from<Fn, Fn&&> && call_stream_invocable<Ret, Fn&, Args...>)
	void push_back(cmd_call<Fn>&& call);

	template <typename Fn>
		requires(std::constructible_from<std::decay_t<Fn>, Fn&&> &&
		         call_stream_invocable<Ret, std::decay_t<Fn>&, Args...>)
	void emplace_back(Fn&& fn);

	template <typename FnTy, typename... Ts>
		requires(std::constructible_from<FnTy, Ts&&...> && call_stream_invocable<Ret, FnTy&, Args...>)
	void emplace_back(Ts&&... args);

	friend void swap(basic_call_stream_impl& lhs, basic_call_stream_impl& rhs) noexcept(std::is_nothrow_swappable_v<decltype(buffer_)>){
		using std::swap;
		swap(lhs.buffer_, rhs.buffer_);
		swap(lhs.ip_, rhs.ip_);
		swap(lhs.last_res_offset_, rhs.last_res_offset_);
	}
};


#ifdef USE_TAIL_DISPATCH
#define CMD_CALL_STOP(end_ptr) return
#define CMD_CALL_TAIL_DISPATCH(next, end_ptr, invoke_args_ptr) \
	if ((next) >= (end_ptr)) return; \
	auto* next_invoker = std::launder(reinterpret_cast<instr_header*>(next))->invoker; \
	CHECKED_ASSUME(next_invoker != nullptr); \
	MUST_TAIL return next_invoker((next), (end_ptr), (invoke_args_ptr));
#else
#define CMD_CALL_STOP(end_ptr) return const_cast<std::byte*>(end_ptr)
#define CMD_CALL_TAIL_DISPATCH(next, end_ptr, invoke_args_ptr) return (next)
#endif

template <typename Fn>
struct cmd_call{
	ADAPTED_NO_UNIQUE_ADDRESS Fn callable;

	template <typename... Args>
		requires(std::constructible_from<Fn, Args&&...>)
	[[nodiscard]] explicit(false) cmd_call(Args&&... args)
		: callable(std::forward<Args>(args)...){
	}

	FORCE_INLINE decltype(auto) operator()() & requires std::invocable<Fn&> {
		return std::invoke(this->callable);
	}

	FORCE_INLINE decltype(auto) operator()() && requires std::invocable<Fn> {
		return std::invoke(std::move(this->callable));
	}

	FORCE_INLINE decltype(auto) operator()() const & requires std::invocable<const Fn&> {
		return std::invoke(this->callable);
	}
};

template <typename Fn>
cmd_call(Fn&&) -> cmd_call<std::decay_t<Fn>>;

export template <typename Allocator, typename... Args>
class basic_call_stream : public basic_call_stream_base<Allocator, Args...>::type{
	using base_type = typename basic_call_stream_base<Allocator, Args...>::type;

public:
	using allocator_type = typename base_type::allocator_type;
	using return_type = typename base_type::return_type;
	using invoke_args = typename base_type::invoke_args;

	explicit basic_call_stream(const allocator_type& alloc = allocator_type())
		: base_type(alloc){
	}

	basic_call_stream(const basic_call_stream&) = delete;
	basic_call_stream& operator=(const basic_call_stream&) = delete;
	basic_call_stream(basic_call_stream&&) noexcept = default;
	basic_call_stream& operator=(basic_call_stream&&) noexcept = default;

	void merge(basic_call_stream&& other){
		base_type::merge(std::move(other));
	}

	friend void swap(basic_call_stream& lhs, basic_call_stream& rhs) noexcept(std::is_nothrow_swappable_v<base_type>){
		using std::swap;
		swap(static_cast<base_type&>(lhs), static_cast<base_type&>(rhs));
	}
};

export template <typename Allocator = std::allocator<std::byte>>
using call_stream = basic_call_stream<Allocator>;

template <typename Allocator, typename Ret, typename... Args>
template <typename FnTy, bool AllowStaticDispatch, typename... CtorArgs>
	requires(std::constructible_from<FnTy, CtorArgs&&...> && call_stream_invocable<Ret, FnTy&, Args...>)
void basic_call_stream_impl<Allocator, Ret, Args...>::emplace_call_(CtorArgs&&... args){
	using PayloadT = FnTy;

	// 1. 无状态可调用对象的特殊优化 (静态调用或空类)
	if constexpr(AllowStaticDispatch){
		constexpr bool is_static = has_compatible_static_call_operator<PayloadT, Ret, Args...>;
		constexpr bool is_empty = std::is_empty_v<PayloadT> &&
		                          std::default_initializable<PayloadT> &&
		                          call_stream_invocable<Ret, const PayloadT&, Args...>;
		if constexpr(!std::is_pointer_v<PayloadT> && (is_static || is_empty)){
			this->emit_instruction(+[](std::byte* base, const std::byte* end, void* invoke_args_ptr) static -> invoke_fn_return_type{
				ATTR_FORCEINLINE_SENTENCE {
					if constexpr(std::is_void_v<Ret>){
						if constexpr(is_static){
							basic_call_stream_impl::template invoke_static_payload_<PayloadT>(invoke_args_ptr);
						} else{
							static const PayloadT fn_raw{};
							basic_call_stream_impl::invoke_payload_(fn_raw, invoke_args_ptr);
						}
					} else{
						if constexpr(is_static){
							if(basic_call_stream_impl::template invoke_static_payload_<PayloadT>(invoke_args_ptr)){
								CMD_CALL_STOP(end);
							}
						} else{
							static const PayloadT fn_raw{};
							if(basic_call_stream_impl::invoke_payload_(fn_raw, invoke_args_ptr)){
								CMD_CALL_STOP(end);
							}
						}
					}
				};

				std::byte* next_ptr = base + sizeof(instr_header);
				CMD_CALL_TAIL_DISPATCH(next_ptr, end, invoke_args_ptr);
			}, nullptr, 0, 1);
			return;
		}
	}

	// 2. 决定负载类型 (Payload)
	constexpr bool is_trivial = std::is_trivially_copyable_v<PayloadT> &&
	                            std::is_trivially_destructible_v<PayloadT>;

	// 3. 提取共享的静态派发指针
	static constexpr auto fptr = +[](std::byte* base, const std::byte* end, void* invoke_args_ptr) static -> invoke_fn_return_type{
		static constexpr std::size_t offset = []{
			if constexpr(is_trivial){
				return align_forward(sizeof(instr_header), alignof(PayloadT));
			} else{
				return align_forward(
					sizeof(instr_header) + sizeof(typename basic_call_stream_impl::resource_handle_fn),
					alignof(PayloadT));
			}
		}();
		static constexpr std::size_t total_size = align_forward(offset + sizeof(PayloadT),
		                                                        alignof(instr_header));

		ATTR_FORCEINLINE_SENTENCE {
			auto& obj = *std::launder(reinterpret_cast<PayloadT*>(base + offset));
			if constexpr(std::is_void_v<Ret>){
				basic_call_stream_impl::invoke_payload_(obj, invoke_args_ptr);
			} else if(basic_call_stream_impl::invoke_payload_(obj, invoke_args_ptr)){
				CMD_CALL_STOP(end);
			}
		}

		std::byte* next_ptr = base + total_size;
		CMD_CALL_TAIL_DISPATCH(next_ptr, end, invoke_args_ptr);
	};

	// 4. 根据类型特性分配与完美转发构造
	if constexpr(is_trivial){
		PayloadT payload(std::forward<CtorArgs>(args)...);
		this->emit_instruction(fptr, &payload, sizeof(PayloadT), alignof(PayloadT));
	} else if constexpr(std::is_nothrow_move_constructible_v<PayloadT>){
		this->template emit_non_trivial_call_inline<PayloadT>(fptr, std::forward<CtorArgs>(args)...);
	} else{
		// 非平凡对象的堆分配路径
		using TypedAlloc = typename std::allocator_traits<Allocator>::template rebind_alloc<PayloadT>;
		TypedAlloc alloc(this->get_allocator());
		PayloadT* heap_obj = std::allocator_traits<TypedAlloc>::allocate(alloc, 1);

		try{
			// 完美转发至堆上构建
			std::allocator_traits<TypedAlloc>::construct(alloc, heap_obj, std::forward<CtorArgs>(args)...);
		} catch(...){
			std::allocator_traits<TypedAlloc>::deallocate(alloc, heap_obj, 1);
			throw;
		}

		try{
			this->emit_non_trivial_call_heap(
				+[](std::byte* base, const std::byte* end, void* invoke_args_ptr) static -> invoke_fn_return_type{
					static constexpr std::size_t payload_offset = align_forward(
						sizeof(instr_header) + sizeof(typename basic_call_stream_impl::resource_handle_fn),
						alignof(PayloadT*));
					static constexpr std::size_t total_size = align_forward(
						payload_offset + sizeof(PayloadT*), alignof(instr_header));

					ATTR_FORCEINLINE_SENTENCE {
						auto& obj_ptr = *std::launder(reinterpret_cast<PayloadT**>(base + payload_offset));
						if constexpr(std::is_void_v<Ret>){
							basic_call_stream_impl::invoke_payload_(*obj_ptr, invoke_args_ptr);
						} else if(basic_call_stream_impl::invoke_payload_(*obj_ptr, invoke_args_ptr)){
							CMD_CALL_STOP(end);
						}
					}

					std::byte* next_ptr = base + total_size;
					CMD_CALL_TAIL_DISPATCH(next_ptr, end, invoke_args_ptr);
				},
				heap_obj,
				+[] FORCE_INLINE (basic_call_stream_impl& s, void* old_base, void* new_base) noexcept{
					if(new_base) return;
					constexpr std::size_t p_off = align_forward(
						sizeof(instr_header) + sizeof(typename basic_call_stream_impl::resource_handle_fn),
						alignof(PayloadT*));
					auto* typed_ptr =
						*std::launder(reinterpret_cast<PayloadT**>(static_cast<std::byte*>(old_base) + p_off));

					TypedAlloc del_alloc(s.get_allocator());
					std::allocator_traits<TypedAlloc>::destroy(del_alloc, typed_ptr);
					std::allocator_traits<TypedAlloc>::deallocate(del_alloc, typed_ptr, 1);
				}
			);
		} catch(...){
			std::allocator_traits<TypedAlloc>::destroy(alloc, heap_obj);
			std::allocator_traits<TypedAlloc>::deallocate(alloc, heap_obj, 1);
			throw;
		}
	}
}

template <typename Allocator, typename Ret, typename... Args>
template <typename Fn>
	requires(std::constructible_from<std::decay_t<Fn>, Fn&&> &&
	         call_stream_invocable<Ret, std::decay_t<Fn>&, Args...>)
void basic_call_stream_impl<Allocator, Ret, Args...>::emplace_back(Fn&& fn){
	this->template emplace_call_<std::decay_t<Fn>, true>(std::forward<Fn>(fn));
}

template <typename Allocator, typename Ret, typename... Args>
template <typename FnTy, typename... Ts>
	requires(std::constructible_from<FnTy, Ts&&...> && call_stream_invocable<Ret, FnTy&, Args...>)
void basic_call_stream_impl<Allocator, Ret, Args...>::emplace_back(Ts&&... args){
	this->template emplace_call_<FnTy, sizeof...(Ts) == 0>(std::forward<Ts>(args)...);
}

template <typename Allocator, typename Ret, typename... Args>
template <typename Fn>
	requires(std::constructible_from<Fn, const Fn&> && call_stream_invocable<Ret, Fn&, Args...>)
void basic_call_stream_impl<Allocator, Ret, Args...>::push_back(const cmd_call<Fn>& call){
	this->emplace_back(call.callable);
}

template <typename Allocator, typename Ret, typename... Args>
template <typename Fn>
	requires(std::constructible_from<Fn, Fn&&> && call_stream_invocable<Ret, Fn&, Args...>)
void basic_call_stream_impl<Allocator, Ret, Args...>::push_back(cmd_call<Fn>&& call){
	this->emplace_back(std::move(call.callable));
}

export template <typename Allocator, typename... Spec, typename Fn>
	requires(not_cmd_call<Fn> &&
	         requires(basic_call_stream<Allocator, Spec...>& stream, Fn&& call){
		         stream.emplace_back(std::forward<Fn>(call));
	         })
basic_call_stream<Allocator, Spec...>& operator<<(basic_call_stream<Allocator, Spec...>& stream, Fn&& call){
	stream.emplace_back(std::forward<Fn>(call));
	return stream;
}

export template <typename Allocator, typename... Spec, typename Fn>
	requires requires(basic_call_stream<Allocator, Spec...>& stream, cmd_call<Fn>&& call){
		stream.push_back(std::move(call));
	}
basic_call_stream<Allocator, Spec...>& operator<<(basic_call_stream<Allocator, Spec...>& stream, cmd_call<Fn>&& call){
	stream.push_back(std::move(call));
	return stream;
}

}
