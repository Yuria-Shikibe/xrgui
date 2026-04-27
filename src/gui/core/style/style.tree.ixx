module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.style.tree;

import std;
import align;
import mo_yanxi.function_manipulate;

export import mo_yanxi.gui.style.interface;

namespace mo_yanxi::gui::style{
template <typename T>
struct typed_draw_param{
	using target_type = T;
	draw_call_param param;

	[[nodiscard]] const T* current_subject() const noexcept{
		return static_cast<const T*>(param.current_subject);
	}

	[[nodiscard]] bool has_subject() const noexcept{
		return current_subject() != nullptr;
	}

	[[nodiscard]] explicit constexpr operator bool() const noexcept{
		return static_cast<bool>(param);
	}
};

export
struct style_tree_metrics_inherited_state{
	// Reserved for future style inheritance-dependent metric solving.
};

export
struct style_tree_metrics_composed_state{
	// Reserved for future style composition-dependent metric solving.
};

export
struct style_tree_metrics_query_param{
	const void* current_subject{};
	style_tree_metrics_inherited_state inherited{};
	style_tree_metrics_composed_state composed{};

	[[nodiscard]] explicit constexpr operator bool() const noexcept{
		return current_subject != nullptr;
	}

	[[nodiscard]] constexpr style_tree_metrics_query_param with_subject(const void* subject) const noexcept{
		auto rst = *this;
		rst.current_subject = subject;
		return rst;
	}
};

export
template <typename T>
struct typed_style_tree_metrics_query_param{
	using target_type = T;
	style_tree_metrics_query_param param;

	[[nodiscard]] const T* current_subject() const noexcept{
		return static_cast<const T*>(param.current_subject);
	}

	[[nodiscard]] bool has_subject() const noexcept{
		return current_subject() != nullptr;
	}

	[[nodiscard]] explicit constexpr operator bool() const noexcept{
		return static_cast<bool>(param);
	}
};

export
struct style_tree_metrics{
	align::spacing inset{};

	[[nodiscard]] constexpr bool empty() const noexcept{
		return inset.left == 0.f && inset.top == 0.f && inset.right == 0.f && inset.bottom == 0.f;
	}

	constexpr void merge_from(const style_tree_metrics& rhs) noexcept{
		inset.left = (std::max)(inset.left, rhs.inset.left);
		inset.top = (std::max)(inset.top, rhs.inset.top);
		inset.right = (std::max)(inset.right, rhs.inset.right);
		inset.bottom = (std::max)(inset.bottom, rhs.inset.bottom);
	}

	[[nodiscard]] friend constexpr style_tree_metrics operator|(style_tree_metrics lhs,
	                                                            const style_tree_metrics& rhs) noexcept{
		lhs.merge_from(rhs);
		return lhs;
	}
};

[[nodiscard]] constexpr draw_call_param mask_out_draw(const draw_call_param& p) noexcept{
	auto blocked = p;
	blocked.current_subject = nullptr;
	return blocked;
}

template <typename Predicate>
inline constexpr bool is_null_pred_v = std::is_null_pointer_v<std::remove_cvref_t<Predicate>>;

template <typename Fn, typename... Args>
concept redundantly_bool_invocable = requires(Fn&& fn, Args&&... args){
	{ mo_yanxi::invoke_redundantly(std::forward<Fn>(fn), std::forward<Args>(args)...) } -> std::convertible_to<bool>;
};

struct style_inplace_vtable{
	using func_signature = void*(void* val, void* t) noexcept;
	using metrics_signature = style_tree_metrics(*)(const void* val, const style_tree_metrics_query_param& p) noexcept;

private:
	func_signature* func_ptr_{};
	metrics_signature metrics_func_ptr_{};

public:
	[[nodiscard]] void* clone_with_ownership(const void* val) const{
		void* rst = func_ptr_(const_cast<void*>(val), nullptr);
		if(!rst){
			throw std::runtime_error{"failed to clone style"};
		}
		return rst;
	}

	void record(const void* val, draw_recorder& ctx) const{
		if(!func_ptr_(const_cast<void*>(val), &ctx)){
			throw std::runtime_error{"failed to record style"};
		}
	}

	void destroy(void* val) const noexcept{
		func_ptr_(nullptr, val);
	}

	[[nodiscard]] style_tree_metrics query_metrics(const void* val,
	                                               const style_tree_metrics_query_param& p) const noexcept{
		if(metrics_func_ptr_ == nullptr){
			return {};
		}
		return metrics_func_ptr_(val, p);
	}

