module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.style.tree;

import std;
import align;
import mo_yanxi.function_manipulate;

export import :interface;
export import mo_yanxi.gui.style.interface;

namespace mo_yanxi::gui::style {

template <typename Self, typename... Args>
concept not_single_self_arg = !(sizeof...(Args) == 1 && (std::same_as<Self, std::remove_cvref_t<Args>> && ...));

template <typename Predicate>
inline constexpr bool is_null_pred_v = std::is_null_pointer_v<std::remove_cvref_t<Predicate>>;

template <typename Fn, typename... Args>
concept redundantly_bool_invocable = mo_yanxi::redundantly_invocable<Fn, Args...>
	&& requires(Fn&& fn, Args&&... args) {
		{
			mo_yanxi::invoke_redundantly(std::forward<Fn>(fn), std::forward<Args>(args)...)
		} -> std::convertible_to<bool>;
	};

export
template <std::ranges::forward_range Container>
struct tree_fork {
	ADAPTED_NO_UNIQUE_ADDRESS Container children;

	using value_type = std::ranges::range_value_t<Container>;
	using target_type = node_trait<std::remove_cvref_t<std::ranges::range_reference_t<Container>>>::target_type;
	static_assert(!std::is_void_v<target_type>, "tree_fork must have a clear target type");

	[[nodiscard]] tree_fork() = default;

	template <typename Rng>
		requires not_single_self_arg<tree_fork, Rng>
	[[nodiscard]] explicit(false) tree_fork(Rng&& value)
		: children(std::forward<Rng>(value)) {
	}

	template <typename... Ts>
		requires std::constructible_from<Container, Ts&&...>
	[[nodiscard]] explicit tree_fork(Ts&&... args)
		: children(std::forward<Ts>(args)...) {
	}

	void record(draw_recorder& ctx) const {
		for(const auto& child : children) {
			style::draw_record(child, ctx);
		}
	}

	FORCE_INLINE void direct(const typed_draw_param<target_type>& p) const {
		for(const auto& child : children) {
			style::draw_direct(child, p);
		}
	}
};

export
template <std::ranges::forward_range Container>
struct tree_metrics_fork {
	ADAPTED_NO_UNIQUE_ADDRESS Container children;

	using target_type = node_trait<std::remove_cvref_t<std::ranges::range_reference_t<Container>>>::target_type;
	static_assert(!std::is_void_v<target_type>, "tree_metrics_fork must have a clear target type");

	[[nodiscard]] tree_metrics_fork() = default;

	template <typename Rng>
		requires not_single_self_arg<tree_metrics_fork, Rng>
	[[nodiscard]] explicit(false) tree_metrics_fork(Rng&& value)
		: children(std::forward<Rng>(value)) {
	}

	[[nodiscard]] style_tree_metrics query_metrics(
		const typed_style_tree_metrics_query_param<target_type>& p = {}) const noexcept {
		style_tree_metrics rst{};
		for(const auto& child : children) {
			rst.merge_from(style::query_metrics(child, p));
		}
		return rst;
	}
};

export
template <typename... Children>
struct tree_tuple_fork {
	static_assert(sizeof...(Children) > 0);

	using target_type = typename node_trait<Children...>::target_type;
	static_assert((std::is_same_v<target_type, typename node_trait<Children>::target_type> && ...));

	ADAPTED_NO_UNIQUE_ADDRESS std::tuple<Children...> children;

	tree_tuple_fork(const tree_tuple_fork&) = default;
	tree_tuple_fork(tree_tuple_fork&&) noexcept = default;
	tree_tuple_fork& operator=(const tree_tuple_fork&) = default;
	tree_tuple_fork& operator=(tree_tuple_fork&&) noexcept = default;

	template <typename... Ts>
		requires not_single_self_arg<tree_tuple_fork, Ts...>
			&& std::constructible_from<std::tuple<Children...>, Ts&&...>
	explicit tree_tuple_fork(Ts&&... values)
		: children(std::forward<Ts>(values)...) {
	}

	template <typename... Ts>
	explicit tree_tuple_fork(std::tuple<Ts...>&& values)
		: children(std::move(values)) {
	}

	template <typename... Ts>
	explicit tree_tuple_fork(const std::tuple<Ts...>& values)
		: children(values) {
	}

	void record(draw_recorder& ctx) const {
		std::apply([&ctx]<typename... Ts>(const Ts&... child) {
			([&] {
				if constexpr(style::style_tree_recordable<std::remove_cvref_t<Ts>>) {
					style::draw_record(child, ctx);
				}
			}(), ...);
		}, children);
	}

