module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.style.tree;

import std;
import align;
import mo_yanxi.function_manipulate;

export import :interface;
export import mo_yanxi.gui.style.interface;

namespace mo_yanxi::gui::style{
export
template <std::ranges::forward_range Container>
struct tree_fork{
	ADAPTED_NO_UNIQUE_ADDRESS Container children;
	using value_type = std::ranges::range_value_t<Container>;
	using target_type = node_trait<std::remove_cvref_t<std::ranges::range_reference_t<Container>>>::target_type;
	static_assert(!std::is_void_v<target_type>, "tree_fork must has a clear target type");

	[[nodiscard]] tree_fork() = default;

	//TODO add std::from_range tag for this constructor
	template <typename Rng>
	[[nodiscard]] explicit(false) tree_fork(Rng&& children)
		: children(std::forward<Rng>(children)){
	}

	template <typename ...Ts>
		requires std::constructible_from<Container, Ts&&...>
	[[nodiscard]] explicit tree_fork(Ts&& ...args)
		: children(std::forward<Ts>(args)...){
	}

	void record(draw_recorder& ctx) const{
		for(const auto& child : children){
			style::draw_record(child, ctx);
		}
	}

	void direct(const typed_draw_param<target_type>& p) const{
		for(const auto& child : children){
			style::draw_direct(child, p);
		}
	}
};

export
template <std::ranges::forward_range Container>
struct tree_metrics_fork{
	ADAPTED_NO_UNIQUE_ADDRESS Container children;
	using target_type = typename node_trait<std::remove_cvref_t<std::ranges::range_reference_t<Container>>>::target_type
	;
	static_assert(!std::is_void_v<target_type>, "tree_metrics_fork must has a clear target type");

	[[nodiscard]] tree_metrics_fork() = default;

	template <typename Rng>
	[[nodiscard]] explicit(false) tree_metrics_fork(Rng&& children)
		: children(std::forward<Rng>(children)){
	}

	[[nodiscard]] style_tree_metrics query_metrics(const style_tree_metrics_query_param& p = {}) const noexcept{
		style_tree_metrics rst{};
		for(const auto& child : children){
			rst.merge_from(style::query_metrics(child, p));
		}
		return rst;
	}
};

export
template <typename... Children>
struct tree_tuple_fork{
	static_assert(sizeof...(Children) > 0);
	using target_type = typename node_trait<Children...>::target_type;
	static_assert((std::is_same_v<target_type, typename node_trait<Children>::target_type> && ...));

	ADAPTED_NO_UNIQUE_ADDRESS std::tuple<Children...> children;

	template <typename... Ts>
	explicit tree_tuple_fork(Ts&&... ts) : children(std::forward<Ts>(ts)...){
	}

	template <typename... Ts>
	explicit tree_tuple_fork(std::tuple<Ts...>&& ts) : children(std::move(ts)){
	}

	template <typename... Ts>
	explicit tree_tuple_fork(const std::tuple<Ts...>& ts) : children(ts){
	}

	void record(draw_recorder& ctx) const{
		std::apply([&ctx]<typename... Ts>(const Ts&... child){
			([&]{
				if constexpr(style_tree_recordable<std::remove_cvref_t<Ts>>){
					style::draw_record(child, ctx);
				}
			}(), ...);
		}, children);
	}

	void direct(const typed_draw_param<target_type>& p) const{
		std::apply([&]<typename... Ts>(const Ts&... child){
			([&]{
				if constexpr(style_tree_direct_drawable<std::remove_cvref_t<Ts>>){
					style::draw_direct(child, p);
				}
			}(), ...);
		}, children);
	}

	[[nodiscard]] style_tree_metrics query_metrics(const style_tree_metrics_query_param& p = {}) const noexcept{
		style_tree_metrics rst{};
		std::apply([&]<typename... Ts>(const Ts&... child){
			([&]{
				if constexpr(style_tree_metrics_queryable<std::remove_cvref_t<Ts>>){
					rst.merge_from(style::query_metrics(child, p));
				}
			}(), ...);
		}, children);
		return rst;
	}
};

export
template <typename Child, typename OnEnter, typename OnLeave>
struct tree_scope{
	using target_type = node_trait<Child>::target_type;
	using enter_fn_type = OnEnter;
	using leave_fn_type = OnLeave;

