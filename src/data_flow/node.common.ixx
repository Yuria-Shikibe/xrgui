module;

#include <cassert>

export module mo_yanxi.react_flow.common;

export import mo_yanxi.react_flow;
import mo_yanxi.meta_programming;
import std;

namespace mo_yanxi::react_flow{
enum struct stoa_err{
	empty,
	out_of_range,
	invalid_input
};

export 
template <typename T>
struct stoa_result : std::expected<T, stoa_err>{
	using std::expected<T, stoa_err>::expected;

	[[nodiscard]] stoa_result() : std::expected<T, stoa_err>(std::unexpect, stoa_err::empty){
	}

};

template <typename Arth, typename Arg>
stoa_result<Arth> stoa(const char* src, const char* dst, Arg from_chars_argument) noexcept{
	if(src == dst){
		return std::unexpected{stoa_err::empty};
	}
	Arth val;
	const auto rst = std::from_chars(src, dst, val, from_chars_argument);
	switch(rst.ec){
	case std::errc{}:
		return val;
	case std::errc::invalid_argument:
		return std::unexpected{stoa_err::invalid_input};
	case std::errc::result_out_of_range:
		return std::unexpected{stoa_err::out_of_range};
	default: std::unreachable();
	}
}

template <typename Arth, typename Rng, typename Arg>
	requires (std::is_arithmetic_v<Arth>)
stoa_result<Arth> stoa(const Rng& rng, Arg from_chars_argument){
	if constexpr (std::is_same_v<std::ranges::range_value_t<Rng>, char>){
		if constexpr (std::ranges::contiguous_range<Rng>){
			const auto size = std::ranges::distance(rng);
			const auto* data = std::ranges::data(rng);
			return react_flow::stoa<Arth>(data, data + size, from_chars_argument);
		}else{
			return react_flow::stoa<Arth>(rng | std::ranges::to<std::string>(), from_chars_argument);
		}
	}else if constexpr (std::convertible_to<std::ranges::range_value_t<Rng>, char>){
		return react_flow::stoa<Arth>(rng | std::ranges::to<std::string>(), from_chars_argument);
	}else{
		static_assert(false, "range not supported");
	}
}

export
template <typename T>
std::string to_string(const stoa_result<T>& rst){
	if(rst){
		return std::format("{}", rst.value());
	} else{
		switch(rst.error()){
		case stoa_err::empty : return "empty";
		case stoa_err::out_of_range : return "out_of_range";
		case stoa_err::invalid_input : return "invalid_input";
		default : std::unreachable();
		}
	}
}

export
template <typename Arth, typename Str>
struct string_to_arth : relay<stoa_result<Arth>, Str>{
	using return_type = stoa_result<Arth>;
	using from_chars_param_type = std::conditional_t<std::floating_point<Arth>, std::chars_format, int>;

	from_chars_param_type from_chars_argument = []{
		if constexpr (std::floating_point<Arth>){
			return std::chars_format::general;
		}else{
			return 10;
		}
	}();

	using relay<stoa_result<Arth>, Str>::relay;

	[[nodiscard]] explicit string_to_arth(
		from_chars_param_type from_chars_argument)
	: from_chars_argument(from_chars_argument){
	}

	[[nodiscard]] string_to_arth(
		propagate_behavior data_propagate_type,
		from_chars_param_type from_chars_argument)
	: relay<std::expected<Arth, stoa_err>, Str>(data_propagate_type),
	from_chars_argument(from_chars_argument){
	}

protected:
	return_type operator()(data_package_optimal<Str>&& input) override{
		return this->operator()(*input.get());
	}

	return_type operator()(const Str& input) override{
		return react_flow::stoa<Arth>(input, from_chars_argument);
	}

	return_type operator()(Str&& input) override{
		return react_flow::stoa<Arth>(input, from_chars_argument);
	}

};

export
template <typename Fn, typename Arg>
struct listener : terminal<Arg>{
	Fn fn;

	template <typename FnTy>
		requires (std::constructible_from<Fn, FnTy&&>)
	[[nodiscard]] listener(propagate_behavior data_propagate_type, FnTy&& fn)
	: terminal<Arg>(data_propagate_type),
	fn(std::forward<FnTy>(fn)){
	}

	[[nodiscard]] explicit listener(const Fn& fn)
	: fn(fn){
	}

protected:
	void on_update(const Arg& data) override{
		std::invoke(fn, data);
	}
};

export
template <typename Fn>
auto make_listener(propagate_behavior data_propagate_type, Fn&& fn){
	using FnTrait = mo_yanxi::function_traits<Fn>::mem_func_args_type;
	static_assert(std::tuple_size_v<FnTrait> == 1);
	return listener<std::decay_t<Fn>, std::tuple_element_t<0, FnTrait>>{data_propagate_type, std::forward<Fn>(fn)};
}

export
template <typename Fn>
auto make_listener(Fn&& fn){
	return react_flow::make_listener(propagate_behavior::eager, std::forward<Fn>(fn));
}

}