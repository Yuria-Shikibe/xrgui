module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.behavior.tree;

import std;
import mo_yanxi.function_manipulate;
export import mo_yanxi.referenced_ptr;

namespace mo_yanxi::behavior {

export
template <typename T, typename... Ts>
struct node_trait {
	using target_type = void;
};

template <typename T, typename... Ts>
	requires requires { typename T::target_type; }
struct node_trait<T, Ts...> {
	using target_type = T::target_type;
};

export
template <typename T>
using node_target_t = node_trait<std::remove_cvref_t<T>>::target_type;

template <typename T>
concept has_declared_target_type = requires {
	typename std::remove_cvref_t<T>::target_type;
};

template <typename Fn>
struct callable_target {
	using type = void;
};

template <typename Fn>
	requires has_declared_target_type<Fn>
struct callable_target<Fn> {
	using type = std::remove_cvref_t<Fn>::target_type;
};

template <typename Fn>
	requires (!has_declared_target_type<Fn> && unambiguous_function<std::remove_cvref_t<Fn>>)
struct callable_target<Fn> {
	using argument_type = function_arg_at_last<std::remove_cvref_t<Fn>>::type;
	using type = node_target_t<argument_type>;
};

template <typename ExplicitTarget, typename Fn>
struct node_target_from_explicit_or_callable {
	using type = ExplicitTarget;
};

template <typename Fn>
struct node_target_from_explicit_or_callable<void, Fn> {
	using type = callable_target<Fn>::type;
};

export
template <typename ExplicitTarget, typename Fn>
using node_target_from_explicit_or_callable_t =
node_target_from_explicit_or_callable<ExplicitTarget, Fn>::type;

export
template <typename Domain>
using mutable_param_t = Domain::mutable_param;

export
template <typename Domain>
using immutable_args_t = Domain::immutable_args;

export
template <typename Domain>
using recorder_t = Domain::recorder;

export
template <typename Domain>
using query_param_t = Domain::query_param;

export
template <typename Domain>
using query_result_t = Domain::query_result;

export
template <typename Domain, typename Target>
using typed_execute_param_t = Domain::template typed_execute_param<Target>;

export
template <typename Domain, typename Target>
using typed_query_param_t = Domain::template typed_query_param<Target>;

template <typename T1, typename T2>
struct get_more_derived {
	static constexpr bool is_related = std::derived_from<T1, T2> || std::derived_from<T2, T1>;
	static_assert(is_related, "The two target types do not have an inheritance relationship.");
};

template <typename T1, typename T2>
	requires std::derived_from<T1, T2>
struct get_more_derived<T1, T2> {
	using type = T1;
};

template <typename T1, typename T2>
	requires (!std::derived_from<T1, T2> && std::derived_from<T2, T1>)
struct get_more_derived<T1, T2> {
	using type = T2;
};

template <typename... Ts>
struct get_most_derived;

template <>
struct get_most_derived<> {
	using type = void;
};

template <typename T>
struct get_most_derived<T> {
	using type = T;
};

template <typename T1, typename T2>
struct get_most_derived<T1, T2> {
	using type = get_more_derived<T1, T2>::type;
};

template <typename T1, typename T2, typename... Ts>
struct get_most_derived<T1, T2, Ts...> {
	using type = get_most_derived<typename get_more_derived<T1, T2>::type, Ts...>::type;
};

template <typename... Ts>
using get_most_derived_t = get_most_derived<Ts...>::type;

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
template <typename T>
concept tree_bool_testable = requires(const T& value) {
	{ static_cast<bool>(value) } -> std::convertible_to<bool>;
};

export
template <typename T>
concept tree_dereferenceable = requires(const T& value) {
	*value;
};

export
template <typename T>
concept tree_typed_target = requires {
	typename node_target_t<T>;
} && !std::is_void_v<node_target_t<T>>;

export
template <typename Domain, typename T>
concept tree_member_recordable = requires(const T& value, recorder_t<Domain>& ctx) {
	value.record(ctx);
};

export
template <typename Domain, typename T>
concept tree_arrow_recordable = requires(const T& value, recorder_t<Domain>& ctx) {
	value->record(ctx);
};

export
template <typename Domain, typename T>
concept tree_recordable = tree_member_recordable<Domain, T>
	|| tree_arrow_recordable<Domain, T>
	|| tree_dereferenceable<T>;

export
template <typename Domain, typename T>
concept tree_member_queryable = tree_typed_target<T>
	&& requires(const T& value, const typed_query_param_t<Domain, node_target_t<T>>& p) {
		{ value.query(p) } -> std::convertible_to<query_result_t<Domain>>;
	};

export
template <typename Domain, typename T>
concept tree_arrow_queryable = tree_typed_target<T>
	&& requires(const T& value, const typed_query_param_t<Domain, node_target_t<T>>& p) {
		{ value->query(p) } -> std::convertible_to<query_result_t<Domain>>;
	};

export
template <typename Domain, typename T>
concept tree_queryable = tree_member_queryable<Domain, T>
	|| tree_arrow_queryable<Domain, T>
	|| tree_dereferenceable<T>;

export
template <typename Domain, typename T, typename Target>
concept tree_executable_typed = requires(const T& value, const typed_execute_param_t<Domain, Target>& p) {
	value.execute(p);
};

export
template <typename Domain, typename T>
concept tree_executable = tree_typed_target<T>
	&& tree_executable_typed<Domain, T, node_target_t<T>>;

inline namespace cpo {
template <typename Domain>
struct present_t {
	template <typename T>
	[[nodiscard]] FORCE_INLINE constexpr bool operator()(const T& value) const noexcept {
		if constexpr(std::is_null_pointer_v<std::remove_cvref_t<T>>) {
			return false;
		} else if constexpr(tree_bool_testable<T>) {
			return static_cast<bool>(value);
		} else {
			return true;
		}
	}
};
}

export template <typename Domain>
constexpr inline cpo::present_t<Domain> present{};

inline namespace cpo {
template <typename Domain>
struct record_t {
	template <typename T>
	FORCE_INLINE void operator()(const T& node, recorder_t<Domain>& ctx) const {
		if(!behavior::present<Domain>(node)) {
			return;
		}

		if constexpr(tree_member_recordable<Domain, T>) {
			node.record(ctx);
		} else if constexpr(tree_arrow_recordable<Domain, T>) {
			node->record(ctx);
		} else if constexpr(tree_dereferenceable<T>) {
			this->operator()(*node, ctx);
		} else {
			static_assert(tree_member_recordable<Domain, T>
				|| tree_arrow_recordable<Domain, T>
				|| tree_dereferenceable<T>,
				"Behavior tree node must be recordable directly or through dereference.");
		}
	}
};

template <typename Domain>
struct execute_t {
	template <typename Target, typename T>
		requires tree_executable_typed<Domain, T, Target> && std::derived_from<Target, node_target_t<T>>
	FORCE_INLINE void operator()(const T& node, const typed_execute_param_t<Domain, Target>& p) const {
		if(!behavior::present<Domain>(node)) {
			return;
		}
		node.execute(p);
	}
};

template <typename Domain>
struct query_t {
	template <typename T>
	[[nodiscard]] FORCE_INLINE query_result_t<Domain> operator()(
		const T& node,
		const typed_query_param_t<Domain, node_target_t<T>>& p = {}) const noexcept {
		if(!behavior::present<Domain>(node)) {
			return Domain::empty_query_result();
		}

		if constexpr(tree_member_queryable<Domain, T>) {
			return node.query(p);
		} else if constexpr(tree_arrow_queryable<Domain, T>) {
			return node->query(p);
		} else if constexpr(tree_dereferenceable<T>) {
			return this->operator()(*node, p);
		} else {
			return Domain::empty_query_result();
		}
	}
};
}

export template <typename Domain>
constexpr inline cpo::record_t<Domain> record{};

export template <typename Domain>
constexpr inline cpo::execute_t<Domain> execute{};

export template <typename Domain>
constexpr inline cpo::query_t<Domain> query{};

export
template <typename Domain>
struct tree_inplace_vtable {
	using func_signature = void*(void* val, void* t);
	using query_signature = query_result_t<Domain>(*)(const void* val, const query_param_t<Domain>& p) noexcept;
	using execute_signature = void(*)(const void* val, const mutable_param_t<Domain>& p,
	                                  const immutable_args_t<Domain>& args) noexcept;

private:
	func_signature* func_ptr_{};
	query_signature query_func_ptr_{};
	execute_signature execute_func_ptr_{};

public:
	[[nodiscard]] void* clone_with_ownership(const void* val) const {
		return func_ptr_(const_cast<void*>(val), nullptr);
	}