	ADAPTED_NO_UNIQUE_ADDRESS enter_fn_type on_enter{};
	ADAPTED_NO_UNIQUE_ADDRESS leave_fn_type on_leave{};
	ADAPTED_NO_UNIQUE_ADDRESS Child child{};

	[[nodiscard]] tree_scope() = default;

	template <typename EnterArg, typename LeaveArg, typename ChildArg>
	[[nodiscard]] explicit(false) tree_scope(EnterArg&& on_enter, LeaveArg&& on_leave, ChildArg&& child)
		: on_enter(std::forward<EnterArg>(on_enter)),
		  on_leave(std::forward<LeaveArg>(on_leave)),
		  child(std::forward<ChildArg>(child)){
	}

	void record(draw_recorder& ctx) const{
		if(!present(child)) return;

		ctx.push_call_enter(*this, [](const tree_scope& self, const draw_call_param& p) static -> draw_call_param{
			return self.enter(p);
		});

		style::draw_record(child, ctx);

		if constexpr(std::is_null_pointer_v<OnLeave>){
			ctx.push_call_leave();
		} else{
			ctx.push_call_leave(*this, [](const tree_scope& self, const draw_call_param& p) static{
				self.leave(p);
			});
		}
	}

	void direct(const typed_draw_param<target_type>& p) const{
		if(!present(child)) return;
		if(auto entered = this->enter(p.param)){
			style::draw_direct(child, typed_draw_param<target_type>{entered});
			if constexpr(!std::is_null_pointer_v<OnLeave>){
				this->leave(entered);
			}
		}
	}

private:
	[[nodiscard]] draw_call_param enter(const draw_call_param& p) const{
		if constexpr(std::is_null_pointer_v<OnEnter>){
			return p;
		} else{
			return std::invoke_r<draw_call_param>(on_enter, *this, typed_draw_param<target_type>{p});
		}
	}

	void leave(const draw_call_param& p) const{
		if constexpr(!std::is_null_pointer_v<OnLeave>){
			std::invoke(on_leave, *this, typed_draw_param<target_type>{p});
		}
	}

public:
	[[nodiscard]] style_tree_metrics query_metrics(const style_tree_metrics_query_param& p = {}) const noexcept{
		if constexpr(style_tree_metrics_queryable<Child>){
			return style::query_metrics(child, p);
		} else{
			return {};
		}
	}
};

export
template <typename DrawFn, typename T = void>
struct tree_leaf{
	using target_type = std::conditional_t<
		std::is_void_v<T> && unambiguous_function<DrawFn>,
		typename node_trait<std::remove_cvref_t<typename function_arg_at_last<DrawFn>::type>>::target_type,
		T>;
	static_assert(!std::is_void_v<target_type>, "tree_fork must has a clear target type");
	static_assert(std::invocable<const DrawFn&, typed_draw_param<target_type>>,
	              "tree_fork must has a clear target type");

	ADAPTED_NO_UNIQUE_ADDRESS DrawFn draw_fn;

	[[nodiscard]] tree_leaf() = default;

	template <typename Fn>
	[[nodiscard]] explicit(false) tree_leaf(Fn&& draw_fn)
		: draw_fn(std::forward<Fn>(draw_fn)){
	}

	template <typename Fn>
	[[nodiscard]] explicit(false) tree_leaf(Fn&& draw_fn, std::in_place_type_t<T>)
		: draw_fn(std::forward<Fn>(draw_fn)){
	}

	void record(draw_recorder& ctx) const{
		ctx.push_call_noop(*this, [](const tree_leaf& self, const draw_call_param& p) static{
			self.direct(typed_draw_param<target_type>{p});
		});
	}

	void direct(const typed_draw_param<target_type>& p) const{
		std::invoke(draw_fn, p);
	}
};

export
template <typename MetricsFn, typename T = void>
struct tree_metrics_leaf{
	using target_type = std::conditional_t<
		std::is_void_v<T> && unambiguous_function<MetricsFn>,
		typename node_trait<std::remove_cvref_t<typename function_arg_at_last<MetricsFn>::type>>::target_type,
		T>;
	static_assert(!std::is_void_v<target_type>, "style_tree_metrics_leaf must has a clear target type");
	static_assert(
		std::invocable<const MetricsFn&> || std::invocable<
			const MetricsFn&, typed_style_tree_metrics_query_param<target_type>>,
		"style_tree_metrics_leaf expects a nullary metrics fn or one taking typed_style_tree_metrics_query_param<T>");