	FORCE_INLINE void direct(const typed_draw_param<target_type>& p) const {
		std::apply([&]<typename... Ts>(const Ts&... child) {
			([&] {
				if constexpr(style::style_tree_direct_drawable<std::remove_cvref_t<Ts>>) {
					style::draw_direct(child, p);
				}
			}(), ...);
		}, children);
	}

	[[nodiscard]] style_tree_metrics query_metrics(
		const typed_style_tree_metrics_query_param<target_type>& p = {}) const noexcept {
		style_tree_metrics rst{};
		std::apply([&]<typename... Ts>(const Ts&... child) {
			([&] {
				if constexpr(style::style_tree_metrics_queryable<std::remove_cvref_t<Ts>>) {
					rst.merge_from(style::query_metrics(child, p));
				}
			}(), ...);
		}, children);
		return rst;
	}
};

export
template <typename OnEnter, typename OnLeave, typename Child>
struct tree_scope {
	using target_type = node_trait<Child>::target_type;
	using enter_fn_type = OnEnter;
	using leave_fn_type = OnLeave;

	static_assert(
		strictly_redundantly_invocable<const enter_fn_type&, tree_scope, typed_draw_param<target_type>>,
		"disallow implicit convert of typed draw param to avoid current subject offset");

	ADAPTED_NO_UNIQUE_ADDRESS enter_fn_type on_enter{};
	ADAPTED_NO_UNIQUE_ADDRESS leave_fn_type on_leave{};
	ADAPTED_NO_UNIQUE_ADDRESS Child child{};

	[[nodiscard]] tree_scope() = default;

	template <typename EnterArg, typename LeaveArg, typename ChildArg>
	[[nodiscard]] explicit(false) tree_scope(EnterArg&& enter, LeaveArg&& leave, ChildArg&& value)
		: on_enter(std::forward<EnterArg>(enter)),
		  on_leave(std::forward<LeaveArg>(leave)),
		  child(std::forward<ChildArg>(value)) {
	}

	void record(draw_recorder& ctx) const {
		if(!style::present(child)) {
			return;
		}

		ctx.push_call_enter(*this, [](const tree_scope& self, const draw_call_param& p,
		                              const draw_immut_args& immut_args) static -> draw_call_param {
			return self.enter(typed_draw_param<target_type>{.param = p, .immut_args = immut_args});
		});

		style::draw_record(child, ctx);

		if constexpr(std::is_null_pointer_v<OnLeave>) {
			ctx.push_call_leave();
		} else {
			ctx.push_call_leave(*this, [](const tree_scope& self, const draw_call_param& p,
			                              const draw_immut_args& immut_args) static {
				self.leave(p, immut_args);
			});
		}
	}

	FORCE_INLINE void direct(const typed_draw_param<target_type>& p) const {
		if(!style::present(child)) {
			return;
		}
		if(auto entered = this->enter(p)) {
			style::draw_direct(child, typed_draw_param<target_type>{.param = entered, .immut_args = p.immut_args});
			if constexpr(!std::is_null_pointer_v<OnLeave>) {
				this->leave(entered, p.immut_args);
			}
		}
	}

private:
	[[nodiscard]] draw_call_param enter(const typed_draw_param<target_type>& p) const {
		return mo_yanxi::invoke_redundantly(on_enter, *this, p);
	}

	void leave(const draw_call_param& p, const draw_immut_args& immut_args) const {
		if constexpr(!std::is_null_pointer_v<OnLeave>) {
			std::invoke(on_leave, *this, typed_draw_param<target_type>{.param = p, .immut_args = immut_args});
		}
	}

public:
	[[nodiscard]] style_tree_metrics query_metrics(
		const typed_style_tree_metrics_query_param<target_type>& p = {}) const noexcept {
		auto rst = style::query_metrics(child, p);
		rst.merge_from(style::get_scope_inset(on_enter));
		return rst;
	}
};

export
template <typename DrawFn, typename T = void>
struct tree_leaf {
	using target_type = behavior::node_target_from_explicit_or_callable_t<T, DrawFn>;
	static_assert(!std::is_void_v<target_type>, "tree_leaf must have a clear target type");
	static_assert(std::invocable<const DrawFn&, typed_draw_param<target_type>>,
	              "tree_leaf draw function must accept typed_draw_param<target_type>");

	ADAPTED_NO_UNIQUE_ADDRESS DrawFn draw_fn;