	void record(const void* val, recorder_t<Domain>& ctx) const {
		func_ptr_(const_cast<void*>(val), &ctx);
	}

	void destroy(void* val) const noexcept {
		func_ptr_(nullptr, val);
	}

	[[nodiscard]] query_result_t<Domain> query(const void* val, const query_param_t<Domain>& p) const noexcept {
		if(query_func_ptr_ == nullptr) {
			return Domain::empty_query_result();
		}
		return query_func_ptr_(val, p);
	}

	void execute(const void* val, const mutable_param_t<Domain>& p, const immutable_args_t<Domain>& args) const noexcept {
		execute_func_ptr_(val, p, args);
	}

	template <typename EntryType, std::derived_from<EntryType> Ty,
	          std::invocable<const Ty&, recorder_t<Domain>&> RecordFn>
	[[nodiscard]] constexpr static tree_inplace_vtable create(RecordFn) noexcept {
		static constexpr auto cast_to_ty = [](void* t) static noexcept -> Ty* {
			return static_cast<Ty*>(static_cast<EntryType*>(t));
		};

		tree_inplace_vtable rst;
		rst.func_ptr_ = +[](void* val, void* t) static -> void* {
			if(val == nullptr) {
				auto* self = cast_to_ty(t);
				delete self;
				return self;
			}

			if(t == nullptr) {
				return static_cast<EntryType*>(new Ty(std::as_const(*cast_to_ty(val))));
			}

			assert(val != nullptr);
			assert(t != nullptr);
			auto& ctx = *static_cast<recorder_t<Domain>*>(t);
			auto& ty = std::as_const(*cast_to_ty(val));
			std::invoke(RecordFn{}, ty, ctx);
			return val;
		};
		rst.query_func_ptr_ = +[](const void* val, const query_param_t<Domain>& p) static noexcept -> query_result_t<Domain> {
			assert(val != nullptr);
			auto& ty = std::as_const(*cast_to_ty(const_cast<void*>(val)));
			if constexpr(tree_queryable<Domain, Ty>) {
				return behavior::query<Domain>(ty, Domain::template make_query_param<node_target_t<Ty>>(p));
			} else {
				return Domain::empty_query_result();
			}
		};
		rst.execute_func_ptr_ = +[](const void* val, const mutable_param_t<Domain>& p,
		                            const immutable_args_t<Domain>& args) static noexcept {
			auto& ty = std::as_const(*cast_to_ty(const_cast<void*>(val)));
			if constexpr(tree_executable<Domain, Ty>) {
				behavior::execute<Domain>(ty, Domain::template make_execute_param<node_target_t<Ty>>(p, args));
			}
		};
		return rst;
	}
};

export
template <typename Domain>
struct tree_node_deleter;

export
template <typename Domain>
struct tree_refable_node_base : referenced_object_persistable {
	friend tree_node_deleter<Domain>;

protected:
	tree_inplace_vtable<Domain> vtable;

public:
	[[nodiscard]] explicit tree_refable_node_base(const tree_inplace_vtable<Domain>& table)
		: vtable(table) {
	}