	ADAPTED_NO_UNIQUE_ADDRESS MetricsFn metrics_fn;

	[[nodiscard]] tree_metrics_leaf() = default;

	template <typename Fn>
	[[nodiscard]] explicit(false) tree_metrics_leaf(Fn&& metrics_fn)
		: metrics_fn(std::forward<Fn>(metrics_fn)){
	}

	template <typename Fn>
	[[nodiscard]] explicit(false) tree_metrics_leaf(Fn&& metrics_fn, std::in_place_type_t<T>)
		: metrics_fn(std::forward<Fn>(metrics_fn)){
	}

	[[nodiscard]] style_tree_metrics query_metrics(const style_tree_metrics_query_param& p = {}) const noexcept{
		if constexpr(std::invocable<const MetricsFn&, typed_style_tree_metrics_query_param<target_type>>){
			return std::invoke(metrics_fn, typed_style_tree_metrics_query_param<target_type>{p});
		} else{
			return std::invoke(metrics_fn);
		}
	}
};

export
template <typename Child, typename Predicate>
struct tree_router_static{
	using target_type = node_trait<Child>::target_type;
	ADAPTED_NO_UNIQUE_ADDRESS Predicate predicate{};
	ADAPTED_NO_UNIQUE_ADDRESS Child child{};

	[[nodiscard]] tree_router_static() = default;

	template <typename ChildArg>
	[[nodiscard]] explicit(false) tree_router_static(ChildArg&& child)
		: child(std::forward<ChildArg>(child)){
	}

	template <typename PredicateArg, typename ChildArg>
	[[nodiscard]] explicit(false) tree_router_static(PredicateArg&& predicate, ChildArg&& child)
		: predicate(std::forward<PredicateArg>(predicate)),
		  child(std::forward<ChildArg>(child)){
	}

	void record(draw_recorder& ctx) const{
		if(!present(child)) return;
		if(!allow_record()) return;

		style::draw_record(child, ctx);
	}

	void direct(const typed_draw_param<target_type>& p) const{
		if(!present(child)) return;
		if(!allow_record()) return;

		style::draw_direct(child, p);
	}

private:
	[[nodiscard]] constexpr bool allow_record() const{
		if constexpr(style::is_null_pred_v<Predicate>){
			return true;
		} else{
			static_assert(redundantly_bool_invocable<const Predicate&, const tree_router_static&, const Child&>,
			              "Static router predicate must be nullptr or redundantly invocable by router/child context");
			return static_cast<bool>(mo_yanxi::invoke_redundantly(predicate, *this, child));
		}
	}

public:
	[[nodiscard]] style_tree_metrics query_metrics(const style_tree_metrics_query_param& p = {}) const noexcept{
		if(!allow_record()){
			return {};
		}

		if constexpr(style_tree_metrics_queryable<Child>){
			return style::query_metrics(child, p);
		} else{
			return {};
		}
	}
};

export
template <typename Child, typename Predicate>
struct tree_router_dynamic{
	using target_type = node_trait<Child>::target_type;

	static_assert(
				redundantly_bool_invocable<
				const Predicate&,
				const tree_router_dynamic&, const Child&, const typed_draw_param<target_type>&>,
				"Dynamic router predicate must be nullptr or redundantly invocable by router/child/direct param context");

	ADAPTED_NO_UNIQUE_ADDRESS Predicate predicate{};
	ADAPTED_NO_UNIQUE_ADDRESS Child child{};

	[[nodiscard]] tree_router_dynamic() = default;

	template <typename ChildArg>
	[[nodiscard]] explicit(false) tree_router_dynamic(ChildArg&& child)
		: child(std::forward<ChildArg>(child)){
	}

	template <typename PredicateArg, typename ChildArg>
	[[nodiscard]] explicit(false) tree_router_dynamic(PredicateArg&& predicate, ChildArg&& child)
		: predicate(std::forward<PredicateArg>(predicate)),
		  child(std::forward<ChildArg>(child)){
	}