	[[nodiscard]] tree_leaf() = default;

	template <typename Fn>
		requires std::invocable<const Fn&, typed_draw_param<target_type>>
	[[nodiscard]] explicit(false) tree_leaf(Fn&& fn)
		: draw_fn(std::forward<Fn>(fn)) {
	}

	template <typename Fn>
		requires std::invocable<const Fn&, typed_draw_param<target_type>>
	[[nodiscard]] explicit(false) tree_leaf(Fn&& fn, std::in_place_type_t<T>)
		: draw_fn(std::forward<Fn>(fn)) {
	}

	void record(draw_recorder& ctx) const {
		ctx.push_call_noop(*this, [](const tree_leaf& self, const draw_call_param& p,
		                             const draw_immut_args& immut_args) static {
			self.direct(typed_draw_param<target_type>{.param = p, .immut_args = immut_args});
		});
	}

	FORCE_INLINE void direct(const typed_draw_param<target_type>& p) const {
		std::invoke(draw_fn, p);
	}
};

export
template <typename MetricsFn, typename T = void>
struct tree_metrics_leaf {
	using target_type = behavior::node_target_from_explicit_or_callable_t<T, MetricsFn>;
	static_assert(!std::is_void_v<target_type>, "tree_metrics_leaf must have a clear target type");
	static_assert(
		std::invocable<const MetricsFn&>
			|| std::invocable<const MetricsFn&, typed_style_tree_metrics_query_param<target_type>>,
		"tree_metrics_leaf expects a nullary metrics function or one taking typed_style_tree_metrics_query_param<T>");

	ADAPTED_NO_UNIQUE_ADDRESS MetricsFn metrics_fn;

	[[nodiscard]] tree_metrics_leaf() = default;

	template <typename Fn>
	[[nodiscard]] explicit(false) tree_metrics_leaf(Fn&& fn)
		: metrics_fn(std::forward<Fn>(fn)) {
	}

	template <typename Fn>
	[[nodiscard]] explicit(false) tree_metrics_leaf(Fn&& fn, std::in_place_type_t<T>)
		: metrics_fn(std::forward<Fn>(fn)) {
	}

	[[nodiscard]] style_tree_metrics query_metrics(
		const typed_style_tree_metrics_query_param<target_type>& p = {}) const noexcept {
		if constexpr(std::invocable<const MetricsFn&, typed_style_tree_metrics_query_param<target_type>>) {
			return std::invoke(metrics_fn, p);
		} else {
			return std::invoke(metrics_fn);
		}
	}
};

export
template <typename Predicate, typename Child>
struct tree_router_static {
	using target_type = node_trait<Child>::target_type;

	ADAPTED_NO_UNIQUE_ADDRESS Predicate predicate{};
	ADAPTED_NO_UNIQUE_ADDRESS Child child{};

	[[nodiscard]] tree_router_static() = default;

	template <typename ChildArg>
		requires not_single_self_arg<tree_router_static, ChildArg>
	[[nodiscard]] explicit(false) tree_router_static(ChildArg&& value)
		: child(std::forward<ChildArg>(value)) {
	}

	template <typename PredicateArg, typename ChildArg>
	[[nodiscard]] explicit(false) tree_router_static(PredicateArg&& pred, ChildArg&& value)
		: predicate(std::forward<PredicateArg>(pred)),
		  child(std::forward<ChildArg>(value)) {
	}

	void record(draw_recorder& ctx) const {
		if(!style::present(child)) {
			return;
		}
		if(!this->allow_record()) {
			return;
		}
		style::draw_record(child, ctx);
	}

	FORCE_INLINE void direct(const typed_draw_param<target_type>& p) const {
		if(!style::present(child)) {
			return;
		}
		if(!this->allow_record()) {
			return;
		}
		style::draw_direct(child, p);
	}

private:
	[[nodiscard]] constexpr bool allow_record() const {
		if constexpr(is_null_pred_v<Predicate>) {
			return true;
		} else {
			static_assert(redundantly_bool_invocable<const Predicate&, const tree_router_static&, const Child&>,
			              "Static router predicate must be null or redundantly invocable by router/child context");
			return static_cast<bool>(mo_yanxi::invoke_redundantly(predicate, *this, child));
		}
	}

public:
	[[nodiscard]] style_tree_metrics query_metrics(
		const typed_style_tree_metrics_query_param<target_type>& p = {}) const noexcept {
		if(!this->allow_record()) {
			return {};
		}
		return style::query_metrics(child, p);
	}
};

export
template <typename Predicate, typename Child>
struct tree_router_dynamic {
	using target_type = node_trait<Child>::target_type;