	[[nodiscard]] tree_refable_node_base(const tree_inplace_vtable<Domain>& table,
	                                     const tags::persistent_tag_t& persistent_tag)
		: referenced_object_persistable(persistent_tag),
		  vtable(table) {
	}

	void record(recorder_t<Domain>& ctx) const {
		vtable.record(this, ctx);
	}

	void execute(const mutable_param_t<Domain>& p, const immutable_args_t<Domain>& args) const noexcept {
		vtable.execute(this, p, args);
	}

	[[nodiscard]] query_result_t<Domain> query(const query_param_t<Domain>& p = {}) const noexcept {
		return vtable.query(this, p);
	}

	[[nodiscard]] tree_refable_node_base* clone_with_ownership() const {
		return static_cast<tree_refable_node_base*>(vtable.clone_with_ownership(this));
	}
};

export
template <typename Domain>
struct tree_node_deleter {
	static void operator()(const tree_refable_node_base<Domain>* node) noexcept {
		node->vtable.destroy(const_cast<tree_refable_node_base<Domain>*>(node));
	}
};

export
template <typename Domain>
using tree_type_erased_ptr = referenced_ptr<tree_refable_node_base<Domain>, tree_node_deleter<Domain>>;

export
template <typename Domain, typename T>
struct typed_node_ptr {
	using target_type = T;

