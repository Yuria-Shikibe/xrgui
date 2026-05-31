module;

#include <mo_yanxi/adapted_attributes.hpp>
#include <cassert>

export module mo_yanxi.function_call_stack;

import std;
import mo_yanxi.function_manipulate;

namespace mo_yanxi{



template <typename T, typename... Args>
concept has_static_call_operator = requires(Args... args){
	{ T::operator()(std::forward<Args>(args)...) };
};

using host_ptr_t = void*;

enum stack_op_t{
	stack_noop,
	stack_enter,
	stack_leave,
	stack_replace,
};


export
template <typename StackArg, typename Allocator = std::allocator<std::byte>, typename... Ts>
struct function_call_stack{
	using stack_argument_t = StackArg;
	using allocator_type = Allocator;

	static constexpr bool is_stack_argument_bool_evaluatable = std::constructible_from<bool, stack_argument_t>;

	static_assert(std::is_same_v<typename std::allocator_traits<allocator_type>::value_type, std::byte>,
	              "Allocator's value_type must be std::byte to prevent accidental template bloat.");

	using call_fn_type = void(*)(host_ptr_t, const stack_argument_t&, Ts...);
	using call_fn_with_ret_type = stack_argument_t(*)(host_ptr_t, const stack_argument_t&, Ts...);

	struct call_unit{
		host_ptr_t host;
		stack_op_t stack_op;

		union{
			call_fn_type fn;
			call_fn_with_ret_type fn_with_ret;
		};
	};

private:
	using param_allocator_t = typename std::allocator_traits<allocator_type>::template rebind_alloc<stack_argument_t>;
	using call_allocator_t = typename std::allocator_traits<allocator_type>::template rebind_alloc<call_unit>;

	std::vector<stack_argument_t, param_allocator_t> arguments;
	std::vector<call_unit, call_allocator_t> calls;

public:
	function_call_stack() noexcept(noexcept(allocator_type())) = default;

	explicit function_call_stack(const allocator_type& alloc) noexcept
		: arguments(alloc), calls(alloc){
	}

	allocator_type get_allocator() const noexcept{
		return allocator_type{arguments.get_allocator()};
	}

	void each(const stack_argument_t& initial_param, Ts... args){
		if(calls.empty()) return;
		assert(!arguments.empty());

		stack_argument_t* param_stack_pointer = arguments.data();
		*param_stack_pointer = initial_param;

		unsigned inactive_counter = 0;
		if constexpr(is_stack_argument_bool_evaluatable){
			if(!static_cast<bool>(initial_param)){
				inactive_counter = 1;
			}
		}

		for(auto&& call : calls){
			switch(call.stack_op){
			case stack_noop :{
				if(inactive_counter == 0 && call.fn != nullptr){
					call.fn(call.host, *param_stack_pointer, std::forward<Ts>(args)...);
				}
				break;
			}
			case stack_enter :{
				stack_argument_t rst;
				if(inactive_counter == 0){
					rst = call.fn_with_ret(call.host, *param_stack_pointer, std::forward<Ts>(args)...);
				} else{
					rst = *param_stack_pointer;
				}

				++param_stack_pointer;
				*param_stack_pointer = rst;

				if(inactive_counter == 0){
					if constexpr(is_stack_argument_bool_evaluatable){
						if(!static_cast<bool>(rst)) inactive_counter = 1;
					}
				} else{
					inactive_counter++;
				}
				break;
			}
			case stack_leave :{
				if(inactive_counter == 0 && call.fn != nullptr){
					call.fn(call.host, *param_stack_pointer, std::forward<Ts>(args)...);
				}

				--param_stack_pointer;

				if(inactive_counter > 0) inactive_counter--;
				break;
			}
			case stack_replace :{
				if(inactive_counter > 0) inactive_counter--;

				stack_argument_t rst;
				const stack_argument_t& parent_arg = *(param_stack_pointer - 1);
				if(inactive_counter == 0){
					rst = call.fn_with_ret(call.host, parent_arg, std::forward<Ts>(args)...);
				} else{
					rst = parent_arg;
				}

				*param_stack_pointer = rst;

				if(inactive_counter == 0){
					if constexpr(is_stack_argument_bool_evaluatable){
						if(!static_cast<bool>(rst)) inactive_counter = 1;
					}
				} else{
					inactive_counter++;
				}
				break;
			}
			default : std::unreachable();
			}
		}
	}

public:
	struct function_call_stack_builder{
		using stack_type = function_call_stack;
		stack_type& stack;