	static_assert(
		is_null_pred_v<Predicate>
			|| redundantly_bool_invocable<
				const Predicate&,
				const tree_router_dynamic&,
				const Child&,
				const typed_draw_param<target_type>&,
				const draw_immut_args&>,
		"Dynamic router predicate must be null or redundantly invocable by router/child/direct param context");

	ADAPTED_NO_UNIQUE_ADDRESS Predicate predicate{};
	ADAPTED_NO_UNIQUE_ADDRESS Child child{};

	[[nodiscard]] tree_router_dynamic() = default;

	template <typename ChildArg>
		requires not_single_self_arg<tree_router_dynamic, ChildArg>
	[[nodiscard]] explicit(false) tree_router_dynamic(ChildArg&& value)
		: child(std::forward<ChildArg>(value)) {
	}

	template <typename PredicateArg, typename ChildArg>
	[[nodiscard]] explicit(false) tree_router_dynamic(PredicateArg&& pred, ChildArg&& value)
		: predicate(std::forward<PredicateArg>(pred)),
		  child(std::forward<ChildArg>(value)) {
	}

	void record(draw_recorder& ctx) const {
		if(!style::present(child)) {
			return;
		}

		ctx.push_call_enter(
			*this,
			[](const tree_router_dynamic& self, const draw_call_param& p,
			   const draw_immut_args& immut_args) static -> draw_call_param {
				return self.route_param(typed_draw_param<target_type>{.param = p, .immut_args = immut_args});
			});
		style::draw_record(child, ctx);
		ctx.push_call_leave();
	}

	FORCE_INLINE void direct(const typed_draw_param<target_type>& p) const {
		if(!style::present(child)) {
			return;
		}
		if(!this->allow_draw(p)) {
			return;
		}
		style::draw_direct(child, p);
	}

private:
	[[nodiscard]] bool allow_draw(const typed_draw_param<target_type>& p) const {
		if constexpr(is_null_pred_v<Predicate>) {
			return true;
		} else {
			return static_cast<bool>(mo_yanxi::invoke_redundantly(predicate, *this, child, p, p.immut_args));
		}
	}

	[[nodiscard]] draw_call_param route_param(const typed_draw_param<target_type>& p) const {
		if(this->allow_draw(p)) {
			return p;
		}
		return style::mask_out_draw(p);
	}

public:
	[[nodiscard]] style_tree_metrics query_metrics(
		const typed_style_tree_metrics_query_param<target_type>& p = {}) const noexcept {
		return style::query_metrics(child, p);
	}
};

export
template <typename Child>
struct tree_direct {
	using target_type = node_trait<Child>::target_type;
	static_assert(!std::is_void_v<target_type>, "tree_direct must have a clear target type");

	ADAPTED_NO_UNIQUE_ADDRESS Child child;

	[[nodiscard]] tree_direct() = default;

	template <typename ChildArg>
		requires not_single_self_arg<tree_direct, ChildArg>
	[[nodiscard]] explicit(false) tree_direct(ChildArg&& value)
		: child(std::forward<ChildArg>(value)) {
	}

	void record(draw_recorder& ctx) const {
		if(!style::present(child)) {
			return;
		}
		ctx.push_call_noop(*this, [](const tree_direct& self, const draw_call_param& p,
		                             const draw_immut_args& immut_args) static {
			style::draw_direct(self.child, typed_draw_param<target_type>{.param = p, .immut_args = immut_args});
		});
	}

	FORCE_INLINE void direct(const typed_draw_param<target_type>& p) const {
		if(!style::present(child)) {
			return;
		}
		style::draw_direct(child, p);
	}

	[[nodiscard]] style_tree_metrics query_metrics(
		const typed_style_tree_metrics_query_param<target_type>& p = {}) const noexcept {
		return style::query_metrics(child, p);
	}
};

constexpr inline std::size_t max_mask_width = 32;

export
struct style_config {
	std::bitset<max_mask_width> used_layer{};

	FORCE_INLINE constexpr bool has_layer(const fx::layer_param& param) const noexcept {
		if(used_layer.none()) [[unlikely]] {
			return true;
		} else [[likely]] {
			return used_layer[param.layer_index];
		}
	}
};

export
template <std::uint32_t Layers>
struct style_config_static {
	FORCE_INLINE static constexpr bool has_layer(const fx::layer_param& param) noexcept {
		if constexpr(Layers == 0) {
			return true;
		} else {
			assert(param.layer_index < std::numeric_limits<decltype(Layers)>::digits);
			return Layers & (1 << param.layer_index);
		}
	}
};

export constexpr style_config layer_top_only{{0b1}};

export
template <unsigned Count>
	requires (Count < max_mask_width && Count > 0)
constexpr style_config layer_draw_until{{(1uz << Count) - 1uz}};

export
struct layer_router_pred {
	style_config config;