	tree_type_erased_ptr<Domain> ptr{};

	[[nodiscard]] typed_node_ptr() = default;

	[[nodiscard]] explicit(false) typed_node_ptr(tree_type_erased_ptr<Domain> value) noexcept
		: ptr(std::move(value)) {
	}

	[[nodiscard]] explicit operator bool() const noexcept {
		return static_cast<bool>(ptr);
	}

	void record(recorder_t<Domain>& ctx) const {
		if(ptr) {
			ptr->record(ctx);
		}
	}

	void execute(const typed_execute_param_t<Domain, T>& p) const noexcept {
		if(ptr) {
			ptr->execute(Domain::template mutable_param_from_execute_param<T>(p),
			             Domain::template immutable_args_from_execute_param<T>(p));
		}
	}

	bool operator==(const typed_node_ptr&) const noexcept = default;

	[[nodiscard]] query_result_t<Domain> query(const typed_query_param_t<Domain, T>& p = {}) const noexcept {
		if(ptr) {
			return ptr->query(Domain::template query_param_from_typed_query_param<T>(p));
		}
		return Domain::empty_query_result();
	}

	[[nodiscard]] typed_node_ptr copy() const {
		return typed_node_ptr{{*ptr->clone_with_ownership()}};
	}
};

export
template <typename Domain, typename Derived>
struct tree_node : tree_refable_node_base<Domain> {
	using base_type = tree_refable_node_base<Domain>;

	[[nodiscard]] constexpr static tree_inplace_vtable<Domain> make_vtable() noexcept {
		return tree_inplace_vtable<Domain>::template create<base_type, Derived>(behavior::record<Domain>);
	}

	[[nodiscard]] tree_node()
		: base_type(make_vtable()) {
	}

	[[nodiscard]] explicit tree_node(const tags::persistent_tag_t& persistent_tag)
		: base_type(make_vtable(), persistent_tag) {
	}

	[[nodiscard]] tree_type_erased_ptr<Domain> make_shared_ptr() noexcept {
		return tree_type_erased_ptr<Domain>{static_cast<base_type&>(*this)};
	}

	[[nodiscard]] std::unique_ptr<Derived> clone() const {
		return std::unique_ptr{static_cast<Derived*>(static_cast<base_type*>(this->vtable.clone_with_ownership(this)))};
	}
};

export
template <typename Domain, typename Comp>
struct bound_tree_node : tree_node<Domain, bound_tree_node<Domain, Comp>> {
	using base_type = tree_node<Domain, bound_tree_node<Domain, Comp>>;
	using target_type = node_trait<Comp>::target_type;

