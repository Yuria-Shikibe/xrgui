//
// Created by Matrix on 2026/3/3.
//

export module mo_yanxi.gui.util.observable_value;

import std;

namespace mo_yanxi::gui::util{

export
template<typename T, typename ...Fn>
	requires (std::equality_comparable<T>)
struct observable_value{
	static_assert(std::is_object_v<T>);
	using function_table = std::tuple<Fn...>;

private:
	T last_{};
	function_table funcs_{};

public:
	observable_value() = default;

	observable_value(const T& last, function_table& channels)
		: last_(last),
		funcs_(channels){
	}

	template <typename Ty, typename ...Fns>
		requires (std::convertible_to<Ty&&, T>)
	explicit observable_value(Ty&& last, Fns&& ...fns)
		: last_(std::forward<Ty>(last)),
		funcs_(std::make_tuple(std::forward<Fns>(fns))...){
	}

	template <typename... Fns>
	explicit observable_value(Fns&&... fns)
		: funcs_(std::make_tuple(std::forward<Fns>(fns))...){
	}

	explicit observable_value(function_table& channels)
		: funcs_(channels){
	}

	template <typename SourceIDT>
	void operator()(SourceIDT&& source_identity, T&& value){
		if(last_ == value)return;
		last_ = std::move(value);
		[&]<std::size_t ...Idx>(std::index_sequence<Idx...>){
			([&]<std::size_t I>{
				using FnTy = std::tuple_element_t<I, function_table>;
				if constexpr (std::invocable<FnTy, SourceIDT&&, const T&>){
					std::invoke(std::get<I>(funcs_), std::forward<SourceIDT>(source_identity), last_);
				}else if constexpr (std::invocable<FnTy, const T&>){
					std::invoke(std::get<I>(funcs_), last_);
				}
			}.template operator()<Idx>(), ...);
		}(std::index_sequence_for<Fn...>{});
	}

	template <typename SourceIDT>
	void operator()(SourceIDT&& source_identity,const T& value){
		this->operator()(source_identity, auto(value));
	}

	void operator()(T&& value){
		if(last_ == value)return;
		last_ = std::move(value);
		[&]<std::size_t ...Idx>(std::index_sequence<Idx...>){
			([&]<std::size_t I>{
				using FnTy = std::tuple_element_t<I, function_table>;
				if constexpr (std::invocable<FnTy, const T&>){
					std::invoke(std::get<I>(funcs_), last_);
				}
			}.template operator()<Idx>(), ...);
		}(std::index_sequence_for<Fn...>{});
	}

	void operator()(const T& value){
		this->operator()(auto(value));
	}

};

template <typename Ty, typename ...Fns>
observable_value(Ty&&, Fns&& ...) -> observable_value<std::decay_t<Ty>, std::decay_t<Fns>...>;
}