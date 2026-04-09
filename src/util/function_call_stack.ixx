module;

#include <mo_yanxi/adapted_attributes.hpp>
#include <cassert>

export module mo_yanxi.function_call_stack;

import std;

namespace mo_yanxi{
template <typename T, typename... Args>
concept has_static_call_operator = requires(Args... args){
	{ T::operator()(std::forward<Args>(args)...) };
};

using guard_fn_type = void(*)(std::span<std::byte>);
using host_ptr_t = void*;

struct alignas(16) guard_unit_head{
	guard_fn_type on_exit;
	std::uint32_t prev_guard_pos;
	std::uint32_t payload_alignment;
};

enum stack_op_t{
	stack_noop,
	stack_enter,
	stack_leave,
};

static constexpr std::uint32_t invalid_guard_pos = ~0U;

export
template <typename StackArg, typename Allocator = std::allocator<std::byte>>
struct function_call_stack_builder;

export
template <typename StackArg, typename Allocator = std::allocator<std::byte>>
struct function_call_stack{
	friend struct function_call_stack_builder<StackArg, Allocator>;

	using stack_argument_t = StackArg;
	using allocator_type = Allocator;

	static_assert(std::is_same_v<typename std::allocator_traits<allocator_type>::value_type, std::byte>,
	              "Allocator's value_type must be std::byte to prevent accidental template bloat.");

	struct call_param_unit{
		stack_argument_t stack_arg;
		std::uint32_t guard_stack_pos;

		constexpr call_param_unit& operator=(const stack_argument_t& p) noexcept{
			stack_arg = p;
			return *this;
		}
	};

	using call_fn_type = void(*)(host_ptr_t, const stack_argument_t&, function_call_stack&);
	using call_fn_with_ret_type = stack_argument_t(*)(host_ptr_t, const stack_argument_t&, function_call_stack&);

	struct alignas(16) call_unit{
		host_ptr_t host;
		stack_op_t stack_op;

		union{
			call_fn_type fn;
			call_fn_with_ret_type fn_with_ret;
		};
	};

private:
	using param_allocator_t = typename std::allocator_traits<allocator_type>::template rebind_alloc<call_param_unit>;
	using call_allocator_t = typename std::allocator_traits<allocator_type>::template rebind_alloc<call_unit>;

	std::vector<std::byte, allocator_type> guards;
	std::vector<call_param_unit, param_allocator_t> arguments;
	std::vector<call_unit, call_allocator_t> calls;

	call_param_unit* param_stack_pointer{};
	std::uint32_t current_guard_head_pos = invalid_guard_pos;

public:
	function_call_stack() noexcept(noexcept(allocator_type())) = default;

	explicit function_call_stack(const allocator_type& alloc) noexcept
		: guards(alloc), arguments(alloc), calls(alloc){
	}

	allocator_type get_allocator() const noexcept{
		return guards.get_allocator();
	}

	void each(const stack_argument_t& initial_param){
		if(calls.empty()) return;
		assert(!arguments.empty());

		guards.clear();
		current_guard_head_pos = invalid_guard_pos;

		param_stack_pointer = arguments.data();
		*param_stack_pointer = initial_param;
		param_stack_pointer->guard_stack_pos = invalid_guard_pos;

		try{
			for(auto&& call : calls){
				switch(call.stack_op){
				case stack_noop : call.fn(call.host, param_stack_pointer->stack_arg, *this);
					break;
				case stack_enter :{
					auto rst = call.fn_with_ret(call.host, param_stack_pointer->stack_arg, *this);
					++param_stack_pointer;
					*param_stack_pointer = rst;
					param_stack_pointer->guard_stack_pos = current_guard_head_pos;
					break;
				}
				case stack_leave :{
					call.fn(call.host, param_stack_pointer->stack_arg, *this);
					--param_stack_pointer;
					std::uint32_t target_guard_pos = param_stack_pointer->guard_stack_pos;
					guard_on_exit(target_guard_pos);
					break;
				}
				default : std::unreachable();
				}
			}

			guard_on_exit(invalid_guard_pos);
		} catch(...){
			while(current_guard_head_pos != invalid_guard_pos){
				try{
					guard_on_exit(invalid_guard_pos);
				} catch(...){
				}
			}


			guards.clear();
			current_guard_head_pos = invalid_guard_pos;
			throw;
		}
	}

	void push_guard(guard_unit_head guard_head, const void* payload_ptr, std::size_t payload_size){
		guard_head.prev_guard_pos = current_guard_head_pos;
		const auto head_pos = static_cast<std::uint32_t>(guards.size());
		current_guard_head_pos = head_pos;

		const std::size_t current_size = head_pos + sizeof(guard_unit_head);
		const std::size_t alignment = guard_head.payload_alignment;
		const std::size_t padding = (alignment - (current_size % alignment)) % alignment;
		const std::size_t payload_pos = current_size + padding;

		std::size_t block_end = payload_pos + payload_size;
		constexpr std::size_t head_align = alignof(guard_unit_head);
		block_end = (block_end + head_align - 1) & ~(head_align - 1);

		guards.resize(block_end);

		std::memcpy(guards.data() + head_pos, &guard_head, sizeof(guard_unit_head));
		if(payload_size > 0 && payload_ptr != nullptr){
			std::memcpy(guards.data() + payload_pos, payload_ptr, payload_size);
		}
	}