	Comp comp;

	[[nodiscard]] bound_tree_node() = default;

	template <typename T>
		requires (!std::same_as<bound_tree_node<Domain, Comp>, std::remove_cvref_t<T>>)
			&& std::constructible_from<Comp, T&&>
	[[nodiscard]] explicit(false) bound_tree_node(T&& value)
		: comp(std::forward<T>(value)) {
	}

	void record(recorder_t<Domain>& ctx) const {
		behavior::record<Domain>(comp, ctx);
	}

	void execute(const typed_execute_param_t<Domain, target_type>& p) const noexcept {
		behavior::execute<Domain>(comp, p);
	}

	[[nodiscard]] query_result_t<Domain> query(const typed_query_param_t<Domain, target_type>& p = {}) const noexcept {
		return behavior::query<Domain>(comp, p);
	}
};

export
template <typename Domain, typename T>
[[nodiscard]] auto make_tree_node_ptr(T&& value) {
	auto ptr = std::make_unique<bound_tree_node<Domain, std::decay_t<T>>>(std::forward<T>(value));
	auto rst = typed_node_ptr<Domain, typename node_trait<std::remove_cvref_t<T>>::target_type>{ptr->make_shared_ptr()};
	ptr.release();
	return rst;
}

export
template <typename Domain, std::ranges::forward_range Container>
struct tree_fork {
	ADAPTED_NO_UNIQUE_ADDRESS Container children;

	using value_type = std::ranges::range_value_t<Container>;
	using target_type = node_trait<std::remove_cvref_t<std::ranges::range_reference_t<Container>>>::target_type;
	static_assert(!std::is_void_v<target_type>, "tree_fork must have a clear target type.");

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

	void record(recorder_t<Domain>& ctx) const {
		for(const auto& child : children) {
			behavior::record<Domain>(child, ctx);
		}
	}

	FORCE_INLINE void execute(const typed_execute_param_t<Domain, target_type>& p) const {
		for(const auto& child : children) {
			behavior::execute<Domain>(child, p);
		}
	}
};

export
template <typename Domain, std::ranges::forward_range Container>
struct tree_query_fork {
	ADAPTED_NO_UNIQUE_ADDRESS Container children;

	using target_type = node_trait<std::remove_cvref_t<std::ranges::range_reference_t<Container>>>::target_type;
	static_assert(!std::is_void_v<target_type>, "tree_query_fork must have a clear target type.");

	[[nodiscard]] tree_query_fork() = default;

	template <typename Rng>
		requires not_single_self_arg<tree_query_fork, Rng>
	[[nodiscard]] explicit(false) tree_query_fork(Rng&& value)
		: children(std::forward<Rng>(value)) {
	}

	[[nodiscard]] query_result_t<Domain> query(const typed_query_param_t<Domain, target_type>& p = {}) const noexcept {
		auto rst = Domain::empty_query_result();
		for(const auto& child : children) {
			Domain::merge_query_result(rst, behavior::query<Domain>(child, p));
		}
		return rst;
	}
};

export
template <typename Domain, typename... Children>
struct tree_tuple_fork {
	static_assert(sizeof...(Children) > 0);

	using target_type = node_trait<Children...>::target_type;
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

	void record(recorder_t<Domain>& ctx) const {
		std::apply([&ctx]<typename... Ts>(const Ts&... child) {
			([&] {
				if constexpr(tree_recordable<Domain, std::remove_cvref_t<Ts>>) {
					behavior::record<Domain>(child, ctx);
				}
			}(), ...);
		}, children);
	}