	void record(draw_recorder& ctx) const{
		if(!present(child)) return;

		ctx.push_call_enter(
			*this, [](const tree_router_dynamic& self, const draw_call_param& p) static -> draw_call_param{
				return self.route_param(typed_draw_param<target_type>{p});
			});
		style::draw_record(child, ctx);
		ctx.push_call_leave();
	}

	void direct(const typed_draw_param<target_type>& p) const{
		if(!present(child)) return;
		if(!this->allow_draw(p)) return;

		style::draw_direct(child, p);
	}

private:
	[[nodiscard]] bool allow_draw(const typed_draw_param<target_type>& p) const{
		return static_cast<bool>(mo_yanxi::invoke_redundantly(predicate, *this, child, p));
	}

	[[nodiscard]] draw_call_param route_param(const typed_draw_param<target_type>& p) const{
		if(allow_draw(p)){
			return p;
		}

		return style::mask_out_draw(p);
	}

public:
	[[nodiscard]] style_tree_metrics query_metrics(const style_tree_metrics_query_param& p = {}) const noexcept{
		if constexpr(style_tree_metrics_queryable<Child>){
			return style::query_metrics(child, p);
		} else{
			return {};
		}
	}
};

export
template <typename Child>
struct tree_direct{
	using target_type = node_trait<Child>::target_type;
	static_assert(!std::is_void_v<target_type>, "tree_direct must has a clear target type");

	ADAPTED_NO_UNIQUE_ADDRESS Child child;

	[[nodiscard]] tree_direct() = default;

	template <typename ChildArg>
	[[nodiscard]] explicit(false) tree_direct(ChildArg&& child)
		: child(std::forward<ChildArg>(child)){
	}

	void record(draw_recorder& ctx) const{
		if(!present(child)) return;
		ctx.push_call_noop(*this, [](const tree_direct& self, const draw_call_param& p) static{
			style::draw_direct(self.child, typed_draw_param<target_type>{p});
		});
	}

	void direct(const typed_draw_param<target_type>& p) const{
		if(!present(child)) return;
		style::draw_direct(child, p);
	}
};

#pragma region Deductions

template <typename Rng>
tree_fork(Rng&&) -> tree_fork<std::decay_t<Rng>>;

template <typename Rng>
tree_metrics_fork(Rng&&) -> tree_metrics_fork<std::decay_t<Rng>>;

template <typename... Ts>
tree_tuple_fork(Ts&&...) -> tree_tuple_fork<std::decay_t<Ts>...>;

template <typename... Ts>
tree_tuple_fork(std::tuple<Ts...>&&) -> tree_tuple_fork<std::decay_t<Ts>...>;

template <typename... Ts>
tree_tuple_fork(const std::tuple<Ts...>&) -> tree_tuple_fork<std::decay_t<Ts>...>;

template <typename OnEnter, typename OnLeave, typename Child>
tree_scope(OnEnter&&, OnLeave&&,
           Child&&) -> tree_scope<std::decay_t<Child>, std::decay_t<OnEnter>, std::decay_t<OnLeave>>;

template <typename DrawFn>
tree_leaf(DrawFn&&) -> tree_leaf<std::decay_t<DrawFn>>;

template <typename DrawFn, typename T>
tree_leaf(DrawFn&&, std::in_place_type_t<T>) -> tree_leaf<std::decay_t<DrawFn>, T>;

template <typename MetricsFn>
tree_metrics_leaf(MetricsFn&&) -> tree_metrics_leaf<std::decay_t<MetricsFn>>;

template <typename MetricsFn, typename T>
tree_metrics_leaf(MetricsFn&&, std::in_place_type_t<T>) -> tree_metrics_leaf<std::decay_t<MetricsFn>, T>;

template <typename Child>
tree_router_static(Child&&) -> tree_router_static<std::decay_t<Child>, std::nullptr_t>;

template <typename Predicate, typename Child>
tree_router_static(Predicate&&, Child&&) -> tree_router_static<std::decay_t<Child>, std::decay_t<Predicate>>;

template <typename Child>
tree_router_dynamic(Child&&) -> tree_router_dynamic<std::decay_t<Child>, std::nullptr_t>;

template <typename Predicate, typename Child>
tree_router_dynamic(Predicate&&, Child&&) -> tree_router_dynamic<std::decay_t<Child>, std::decay_t<Predicate>>;

template <typename Child>
tree_direct(Child&&) -> tree_direct<std::decay_t<Child>>;
#pragma endregion
}