	template <typename EntryType, std::derived_from<EntryType> Ty, std::invocable<const Ty&, draw_recorder&> RecordFn>
		requires (std::is_final_v<Ty> || std::has_virtual_destructor_v<Ty>)
	constexpr static style_inplace_vtable create(RecordFn fn) noexcept{
		static constexpr auto cast_to_ty = [](void* t) static noexcept -> Ty*{
			return static_cast<Ty*>(static_cast<EntryType*>(t));
		};
		style_inplace_vtable rst;
		rst.func_ptr_ = +[](void* val, void* t) static noexcept -> void*{
			if(val == nullptr){
				// 销毁分支：val为空，t 指代 create 时指定的类型 Ty
				auto* self = cast_to_ty(t);
				delete self;
				return self;
			}

			if(t == nullptr){
				// 拷贝分支：t为空，val 指代 create 时指定的类型 Ty
				return new(std::nothrow) Ty(std::as_const(*cast_to_ty(val)));
			}

			// 记录分支：此时 t 是 record context，val 是类型 Ty
			assert(val != nullptr);
			assert(t != nullptr);
			auto& ctx = *static_cast<draw_recorder*>(t);
			auto& ty = std::as_const(*cast_to_ty(val));
			try{
				std::invoke(RecordFn{}, ty, ctx);
				return val;
			} catch(...){
				return nullptr;
			}
		};
		rst.metrics_func_ptr_ = +[](const void* val,
		                            const style_tree_metrics_query_param& p) static noexcept -> style_tree_metrics{
			assert(val != nullptr);
			auto& ty = std::as_const(*cast_to_ty(const_cast<void*>(val)));
			if constexpr(requires(const Ty& value, const style_tree_metrics_query_param& q){
				{ value.query_metrics(q) } -> std::convertible_to<style_tree_metrics>;
			}){
				return ty.query_metrics(p);
			} else{
				return {};
			}
		};
		return rst;
	}
};

export
struct style_tree_node_deleter;

export
struct style_tree_refable_node_base : referenced_object_persistable{
	friend style_tree_node_deleter;

protected:
	style_inplace_vtable vtable;

public:
	[[nodiscard]] explicit style_tree_refable_node_base(const style_inplace_vtable& vtable)
		: vtable(vtable){
	}

	[[nodiscard]] style_tree_refable_node_base(const style_inplace_vtable& vtable,
	                                           const tags::persistent_tag_t& persistent_tag) :
		referenced_object_persistable(persistent_tag),
		vtable(vtable){
	}

	void record_draw(draw_recorder& ctx) const{
		vtable.record(this, ctx);
	}