	FORCE_INLINE void execute(const typed_execute_param_t<Domain, target_type>& p) const {
		std::apply([&]<typename... Ts>(const Ts&... child) {
			([&] {
				if constexpr(tree_executable<Domain, std::remove_cvref_t<Ts>>) {
					behavior::execute<Domain>(child, p);
				}
			}(), ...);
		}, children);
	}

	[[nodiscard]] query_result_t<Domain> query(const typed_query_param_t<Domain, target_type>& p = {}) const noexcept {
		auto rst = Domain::empty_query_result();
		std::apply([&]<typename... Ts>(const Ts&... child) {
			([&] {
				if constexpr(tree_queryable<Domain, std::remove_cvref_t<Ts>>) {
					Domain::merge_query_result(rst, behavior::query<Domain>(child, p));
				}
			}(), ...);
		}, children);
		return rst;
	}
};

export
template <typename Domain, typename OnEnter, typename OnLeave, typename Child>
struct tree_scope {
	using target_type = node_trait<Child>::target_type;
	using enter_fn_type = OnEnter;
	using leave_fn_type = OnLeave;

	static_assert(
		strictly_redundantly_invocable<const enter_fn_type&, tree_scope, typed_execute_param_t<Domain, target_type>>,
		"Enter function must be redundantly invocable with the scope and typed execute param.");

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

	void record(recorder_t<Domain>& ctx) const {
		if(!behavior::present<Domain>(child)) {
			return;
		}

		ctx.push_call_enter(*this, [](const tree_scope& self, const mutable_param_t<Domain>& p,
		                              const immutable_args_t<Domain>& args) static -> mutable_param_t<Domain> {
			return self.enter(Domain::template make_execute_param<target_type>(p, args));
		});

		behavior::record<Domain>(child, ctx);

		if constexpr(std::is_null_pointer_v<OnLeave>) {
			ctx.push_call_leave();
		} else {
			ctx.push_call_leave(*this, [](const tree_scope& self, const mutable_param_t<Domain>& p,
			                              const immutable_args_t<Domain>& args) static {
				self.leave(p, args);
			});
		}
	}

	FORCE_INLINE void execute(const typed_execute_param_t<Domain, target_type>& p) const {
		if(!behavior::present<Domain>(child)) {
			return;
		}
		if(auto entered = this->enter(p)) {
			behavior::execute<Domain>(
				child,
				Domain::template make_execute_param<target_type>(
					entered,
					Domain::template immutable_args_from_execute_param<target_type>(p)));
			if constexpr(!std::is_null_pointer_v<OnLeave>) {
				this->leave(entered, Domain::template immutable_args_from_execute_param<target_type>(p));
			}
		}
	}

private:
	[[nodiscard]] mutable_param_t<Domain> enter(const typed_execute_param_t<Domain, target_type>& p) const {
		return mo_yanxi::invoke_redundantly(on_enter, *this, p);
	}

	void leave(const mutable_param_t<Domain>& p, const immutable_args_t<Domain>& args) const {
		if constexpr(!std::is_null_pointer_v<OnLeave>) {
			std::invoke(on_leave, *this, Domain::template make_execute_param<target_type>(p, args));
		}
	}

public:
	[[nodiscard]] query_result_t<Domain> query(const typed_query_param_t<Domain, target_type>& p = {}) const noexcept {
		auto rst = behavior::query<Domain>(child, p);
		Domain::merge_query_result(rst, Domain::scope_query_result(on_enter));
		return rst;
	}
};

export
template <typename Domain, typename ExecuteFn, typename T = void>
struct tree_leaf {
	using target_type = node_target_from_explicit_or_callable_t<T, ExecuteFn>;
	static_assert(!std::is_void_v<target_type>, "tree_leaf must have a clear target type.");
	static_assert(std::invocable<const ExecuteFn&, typed_execute_param_t<Domain, target_type>>,
	              "tree_leaf execute function must accept the domain typed execute param.");

	ADAPTED_NO_UNIQUE_ADDRESS ExecuteFn execute_fn;

