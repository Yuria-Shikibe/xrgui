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
MUST_TAIL [[gnu::musttail]]
#else
#error "Your compiler does not support the musttail attribute, which is strictly required for this VM engine."
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
	{ T::operator()(std::forward<Args>(args)...) };
};

using invoke_fn_return_type =
#ifdef USE_TAIL_DISPATCH
void
#else
std::byte*
#endif
;

using invoker_fn = invoke_fn_return_type(*)(std::byte* current_instr_base, const std::byte* end);


struct alignas(16) instr_header{
	invoker_fn invoker; // 8 bytes
	std::uint32_t prev_res_offset; // 4 bytes
	std::uint32_t flags_or_padding; // 4 bytes
};


export template <typename Fn, typename... Args>
struct cmd_call;

FORCE_INLINE constexpr std::size_t align_forward(std::size_t offset, std::size_t alignment) noexcept{
	assert(std::has_single_bit(alignment));
	std::size_t aligned = (offset + alignment - 1) & ~(alignment - 1);
	assert(aligned >= offset);
	return aligned;
}

export template <typename Allocator = std::allocator<std::byte>>
class call_stream{
public:
	static_assert(std::is_same_v<typename std::allocator_traits<Allocator>::value_type, std::byte>,
	              "allocator value type must be std::byte");

	using allocator_type = Allocator;

	// Handler 现已无缝嵌入在 Stream 内部，直接接收 old_base 与 new_base 以完成就地搬迁或析构
	using resource_handle_fn = void(*)(call_stream& stream, void* old_base, void* new_base) noexcept;

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

public:
	explicit call_stream(const allocator_type& alloc = allocator_type())
		: buffer_(alloc){
	}

	call_stream(const call_stream&) = delete;
	call_stream& operator=(const call_stream&) = delete;

	call_stream(call_stream&& other) noexcept
		: buffer_(std::move(other.buffer_)),
		  ip_(std::exchange(other.ip_, 0)),
		  last_res_offset_(std::exchange(other.last_res_offset_, ~0U)){
	}

	call_stream& operator=(call_stream&& other) noexcept{
		if(this == &other) return *this;
		clear_res_();
		buffer_ = std::move(other.buffer_);
		ip_ = std::exchange(other.ip_, 0);
		last_res_offset_ = std::exchange(other.last_res_offset_, ~0U);
		return *this;
	}