	template <std::invocable<> Fn>
	void push_guard(Fn&& fn){
		using FnTy = std::decay_t<Fn>;
		static_assert(std::is_trivially_copyable_v<FnTy> && std::is_trivially_destructible_v<FnTy>);

		this->push_guard({
			                 .on_exit = +[](std::span<std::byte> payloads){
				                 auto* fn_ptr = std::launder(reinterpret_cast<FnTy*>(payloads.data()));
				                 std::invoke(*fn_ptr);
			                 },
			                 .prev_guard_pos = 0,
			                 .payload_alignment = static_cast<std::uint16_t>(alignof(FnTy))
		                 }, std::addressof(fn), sizeof(FnTy));
	}

private:
	void guard_on_exit(std::uint32_t target_pos){
		std::size_t next_boundary_pos = guards.size();

		while(current_guard_head_pos != target_pos && current_guard_head_pos != invalid_guard_pos){
			const auto* head = reinterpret_cast<guard_unit_head*>(guards.data() + current_guard_head_pos);
			std::size_t head_end_pos = current_guard_head_pos + sizeof(guard_unit_head);
			std::size_t alignment = head->payload_alignment;
			std::size_t padding = (alignment - (head_end_pos % alignment)) % alignment;
			std::size_t payload_pos = head_end_pos + padding;

			std::size_t dynamic_payload_size = next_boundary_pos - payload_pos;
			std::span<std::byte> payload_span{guards.data() + payload_pos, dynamic_payload_size};


			auto exit_fn = head->on_exit;
			std::uint32_t next_guard_pos = head->prev_guard_pos;


			next_boundary_pos = current_guard_head_pos;
			current_guard_head_pos = next_guard_pos;

			if(exit_fn){
				exit_fn(payload_span);
			}
		}

		if(target_pos == invalid_guard_pos){
			guards.clear();
		}
	}
};

template <typename StackArg, typename Allocator>
struct function_call_stack_builder{
	using stack_argument_t = StackArg;
	using allocator_type = Allocator;
	using stack_type = function_call_stack<stack_argument_t, allocator_type>;
	stack_type& stack;

private:
	std::uint32_t current_build_depth{};
	std::uint32_t max_build_depth{};

public:
	[[nodiscard]] explicit function_call_stack_builder(stack_type& stack_ref)
		: stack(stack_ref){
		begin_push();
	}

	~function_call_stack_builder(){
		end_push();
	}

	void begin_push() noexcept{
		stack.calls.clear();
		stack.arguments.clear();
	}

	void push_call(typename stack_type::call_unit call_unit){
		switch(call_unit.stack_op){
		case stack_noop :
			assert(current_build_depth != 0);
			break;
		case stack_enter : current_build_depth++;
			if(current_build_depth > max_build_depth){
				max_build_depth = current_build_depth;
			}
			break;
		case stack_leave :
			assert(current_build_depth != 0);
			current_build_depth--;
			break;
		default : std::unreachable();
		}
		stack.calls.push_back(call_unit);
	}

	void end_push(){
		stack.arguments.resize(max_build_depth + 1);
	}

	template <typename T, typename Fn>
		requires std::is_invocable_r_v<stack_argument_t, Fn, T&, const stack_argument_t&, stack_type&>
	void push_call_enter(T& host, Fn /*fn*/){
		this->push_call({
				.host = const_cast<host_ptr_t>(static_cast<const volatile void*>(std::addressof(host))),
				.stack_op = stack_enter,
				.fn_with_ret = +[](host_ptr_t h, const stack_argument_t& p, stack_type& q) static -> stack_argument_t{
					if constexpr(has_static_call_operator<Fn, T&, const stack_argument_t&, stack_type&>){
						return Fn::operator()(*static_cast<T*>(h), p, q);
					} else{
						static_assert(std::is_default_constructible_v<Fn>, "Fn must be a default-constructible stateless functor");
						return std::invoke_r<stack_argument_t>(Fn{}, *static_cast<T*>(h), p, q);
					}
				}
			});
	}

	template <typename T, std::invocable<T&, const stack_argument_t&, stack_type&> Fn>
	void push_call_leave(T& host, Fn /*fn*/){
		this->push_call({
				.host = const_cast<host_ptr_t>(static_cast<const volatile void*>(std::addressof(host))),
				.stack_op = stack_leave,
				.fn = +[](host_ptr_t h, const stack_argument_t& p, stack_type& q) static{
					if constexpr(has_static_call_operator<Fn, T&, const stack_argument_t&, stack_type&>){
						Fn::operator()(*static_cast<T*>(h), p, q);
					} else{
						static_assert(std::is_default_constructible_v<Fn>, "Fn must be a default-constructible stateless functor");
						std::invoke(Fn{}, *static_cast<T*>(h), p, q);
					}
				}
			});
	}

	template <typename T, std::invocable<T&, const stack_argument_t&, stack_type&> Fn>
	void push_call_noop(T& host, Fn /*fn*/){
		this->push_call({
				.host = const_cast<host_ptr_t>(static_cast<const volatile void*>(std::addressof(host))),
				.stack_op = stack_noop,
				.fn = +[](host_ptr_t h, const stack_argument_t& p, stack_type& q) static{
					if constexpr(has_static_call_operator<Fn, T&, const stack_argument_t&, stack_type&>){
						Fn::operator()(*static_cast<T*>(h), p, q);
					} else{
						static_assert(std::is_default_constructible_v<Fn>, "Fn must be a default-constructible stateless functor");
						std::invoke(Fn{}, *static_cast<T*>(h), p, q);
					}
				}
			});
	}
};
}