	[[nodiscard]] tree_leaf() = default;

	template <typename Fn>
		requires std::invocable<const Fn&, typed_execute_param_t<Domain, target_type>>
	[[nodiscard]] explicit(false) tree_leaf(Fn&& fn)
		: execute_fn(std::forward<Fn>(fn)) {
	}

	template <typename Fn>
		requires std::invocable<const Fn&, typed_execute_param_t<Domain, target_type>>
	[[nodiscard]] explicit(false) tree_leaf(Fn&& fn, std::in_place_type_t<T>)
		: execute_fn(std::forward<Fn>(fn)) {
	}

	void record(recorder_t<Domain>& ctx) const {
		ctx.push_call_noop(*this, [](const tree_leaf& self, const mutable_param_t<Domain>& p,
		                             const immutable_args_t<Domain>& args) static {
			self.execute(Domain::template make_execute_param<target_type>(p, args));
		});
	}

	FORCE_INLINE void execute(const typed_execute_param_t<Domain, target_type>& p) const {
		std::invoke(execute_fn, p);
	}
};

export
template <typename Domain, typename QueryFn, typename T = void>
struct tree_query_leaf {
	using target_type = node_target_from_explicit_or_callable_t<T, QueryFn>;
	static_assert(!std::is_void_v<target_type>, "tree_query_leaf must have a clear target type.");
	static_assert(
		std::invocable<const QueryFn&>
			|| std::invocable<const QueryFn&, typed_query_param_t<Domain, target_type>>,
		"tree_query_leaf expects a nullary query function or one taking the domain typed query param.");

	ADAPTED_NO_UNIQUE_ADDRESS QueryFn query_fn;

	[[nodiscard]] tree_query_leaf() = default;

	template <typename Fn>
		requires (!std::same_as<tree_query_leaf<Domain, QueryFn, T>, std::remove_cvref_t<Fn>>)
			&& std::constructible_from<QueryFn, Fn&&>
	[[nodiscard]] explicit(false) tree_query_leaf(Fn&& fn)
		: query_fn(std::forward<Fn>(fn)) {
	}

	template <typename Fn>
		requires (!std::same_as<tree_query_leaf<Domain, QueryFn, T>, std::remove_cvref_t<Fn>>)
			&& std::constructible_from<QueryFn, Fn&&>
	[[nodiscard]] explicit(false) tree_query_leaf(Fn&& fn, std::in_place_type_t<T>)
		: query_fn(std::forward<Fn>(fn)) {
	}

	[[nodiscard]] query_result_t<Domain> query(const typed_query_param_t<Domain, target_type>& p = {}) const noexcept {
		if constexpr(std::invocable<const QueryFn&, typed_query_param_t<Domain, target_type>>) {
			return std::invoke(query_fn, p);
		} else {
			return std::invoke(query_fn);
		}
	}
};

export
template <typename Domain, typename Predicate, typename Child>
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

	void record(recorder_t<Domain>& ctx) const {
		if(!behavior::present<Domain>(child)) {
			return;
		}
		if(!this->allow_record()) {
			return;
		}
		behavior::record<Domain>(child, ctx);
	}

	FORCE_INLINE void execute(const typed_execute_param_t<Domain, target_type>& p) const {
		if(!behavior::present<Domain>(child)) {
			return;
		}
		if(!this->allow_record()) {
			return;
		}
		behavior::execute<Domain>(child, p);
	}

private:
	[[nodiscard]] constexpr bool allow_record() const {
		if constexpr(is_null_pred_v<Predicate>) {
			return true;
		} else {
			static_assert(redundantly_bool_invocable<const Predicate&, const tree_router_static&, const Child&>,
			              "Static router predicate must be null or redundantly invocable by router/child context.");
			return static_cast<bool>(mo_yanxi::invoke_redundantly(predicate, *this, child));
		}
	}

public:
	[[nodiscard]] query_result_t<Domain> query(const typed_query_param_t<Domain, target_type>& p = {}) const noexcept {
		if(!this->allow_record()) {
			return Domain::empty_query_result();
		}
		return behavior::query<Domain>(child, p);
	}
};

