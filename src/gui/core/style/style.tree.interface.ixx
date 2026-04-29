module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.style.tree:interface;

import std;
import align;
import mo_yanxi.function_manipulate;
export import mo_yanxi.gui.style.interface;
export import mo_yanxi.gui.renderer.frontend;

namespace mo_yanxi::gui::style{
export
template <typename T>
struct typed_draw_param{
	using target_type = T;
	draw_call_param param;

	[[nodiscard]] const T* current_subject() const noexcept{
		return static_cast<const T*>(param.current_subject);
	}

	[[nodiscard]] const T& subject() const noexcept{
		assert(param.current_subject != nullptr);
		return *current_subject();
	}

	[[nodiscard]] renderer_frontend& renderer() const noexcept requires requires(const target_type& t){
		{ t.renderer() } -> std::convertible_to<renderer_frontend&>;
	}{
		assert(param.current_subject != nullptr);
		return subject().renderer();
	}

	[[nodiscard]] bool has_subject() const noexcept{
		return current_subject() != nullptr;
	}

	[[nodiscard]] explicit constexpr operator bool() const noexcept{
		return static_cast<bool>(param);
	}

	auto* operator->(this auto& self) noexcept{
		return &self.param;
	}

	explicit(false) operator draw_call_param&() noexcept{
		return param;
	}

	explicit(false) operator const draw_call_param&() const noexcept{
		return param;
	}

	template <typename B>
		requires std::derived_from<T, B>
	explicit(false) operator typed_draw_param<B>() const noexcept{
		return typed_draw_param<B>{
			draw_call_param{
				.current_subject = static_cast<B*>(current_subject()),
				.draw_bound = param.draw_bound,
				.opacity_scl = param.opacity_scl,
				.layer_param = param.layer_param
			}};
	}
};

export
template <typename B, std::derived_from<B> D>
struct typed_draw_param_adaptor{
	using target_type = B;

	typed_draw_param<D> param;

	[[nodiscard]] const target_type* current_subject() const noexcept{
		return static_cast<const target_type*>(param.current_subject());
	}

	[[nodiscard]] const target_type& subject() const noexcept{
		assert(param->current_subject != nullptr);
		return *current_subject();
	}

	template <typename T>
		requires std::derived_from<D, T>
	explicit(false) operator typed_draw_param_adaptor<T, D>() const noexcept{
		return typed_draw_param_adaptor<T, D>{param};
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
concept redundantly_bool_invocable = mo_yanxi::redundantly_invocable<Fn, Args...>
	&& requires(Fn&& fn, Args&&... args){
		{
			mo_yanxi::invoke_redundantly(std::forward<Fn>(fn), std::forward<Args>(args)...)
		} -> std::convertible_to<bool>;
	};

struct style_inplace_vtable{
	using func_signature = void*(void* val, void* t) noexcept;
	using metrics_signature = style_tree_metrics(*)(const void* val, const style_tree_metrics_query_param& p) noexcept;
	using direct_signature = void(*)(const void* val, const draw_call_param& p) noexcept;

private:
	func_signature* func_ptr_{};
	metrics_signature metrics_func_ptr_{};
	direct_signature direct_func_ptr_{};

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

	void direct_draw(const void* val, const draw_call_param& p) const{
		direct_func_ptr_(val, p);
	}

	template <typename EntryType, std::derived_from<EntryType> Ty, std::invocable<const Ty&, draw_recorder&> RecordFn>
		// requires (std::is_final_v<Ty> || std::has_virtual_destructor_v<Ty>)
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
		rst.direct_func_ptr_ = +[](const void* val,
		                           const draw_call_param& p) static noexcept -> void{
			auto& ty = std::as_const(*cast_to_ty(const_cast<void*>(val)));
			if constexpr(requires{ typename Ty::target_type; }
				&& requires(const Ty& v, const typed_draw_param<typename Ty::target_type>& dp){ v.direct(dp); }){
				draw_direct(ty, typed_draw_param<typename Ty::target_type>{p});
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

	void direct_draw(const draw_call_param& p) const{
		vtable.direct_draw(this, p);
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
template <typename T, typename Target>
concept style_tree_direct_drawable_typed = requires(const T& value, const typed_draw_param<Target>& p){
	value.direct(p);
};

export
template <typename T>
concept style_tree_direct_drawable =
	requires{ typename node_trait<T>::target_type; }
	&& !std::is_void_v<typename node_trait<T>::target_type>
	&& style_tree_direct_drawable_typed<T, typename node_trait<T>::target_type>;

inline namespace cpo{
struct draw_direct_t{
	template <typename Target, typename T>
		requires style_tree_direct_drawable_typed<T, Target>
	void operator()(const T& node, const typed_draw_param<Target>& p) const{
		if(!style::present(node)) return;
		node.direct(p);
	}
};
}

export constexpr inline draw_direct_t draw_direct;

#pragma region NodePtrImpl

export
template <typename T>
struct target_known_node_ptr{
	using target_type = T;
	style_tree_type_erased_ptr ptr;

	void record(draw_recorder& ctx) const{
		if(ptr)ptr->record_draw(ctx);
	}

	void direct(const typed_draw_param<T>& p) const{
		if(ptr)ptr->direct_draw(p.param);
	}


	[[nodiscard]] style_tree_metrics query_metrics(const style_tree_metrics_query_param& p = {}) const noexcept{
		if(ptr){
			return ptr->query_metrics(p);
		}
		return {};
	}
};

export
template <typename Derived>
struct style_tree_node : style_tree_refable_node_base{
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
	using target_type = node_trait<Comp>::target_type;

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

export
template <typename T>
[[nodiscard]] auto make_tree_node_ptr(T&& v){
	auto ptr = std::make_unique<bound_tree_node<std::decay_t<T>>>(std::forward<T>(v));
	auto rst = target_known_node_ptr<typename node_trait<std::remove_cvref_t<T>>::target_type>{/*ptr->make_shared_ptr()*/};
	// ptr.release();
	return rst;
}

#pragma endregion;
}