	[[nodiscard]] constexpr bool operator()(const draw_immut_args& args) const noexcept {
		return config.has_layer(args.layer);
	}
};

export
template <std::size_t Layers>
struct layer_router_static_pred {
	[[nodiscard]] constexpr bool operator()(const draw_immut_args& args) const noexcept {
		if constexpr(Layers == 0) {
			return true;
		} else {
			assert(args.layer.layer_index < std::numeric_limits<decltype(Layers)>::digits);
			return Layers & (1uz << args.layer.layer_index);
		}
	}
};

export
template <typename Child>
struct layer_router : tree_router_dynamic<layer_router_pred, Child> {
	using base_type = tree_router_dynamic<layer_router_pred, Child>;
	using target_type = typename base_type::target_type;

	template <typename ChildArg>
	[[nodiscard]] explicit(false) layer_router(style_config config, ChildArg&& child)
		: base_type(layer_router_pred{config}, std::forward<ChildArg>(child)) {
	}

	template <typename ChildArg>
	[[nodiscard]] explicit(false) layer_router(ChildArg&& child)
		: base_type(layer_router_pred{style_config{0b1}}, std::forward<ChildArg>(child)) {
	}
};

export
template <std::size_t Layers, typename Child>
struct layer_router_static : tree_router_dynamic<layer_router_static_pred<Layers>, Child> {
	using base_type = tree_router_dynamic<layer_router_static_pred<Layers>, Child>;
	using target_type = typename base_type::target_type;

	template <std::size_t L, typename ChildArg>
	[[nodiscard]] explicit(false) layer_router_static(std::in_place_index_t<L>, ChildArg&& child)
		: base_type(layer_router_static_pred<Layers>{}, std::forward<ChildArg>(child)) {
	}

	template <typename ChildArg>
	[[nodiscard]] explicit(false) layer_router_static(ChildArg&& child)
		: base_type(layer_router_static_pred<Layers>{}, std::forward<ChildArg>(child)) {
	}
};

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
tree_scope(OnEnter&&, OnLeave&&, Child&&)
	-> tree_scope<std::decay_t<OnEnter>, std::decay_t<OnLeave>, std::decay_t<Child>>;

template <typename DrawFn>
tree_leaf(DrawFn&&) -> tree_leaf<std::decay_t<DrawFn>>;

template <typename DrawFn, typename T>
tree_leaf(DrawFn&&, std::in_place_type_t<T>) -> tree_leaf<std::decay_t<DrawFn>, T>;

template <typename MetricsFn>
tree_metrics_leaf(MetricsFn&&) -> tree_metrics_leaf<std::decay_t<MetricsFn>>;

template <typename MetricsFn, typename T>
tree_metrics_leaf(MetricsFn&&, std::in_place_type_t<T>) -> tree_metrics_leaf<std::decay_t<MetricsFn>, T>;

template <typename Child>
tree_router_static(Child&&) -> tree_router_static<std::nullptr_t, std::decay_t<Child>>;

template <typename Predicate, typename Child>
tree_router_static(Predicate&&, Child&&) -> tree_router_static<std::decay_t<Predicate>, std::decay_t<Child>>;

template <typename Child>
tree_router_dynamic(Child&&) -> tree_router_dynamic<std::nullptr_t, std::decay_t<Child>>;

template <typename Predicate, typename Child>
tree_router_dynamic(Predicate&&, Child&&) -> tree_router_dynamic<std::decay_t<Predicate>, std::decay_t<Child>>;

template <typename Child>
tree_direct(Child&&) -> tree_direct<std::decay_t<Child>>;

template <typename Child>
layer_router(style_config, Child&&) -> layer_router<std::decay_t<Child>>;

template <typename Child>
layer_router(Child&&) -> layer_router<std::decay_t<Child>>;

template <std::size_t Layers, typename Child>
layer_router_static(std::in_place_index_t<Layers>, Child&&) -> layer_router_static<Layers, std::decay_t<Child>>;

template <typename Child>
layer_router_static(Child&&) -> layer_router_static<1, std::decay_t<Child>>;

}