export
template <typename Domain, typename Predicate, typename Child>
struct tree_router_dynamic {
	using target_type = node_trait<Child>::target_type;

	static_assert(
		is_null_pred_v<Predicate>
			|| redundantly_bool_invocable<
				const Predicate&,
				const tree_router_dynamic&,
				const Child&,
				const typed_execute_param_t<Domain, target_type>&,
				const immutable_args_t<Domain>&>,
		"Dynamic router predicate must be null or redundantly invocable by router/child/execute context.");

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

	void record(recorder_t<Domain>& ctx) const {
		if(!behavior::present<Domain>(child)) {
			return;
		}

		ctx.push_call_enter(*this, [](const tree_router_dynamic& self, const mutable_param_t<Domain>& p,
		                              const immutable_args_t<Domain>& args) static -> mutable_param_t<Domain> {
			return self.route_param(Domain::template make_execute_param<target_type>(p, args));
		});
		behavior::record<Domain>(child, ctx);
		ctx.push_call_leave();
	}

	FORCE_INLINE void execute(const typed_execute_param_t<Domain, target_type>& p) const {
		if(!behavior::present<Domain>(child)) {
			return;
		}
		if(!this->allow_execute(p)) {
			return;
		}
		behavior::execute<Domain>(child, p);
	}

private:
	[[nodiscard]] bool allow_execute(const typed_execute_param_t<Domain, target_type>& p) const {
		if constexpr(is_null_pred_v<Predicate>) {
			return true;
		} else {
			return static_cast<bool>(mo_yanxi::invoke_redundantly(
				predicate,
				*this,
				child,
				p,
				Domain::template immutable_args_from_execute_param<target_type>(p)));
		}
	}

	[[nodiscard]] mutable_param_t<Domain> route_param(const typed_execute_param_t<Domain, target_type>& p) const {
		if(this->allow_execute(p)) {
			return Domain::template mutable_param_from_execute_param<target_type>(p);
		}
		return Domain::block_execute(Domain::template mutable_param_from_execute_param<target_type>(p));
	}

public:
	[[nodiscard]] query_result_t<Domain> query(const typed_query_param_t<Domain, target_type>& p = {}) const noexcept {
		return behavior::query<Domain>(child, p);
	}
};

export
template <typename Domain, typename Child>
struct tree_direct {
	using target_type = node_trait<Child>::target_type;
	static_assert(!std::is_void_v<target_type>, "tree_direct must have a clear target type.");

	ADAPTED_NO_UNIQUE_ADDRESS Child child;

	[[nodiscard]] tree_direct() = default;

	template <typename ChildArg>
		requires not_single_self_arg<tree_direct, ChildArg>
	[[nodiscard]] explicit(false) tree_direct(ChildArg&& value)
		: child(std::forward<ChildArg>(value)) {
	}

	void record(recorder_t<Domain>& ctx) const {
		if(!behavior::present<Domain>(child)) {
			return;
		}
		ctx.push_call_noop(*this, [](const tree_direct& self, const mutable_param_t<Domain>& p,
		                             const immutable_args_t<Domain>& args) static {
			behavior::execute<Domain>(self.child, Domain::template make_execute_param<target_type>(p, args));
		});
	}

	FORCE_INLINE void execute(const typed_execute_param_t<Domain, target_type>& p) const {
		if(!behavior::present<Domain>(child)) {
			return;
		}
		behavior::execute<Domain>(child, p);
	}

	[[nodiscard]] query_result_t<Domain> query(const typed_query_param_t<Domain, target_type>& p = {}) const noexcept {
		return behavior::query<Domain>(child, p);
	}
};

}