	private:
		std::uint32_t current_build_depth{};
		std::uint32_t max_build_depth{};

		void ensure_argument_stack_size(std::uint32_t depth){
			const auto required_size = static_cast<std::size_t>(depth) + 1;
			if(stack.arguments.size() < required_size){
				stack.arguments.resize(required_size);
			}
		}

	public:
		[[nodiscard]] explicit function_call_stack_builder(stack_type& stack_ref)
			: stack(stack_ref){
			begin_push();
		}

		~function_call_stack_builder(){
			end_push();
		}

		[[nodiscard]] const stack_type& get_stack() const noexcept{
			return stack;
		}

		[[nodiscard]] stack_type& get_stack() noexcept{
			return stack;
		}

		void begin_push() noexcept{
			stack.calls.clear();
			stack.arguments.clear();
			current_build_depth = 0;
			max_build_depth = 0;
		}

		void push_call(call_unit call_unit){
			// 自动窥孔优化：合并无作用的 leave 与接下来的 enter 为 replace
			if(call_unit.stack_op == stack_enter && !stack.calls.empty()){
				auto& last_call = stack.calls.back();
				if(last_call.stack_op == stack_leave && last_call.fn == nullptr){
					// 将上一个 leave 节点就地覆写为 replace
					last_call.stack_op = stack_replace;
					last_call.host = call_unit.host;
					last_call.fn_with_ret = call_unit.fn_with_ret;

					// 修正深度：之前的 leave 导致 current_build_depth 减了 1。
					// 转变为同级 replace 后，需要将这 1 补回来。
					current_build_depth++;
					if(current_build_depth > max_build_depth){
						max_build_depth = current_build_depth;
						this->ensure_argument_stack_size(max_build_depth);
					}
					return;
				}
			}

			switch(call_unit.stack_op){
			case stack_noop :
				// assert(current_build_depth != 0);
				this->ensure_argument_stack_size(current_build_depth);
				break;
			case stack_enter : current_build_depth++;
				if(current_build_depth > max_build_depth){
					max_build_depth = current_build_depth;
					this->ensure_argument_stack_size(max_build_depth);
				}
				break;
			case stack_leave :
				assert(current_build_depth != 0);
				current_build_depth--;
				break;
			case stack_replace :
				assert(current_build_depth != 0);
				break;
			default : std::unreachable();
			}
			stack.calls.push_back(call_unit);
		}

		void end_push() noexcept{
			assert(current_build_depth == 0);
			assert(stack.calls.empty() || stack.arguments.size() >= static_cast<std::size_t>(max_build_depth) + 1);
		}

		template <typename T, typename Fn>
			requires std::is_invocable_r_v<stack_argument_t, Fn, T&, const stack_argument_t&, Ts...>
		void push_call_enter(T& host, Fn /*fn*/){
			static_assert(std::is_empty_v<Fn>);
			this->push_call({
					.host = const_cast<host_ptr_t>(static_cast<const volatile void*>(std::addressof(host))),
					.stack_op = stack_enter,
					.fn_with_ret = +[](host_ptr_t h, const stack_argument_t& p, Ts... args) static -> stack_argument_t{
						if constexpr(has_static_call_operator<Fn, T&, const stack_argument_t&, Ts...>){
							return Fn::operator()(*static_cast<T*>(h), p, std::forward<Ts>(args)...);
						} else{
							static_assert(std::is_default_constructible_v<Fn>,
							              "Fn must be a default-constructible stateless functor");
							return std::invoke_r<stack_argument_t>(Fn{}, *static_cast<T*>(h), p,
							                                       std::forward<Ts>(args)...);
						}
					}
				});
		}