	~call_stream(){
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
	template <typename T, typename... Args>
		requires std::is_nothrow_move_constructible_v<T>
	void emit_non_trivial_call_inline(invoker_fn invoker, Args&&... args){
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

			static constexpr auto res_handler = +[](call_stream&, void* old_base, void* new_base) noexcept{
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
			new(obj_ptr) T(std::forward<Args>(args)...);
		} catch(...){
			buffer_.rollback_size(checkpoint);
			throw;
		}
	}

	void emit_noop(){
		this->emit_instruction(+[](std::byte* base, const std::byte* end) static -> invoke_fn_return_type{
			std::byte* next_ptr = base + sizeof(instr_header);

#ifdef USE_TAIL_DISPATCH
			if(next_ptr < end){
				auto next_invoker = std::launder(reinterpret_cast<instr_header*>(next_ptr))->invoker;
				MUST_TAIL return next_invoker(next_ptr, end);
			}
#else
			return next_ptr;
#endif
		}, nullptr, 0, 1);
	}

	// 极速 VM 调度引擎 - 智能双模驱动
	FORCE_INLINE void execute(){
		if(empty()) return;

		std::byte* ptr = buffer_.data() + ip_;
		const std::byte* end = buffer_.data() + buffer_.size();

		try{
#ifdef USE_TAIL_DISPATCH
			// Clang/GCC：纯尾调用驱动，一旦启动直到遇到 end 才会返回
			auto invoker = std::launder(reinterpret_cast<instr_header*>(ptr))->invoker;
			invoker(ptr, end);
#else
			// MSVC：稳健的 while 循环指针追逐，绝对不会爆栈
			while(ptr < end){
				auto invoker = std::launder(reinterpret_cast<instr_header*>(ptr))->invoker;
				CHECKED_ASSUME(invoker != nullptr);
				ptr = invoker(ptr, end);
			}
#endif
			ip_ = buffer_.size();
		} catch(...){
			ip_ = buffer_.size();
			throw;
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

	template <typename Fn, typename... Args>
	void push_back(const cmd_call<Fn, Args...>& call);

	template <typename Fn, typename... Args>
	void push_back(cmd_call<Fn, Args...>&& call);

	template <typename Fn, typename... Args>
	void emplace_back(Fn&& fn, Args&&... args);

	friend void swap(call_stream& lhs, call_stream& rhs) noexcept(std::is_nothrow_swappable_v<decltype(buffer_)>){
		using std::swap;
		swap(lhs.buffer_, rhs.buffer_);
		swap(lhs.ip_, rhs.ip_);
		swap(lhs.last_res_offset_, rhs.last_res_offset_);
	}
};


#ifdef USE_TAIL_DISPATCH
#define CMD_CALL_TAIL_DISPATCH(next, end_ptr) \
	if ((next) >= (end_ptr)) return; \
	auto* next_invoker = std::launder(reinterpret_cast<instr_header*>(next))->invoker; \
	CHECKED_ASSUME(next_invoker != nullptr); \
	MUST_TAIL return next_invoker((next), (end_ptr));
#else
#define CMD_CALL_TAIL_DISPATCH(next, end_ptr) return (next)
#endif

template <typename Fn, typename... Args>
struct cmd_call{
	using args_tuple = std::tuple<Args...>;
	ADAPTED_NO_UNIQUE_ADDRESS Fn callable;
	ADAPTED_NO_UNIQUE_ADDRESS args_tuple args;

	template <typename FnTy, typename... ArgsTy>
		requires(std::invocable<Fn, ArgsTy&&...>)
	[[nodiscard]] explicit(false) cmd_call(FnTy&& callable, ArgsTy&&... args)
		: callable(std::forward<FnTy>(callable)),
		  args(std::make_tuple(std::forward<ArgsTy>(args)...)){
	}

	FORCE_INLINE decltype(auto) operator()() & {
		return std::apply(callable, args);
	}
	FORCE_INLINE decltype(auto) operator()() && {
		return std::apply(std::move(callable), std::move(args));
	}
	FORCE_INLINE decltype(auto) operator()() const & {
		return std::apply(callable, args);
	}

	template <typename Allocator>
	friend call_stream<Allocator>& operator<<(call_stream<Allocator>& stream, cmd_call&& call){
		stream.push_back(std::move(call));
		return stream;
	}
};

template <typename Fn, typename... Args>
cmd_call(Fn&&, Args&&...) -> cmd_call<std::decay_t<Fn>, std::decay_t<Args>...>;

export template <typename Allocator, std::invocable<> Fn>
call_stream<Allocator>& operator<<(call_stream<Allocator>& stream, Fn&& call){
	stream.emplace_back(std::forward<Fn>(call));
	return stream;
}

template <typename Allocator>
template <typename Fn, typename... Args>
void call_stream<Allocator>::emplace_back(Fn&& fn, Args&&... args){
	using FnRaw = std::decay_t<Fn>;
	constexpr bool has_args = sizeof...(Args) > 0;

	// 1. 无参且无状态的特殊优化 (静态调用或空类)
	if constexpr(!has_args){
		constexpr bool is_static = has_static_call_operator<FnRaw>;
		constexpr bool is_empty = std::is_empty_v<FnRaw>;
		if constexpr(!std::is_pointer_v<FnRaw> && (is_static || is_empty)){
			this->emit_instruction(+[](std::byte* base, const std::byte* end) static -> invoke_fn_return_type{
				ATTR_FORCEINLINE_SENTENCE {
					if constexpr(is_static){
						(void)FnRaw::operator()();
					} else{
						static constexpr FnRaw fn_raw{};
						(void)fn_raw();
					}
				};

				std::byte* next_ptr = base + sizeof(instr_header);
				CMD_CALL_TAIL_DISPATCH(next_ptr, end);
			}, nullptr, 0, 1);
			return;
		}
	}

	// 2. 决定负载类型 (Payload)
	using PayloadT = std::conditional_t<has_args,
		cmd_call<std::decay_t<Fn>, std::decay_t<Args>...>,
		FnRaw>;

	constexpr bool is_trivial = std::is_trivially_copyable_v<PayloadT> &&
	                            std::is_trivially_destructible_v<PayloadT>;

	// 3. 提取共享的静态派发指针
	static constexpr auto fptr = +[](std::byte* base, const std::byte* end) static -> invoke_fn_return_type{
		static constexpr std::size_t offset = []{
			if constexpr(is_trivial){
				return align_forward(sizeof(instr_header), alignof(PayloadT));
			} else{
				return align_forward(
					sizeof(instr_header) + sizeof(typename call_stream<Allocator>::resource_handle_fn),
					alignof(PayloadT));
			}
		}();
		static constexpr std::size_t total_size = align_forward(offset + sizeof(PayloadT),
		                                                        alignof(instr_header));

		ATTR_FORCEINLINE_SENTENCE {
			auto& obj = *std::launder(reinterpret_cast<PayloadT*>(base + offset));
			if constexpr(!has_args){
				(void)obj();
			} else{
				(void)std::apply(obj.callable, obj.args);
			}
		}

		std::byte* next_ptr = base + total_size;
		CMD_CALL_TAIL_DISPATCH(next_ptr, end);
	};

	// 4. 根据类型特性分配与完美转发构造
	if constexpr(is_trivial){
		if constexpr(has_args){
			PayloadT payload(std::forward<Fn>(fn), std::forward<Args>(args)...);
			this->emit_instruction(fptr, &payload, sizeof(PayloadT), alignof(PayloadT));
		} else{
			this->emit_instruction(fptr, &fn, sizeof(PayloadT), alignof(PayloadT));
		}
	} else if constexpr(std::is_nothrow_move_constructible_v<PayloadT>){
		// 直接就地完美转发给底层 placement new，省去了创建中间 cmd_call 的开销
		if constexpr(has_args){
			this->template emit_non_trivial_call_inline<PayloadT>(fptr, std::forward<Fn>(fn), std::forward<Args>(args)...);
		} else{
			this->template emit_non_trivial_call_inline<PayloadT>(fptr, std::forward<Fn>(fn));
		}
	} else{
		// 非平凡对象的堆分配路径
		using TypedAlloc = typename std::allocator_traits<Allocator>::template rebind_alloc<PayloadT>;
		TypedAlloc alloc(this->get_allocator());
		PayloadT* heap_obj = std::allocator_traits<TypedAlloc>::allocate(alloc, 1);

		try{
			// 完美转发至堆上构建
			if constexpr(has_args){
				std::allocator_traits<TypedAlloc>::construct(alloc, heap_obj, std::forward<Fn>(fn), std::forward<Args>(args)...);
			} else{
				std::allocator_traits<TypedAlloc>::construct(alloc, heap_obj, std::forward<Fn>(fn));
			}
		} catch(...){
			std::allocator_traits<TypedAlloc>::deallocate(alloc, heap_obj, 1);
			throw;
		}

		try{
			this->emit_non_trivial_call_heap(
				+[](std::byte* base, const std::byte* end) static -> invoke_fn_return_type{
					static constexpr std::size_t payload_offset = align_forward(
						sizeof(instr_header) + sizeof(typename call_stream<Allocator>::resource_handle_fn),
						alignof(PayloadT*));
					static constexpr std::size_t total_size = align_forward(
						payload_offset + sizeof(PayloadT*), alignof(instr_header));

					ATTR_FORCEINLINE_SENTENCE {
						auto& obj_ptr = *std::launder(reinterpret_cast<PayloadT**>(base + payload_offset));
						if constexpr(!has_args){
							(void)(*obj_ptr)();
						} else{
							(void)std::apply(obj_ptr->callable, std::move(obj_ptr->args));
						}
					}

					std::byte* next_ptr = base + total_size;
					CMD_CALL_TAIL_DISPATCH(next_ptr, end);
				},
				heap_obj,
				+[] FORCE_INLINE (call_stream<Allocator>& s, void* old_base, void* new_base) noexcept{
					if(new_base) return;
					constexpr std::size_t p_off = align_forward(
						sizeof(instr_header) + sizeof(typename call_stream<Allocator>::resource_handle_fn),
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

template <typename Allocator>
template <typename Fn, typename... Args>
void call_stream<Allocator>::push_back(const cmd_call<Fn, Args...>& call){
	std::apply([&]<typename ...Ts>(const Ts&... unpacked_args) {
		this->emplace_back(call.callable, unpacked_args...);
	}, call.args);
}

template <typename Allocator>
template <typename Fn, typename... Args>
void call_stream<Allocator>::push_back(cmd_call<Fn, Args...>&& call){
	std::apply([&]<typename... T0>(T0&&... unpacked_args) {
		this->emplace_back(std::move(call.callable), std::forward<T0>(unpacked_args)...);
	}, std::move(call.args));
}

}