	[[nodiscard]] style_tree_metrics query_metrics(const style_tree_metrics_query_param& p = {}) const noexcept{
		return vtable.query_metrics(this, p);
	}
};

export
struct style_tree_node_deleter{
	static void operator()(const style_tree_refable_node_base* node) noexcept{
		//TODO this is ugly
		node->vtable.destroy(const_cast<style_tree_refable_node_base*>(node));
	}
};

export
using style_tree_type_erased_ptr = referenced_ptr<style_tree_refable_node_base, style_tree_node_deleter>;


export
template <typename T>
concept style_tree_bool_testable = requires(const T& value){
	{ static_cast<bool>(value) } -> std::convertible_to<bool>;
};

export
template <typename T>
concept style_tree_dereferenceable = requires(const T& value){
	*value;
};

export
template <typename T>
concept style_tree_member_recordable = requires(const T& value, draw_recorder& ctx){
	value.record(ctx);
};

export
template <typename T>
concept style_tree_member_record_drawable = requires(const T& value, draw_recorder& ctx){
	value.record_draw(ctx);
};

export
template <typename T>
concept style_tree_member_metrics_queryable = requires(const T& value, const style_tree_metrics_query_param& p){
	{ value.query_metrics(p) } -> std::convertible_to<style_tree_metrics>;
};

export
template <typename T>
concept style_tree_draw_dispatchable = style_tree_member_recordable<T>
	|| style_tree_member_record_drawable<T>
	|| requires(const T& value, draw_recorder& r){ value->record(r); }
	|| requires(const T& value, draw_recorder& r){ value->record_draw(r); }
	|| style_tree_dereferenceable<T>;

export
template <typename T>
concept style_tree_metrics_dispatchable = style_tree_member_metrics_queryable<T>
	|| requires(const T& value, const style_tree_metrics_query_param& p){ value->query_metrics(p); }
	|| style_tree_dereferenceable<T>;

inline namespace cpo{
struct present_t{
	template <typename T>
	[[nodiscard]] FORCE_INLINE constexpr bool operator()(const T& value) const noexcept{
		if constexpr(std::is_null_pointer_v<std::remove_cvref_t<T>>){
			return false;
		} else if constexpr(style_tree_bool_testable<T>){
			return static_cast<bool>(value);
		} else{
			return true;
		}
	}
};
}

export constexpr inline cpo::present_t present;

inline namespace cpo{
struct query_metrics_t{
	template <typename T>
	[[nodiscard]] FORCE_INLINE style_tree_metrics operator()(const T& node,
	                                                         const style_tree_metrics_query_param& p = {}) const
		noexcept{
		if(!style::present(node)){
			return {};
		}

		if constexpr(style_tree_member_metrics_queryable<T>){
			return node.query_metrics(p);
		} else if constexpr(requires(const T& value, const style_tree_metrics_query_param& q){
			value->query_metrics(q);
		}){
			return node->query_metrics(p);
		} else if constexpr(style_tree_dereferenceable<T>){
			return (*this)(*node, p);
		} else{
			return {};
		}
	}
};

struct draw_record_t{
	template <typename T>
	FORCE_INLINE void operator()(const T& node, draw_recorder& ctx) const{
		if(!style::present(node)){
			return;
		}

		if constexpr(style_tree_member_recordable<T>){
			node.record(ctx);
		} else if constexpr(style_tree_member_record_drawable<T>){
			node.record_draw(ctx);
		} else if constexpr(requires(const T& value, draw_recorder& r){ value->record(r); }){
			node->record(ctx);
		} else if constexpr(requires(const T& value, draw_recorder& r){ value->record_draw(r); }){
			node->record_draw(ctx);
		} else if constexpr(style_tree_dereferenceable<T>){
			(*this)(*node, ctx);
		} else{
			static_assert(style_tree_member_recordable<T> || style_tree_member_record_drawable<T>
			              || requires(const T& value, draw_recorder& r){ value->record(r); }
			              || requires(const T& value, draw_recorder& r){ value->record_draw(r); }
			              || style_tree_dereferenceable<T>,
			              "Style tree node must be recordable directly, via record_draw, or via dereference");
		}
	}
};
}

export constexpr inline query_metrics_t query_metrics;
export constexpr inline draw_record_t draw_record;

inline namespace cpo{
struct inset_t{
	template <typename T>
	[[nodiscard]] FORCE_INLINE align::spacing operator()(const T& node,
	                                                     const style_tree_metrics_query_param& p = {}) const noexcept{
		return style::query_metrics(node, p).inset;
	}
};
}

export constexpr inline inset_t inset;

export
template <typename T>
concept style_tree_recordable = style_tree_draw_dispatchable<T>;

export
template <typename T>
concept style_tree_metrics_queryable = style_tree_metrics_dispatchable<T>;

#pragma region NodePtrImpl

export
template <typename T>
struct target_known_node_ptr{
	using target_type = T;
	style_tree_type_erased_ptr ptr;

	void record(draw_recorder& ctx) const{
		style::draw_record(ptr, ctx);
	}
};

export
template <typename Derived>
struct style_tree_node : protected style_tree_refable_node_base{
	constexpr static style_inplace_vtable make_vtable() noexcept{
		return style_inplace_vtable::create<style_tree_refable_node_base, Derived>(draw_record);
	}

	[[nodiscard]] style_tree_node()
		: style_tree_refable_node_base(make_vtable()){
	}

	[[nodiscard]] explicit style_tree_node(const tags::persistent_tag_t& persistent_tag)
		: style_tree_refable_node_base(make_vtable(), persistent_tag){
	}

	[[nodiscard]] style_tree_type_erased_ptr make_shared_ptr() noexcept{
		return style_tree_type_erased_ptr{static_cast<style_tree_refable_node_base&>(*this)};
	}

	[[nodiscard]] std::unique_ptr<Derived> clone() const{
		return std::unique_ptr{static_cast<Derived*>(vtable.clone_with_ownership(this))};
	}
};

export
template <typename Comp>
struct bound_tree_node : style_tree_node<bound_tree_node<Comp>>{
	Comp comp;

	[[nodiscard]] bound_tree_node() = default;

	template <typename T>
	[[nodiscard]] explicit(false) bound_tree_node(T&& comp)
		: comp(std::forward<T>(comp)){
	}