		template <typename T, std::invocable<T&, const stack_argument_t&, Ts...> Fn>
		void push_call_leave(T& host, Fn /*fn*/){
			static_assert(std::is_empty_v<Fn>);
			this->push_call({
					.host = const_cast<host_ptr_t>(static_cast<const volatile void*>(std::addressof(host))),
					.stack_op = stack_leave,
					.fn = +[](host_ptr_t h, const stack_argument_t& p, Ts... args) static{
						if constexpr(has_static_call_operator<Fn, T&, const stack_argument_t&, Ts...>){
							Fn::operator()(*static_cast<T*>(h), p, std::forward<Ts>(args)...);
						} else{
							static_assert(std::is_default_constructible_v<Fn>,
							              "Fn must be a default-constructible stateless functor");
							std::invoke(Fn{}, *static_cast<T*>(h), p, std::forward<Ts>(args)...);
						}
					}
				});
		}

		template <typename T, typename Fn>
			requires std::is_invocable_r_v<stack_argument_t, Fn, T&, const stack_argument_t&, Ts...>
		void push_call_replace(T& host, Fn /*fn*/){
			static_assert(std::is_empty_v<Fn>);
			this->push_call({
					.host = const_cast<host_ptr_t>(static_cast<const volatile void*>(std::addressof(host))),
					.stack_op = stack_replace,
					.fn_with_ret = +[](host_ptr_t h, const stack_argument_t& p, Ts... args) static -> stack_argument_t{
						if constexpr(has_static_call_operator<Fn, T&, const stack_argument_t&, Ts...>){
							return Fn::operator()(*static_cast<T*>(h), p, std::forward<Ts>(args)...);
						} else{
							static_assert(std::is_default_constructible_v<Fn>,
										  "Fn must be a default-constructible stateless functor");
							return std::invoke_r<stack_argument_t>(Fn{}, *static_cast<T*>(h), p,
							                                       std::forward<Ts>(args)...);
						}
					}
				});
		}

		template <typename T, std::invocable<T&, const stack_argument_t&, Ts...> Fn>
		void push_call_noop(T& host, Fn /*fn*/){
			static_assert(std::is_empty_v<Fn>);
			this->push_call({
					.host = const_cast<host_ptr_t>(static_cast<const volatile void*>(std::addressof(host))),
					.stack_op = stack_noop,
					.fn = +[](host_ptr_t h, const stack_argument_t& p, Ts... args) static{
						if constexpr(has_static_call_operator<Fn, T&, const stack_argument_t&, Ts...>){
							Fn::operator()(*static_cast<T*>(h), p, std::forward<Ts>(args)...);
						} else{
							static_assert(std::is_default_constructible_v<Fn>,
										  "Fn must be a default-constructible stateless functor");
							std::invoke(Fn{}, *static_cast<T*>(h), p, std::forward<Ts>(args)...);
						}
					}
				});
		}

		template <typename T, typename Fn>
		void push_call_enter(T& host, Fn /*fn*/){
			this->push_call_enter(host, mo_yanxi::make_func_wrapper<T&, const stack_argument_t&, Ts...>(Fn{}));
		}

		template <typename T, typename Fn>
		void push_call_leave(T& host, Fn /*fn*/){
			this->push_call_leave(host, mo_yanxi::make_func_wrapper<T&, const stack_argument_t&, Ts...>(Fn{}));
		}

		template <typename T, typename Fn>
		void push_call_replace(T& host, Fn /*fn*/){
			this->push_call_replace(host, mo_yanxi::make_func_wrapper<T&, const stack_argument_t&, Ts...>(Fn{}));
		}

		template <typename T, typename Fn>
		void push_call_noop(T& host, Fn /*fn*/){
			this->push_call_noop(host, mo_yanxi::make_func_wrapper<T&, const stack_argument_t&, Ts...>(Fn{}));
		}

		void push_call_leave(){
			this->push_call({
					.host = nullptr,
					.stack_op = stack_leave,
					.fn = nullptr
				});
		}

	};
};

export
template <typename StackArg, typename Allocator = std::allocator<std::byte>, typename... Ts>
using function_call_stack_builder = function_call_stack<StackArg, Allocator, Ts...>::function_call_stack_builder;
}