	void record(draw_recorder& ctx) const{
		style::draw_record(comp, ctx);
	}

	[[nodiscard]] style_tree_metrics query_metrics(const style_tree_metrics_query_param& p = {}) const noexcept{
		return style::query_metrics(comp, p);
	}
};

template <typename T>
bound_tree_node(T&&) -> bound_tree_node<std::decay_t<T>>;

#pragma endregion

template <typename Node>
inline constexpr bool is_prefer_directly_draw_unwrap_v = false;

export
template <typename T, typename... Ts>
struct node_trait{
	using target_type = void;
};

template <typename T, typename... Ts>
	requires requires{
		typename T::target_type;
	}
struct node_trait<T, Ts...>{
	using target_type = typename T::target_type;
};

template <>
struct node_trait<style_tree_type_erased_ptr>{
	using target_type = void;
};

export
template <std::ranges::forward_range Container>
struct tree_fork{
	ADAPTED_NO_UNIQUE_ADDRESS Container children;
	using target_type = node_trait<std::remove_cvref_t<std::ranges::range_reference_t<Container>>>::target_type;
	static_assert(!std::is_void_v<target_type>, "tree_fork must has a clear target type");

	[[nodiscard]] tree_fork() = default;

	template <typename Rng>
	[[nodiscard]] explicit(false) tree_fork(Rng&& children)
		: children(std::forward<Rng>(children)){
	}

	void record(draw_recorder& ctx) const{
		for(const auto& child : children){
			style::draw_record(child, ctx);
		}
	}
};

export
template <std::ranges::forward_range Container>
struct tree_metrics_fork{
	ADAPTED_NO_UNIQUE_ADDRESS Container children;
	using target_type = typename node_trait<std::remove_cvref_t<std::ranges::range_reference_t<Container>>>::target_type;
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

	template <typename ChildArg>
	[[nodiscard]] explicit(false) tree_scope(ChildArg&& child)
		: child(std::forward<ChildArg>(child)){
	}

	template <typename EnterArg, typename ChildArg>
	[[nodiscard]] explicit(false) tree_scope(EnterArg&& on_enter, ChildArg&& child)
		: on_enter(std::forward<EnterArg>(on_enter)),
		  child(std::forward<ChildArg>(child)){
	}

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
			self.draw(p);
		});
	}

	void draw(const draw_call_param& p) const{
		if(!p.current_subject){
			return;
		}

		std::invoke(draw_fn, typed_draw_param<target_type>(p));
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

template <typename DrawFn, typename T>
inline constexpr bool is_prefer_directly_draw_unwrap_v<tree_leaf<DrawFn, T>> = true;

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

		if(allow_record()){
			style::draw_record(child, ctx);
		}
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

		if constexpr(is_prefer_directly_draw_unwrap_v<std::remove_cvref_t<Child>>){
			ctx.push_call_noop(*this, [](const tree_router_dynamic& self, const draw_call_param& p) static{
				if(!self.allow_draw(p)){
					return;
				}

				//TODO make draw cpo?
				self.child.draw(p);
			});
		} else{
			ctx.push_call_enter(*this, [](const tree_router_dynamic& self,
			                              const draw_call_param& p) static -> draw_call_param{
				return self.route_param(p);
			});

			style::draw_record(child, ctx);
			ctx.push_call_leave();
		}
	}

private:
	[[nodiscard]] bool allow_draw(const draw_call_param& p) const{
		if(!p.current_subject){
			return false;
		}

		const typed_draw_param<target_type> typed{p};
		if constexpr(style::is_null_pred_v<Predicate>){
			return true;
		} else{
			static_assert(
				redundantly_bool_invocable<const Predicate&, const tree_router_dynamic&, const Child&, const
				                           typed_draw_param<target_type>&>,
				"Dynamic router predicate must be nullptr or redundantly invocable by router/child/draw param context");
			return static_cast<bool>(mo_yanxi::invoke_redundantly(predicate, *this, child, typed));
		}
	}

	[[nodiscard]] draw_call_param route_param(const draw_call_param& p) const{
		if(!p.current_subject){
			return p;
		}

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

template <typename Child>
tree_scope(Child&&) -> tree_scope<std::decay_t<Child>, std::nullptr_t, std::nullptr_t>;

template <typename OnEnter, typename Child>
tree_scope(OnEnter&&, Child&&) -> tree_scope<std::decay_t<Child>, std::decay_t<OnEnter>, std::nullptr_t>;

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
#pragma endregion
}
