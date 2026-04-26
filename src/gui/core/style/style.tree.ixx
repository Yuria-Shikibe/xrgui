module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.style.tree;

import std;
import mo_yanxi.function_manipulate;

export import mo_yanxi.gui.style.interface;

namespace mo_yanxi::gui::style{

template <typename T>
struct typed_draw_param{
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

[[nodiscard]] constexpr draw_call_param mask_out_draw(const draw_call_param& p) noexcept{
	auto blocked = p;
	blocked.current_subject = nullptr;
	return blocked;
}

template <typename Predicate>
inline constexpr bool is_null_pred_v = std::is_null_pointer_v<std::remove_cvref_t<Predicate>>;

struct style_inplace_vtable{
	using func_signature = void*(void* val, void* t) noexcept;

private:
	func_signature* func_ptr_{};

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

	template <typename EntryType, std::derived_from<EntryType> Ty, std::invocable<const Ty&, draw_recorder&> RecordFn>
		requires (std::is_final_v<Ty> || std::has_virtual_destructor_v<Ty>)
	constexpr static style_inplace_vtable create(RecordFn fn) noexcept {
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

	[[nodiscard]] style_tree_refable_node_base(const style_inplace_vtable& vtable, const tags::persistent_tag_t& persistent_tag) : referenced_object_persistable(persistent_tag),
		  vtable(vtable){
	}

	void record_draw(draw_recorder& ctx) const{
		vtable.record(this, ctx);
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

inline namespace cpo{

struct present_t{
	template <typename T>
	[[nodiscard]] FORCE_INLINE constexpr bool operator()(const T& value) const noexcept{
		if constexpr (std::is_null_pointer_v<std::remove_cvref_t<T>>){
			return false;
		} else if constexpr (style_tree_bool_testable<T>){
			return static_cast<bool>(value);
		} else{
			return true;
		}
	}
};

}

export constexpr inline cpo::present_t present;

inline namespace cpo{

struct draw_record_t{
	template <typename T>
	FORCE_INLINE void operator()(const T& node, draw_recorder& ctx) const{
		if(!style::present(node)){
			return;
		}

		if constexpr (style_tree_member_recordable<T>){
			node.record(ctx);
		} else if constexpr (style_tree_member_record_drawable<T>){
			node.record_draw(ctx);
		} else if constexpr (requires(const T& value, draw_recorder& r){ value->record(r); }){
			node->record(ctx);
		} else if constexpr (requires(const T& value, draw_recorder& r){ value->record_draw(r); }){
			node->record_draw(ctx);
		} else if constexpr (style_tree_dereferenceable<T>){
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

export constexpr inline draw_record_t draw_record;

export
template <typename T>
concept style_tree_recordable = requires(const T& value, draw_recorder& ctx){
	style::draw_record(value, ctx);
};

#pragma region NodePtrImpl

export
template <typename Derived>
struct style_tree_node : protected style_tree_refable_node_base{

	constexpr static style_inplace_vtable make_vtable() noexcept {
		return style_inplace_vtable::create<style_tree_refable_node_base, Derived>(draw_record);
	}

	[[nodiscard]] style_tree_node()
		: style_tree_refable_node_base(make_vtable()){
	}

	[[nodiscard]] explicit style_tree_node(const tags::persistent_tag_t& persistent_tag)
		: style_tree_refable_node_base(make_vtable(), persistent_tag){
	}

	[[nodiscard]] style_tree_type_erased_ptr make_shared_ptr() noexcept {
		return style_tree_type_erased_ptr{static_cast<style_tree_refable_node_base&>(*this)};
	}

	[[nodiscard]] std::unique_ptr<Derived> clone() const {
		return std::unique_ptr{static_cast<Derived*>(vtable.clone_with_ownership(this))};
	}
};

export
template <typename Comp>
struct bound_tree_node : style_tree_node<bound_tree_node<Comp>>{
	Comp comp;

	void record(draw_recorder& ctx) const{
		style::draw_record(comp, ctx);
	}
};

#pragma endregion

template <typename Node>
inline constexpr bool is_prefer_directly_draw_unwrap_v = false;

export
template <typename T, typename... Ts>
struct node_trait{
	using target_type = void;
};

export
template <typename T, typename... Ts>
	requires requires{
		typename T::target_type;
	}
struct node_trait<T, Ts...>{
	using target_type = typename T::target_type;
};

export
template <>
struct node_trait<style_tree_type_erased_ptr>{
	using target_type = void;
};

export
template <std::ranges::forward_range Container, typename TargetType = void>
struct tree_fork{
	ADAPTED_NO_UNIQUE_ADDRESS Container children;
	using target_type = std::conditional_t<std::is_void_v<TargetType>, node_trait<std::remove_cvref_t<std::ranges::range_reference_t<Container>>>, TargetType>;
	static_assert(!std::is_void_v<target_type>, "tree_fork must has a clear target type");

	[[nodiscard]] tree_fork() = default;

	template <typename Rng>
	[[nodiscard]] explicit(false) tree_fork(Rng&& children)
		: tree_fork(std::forward<Rng>(children)){
	}

	template <typename Rng>
	[[nodiscard]] explicit(false) tree_fork(Rng&& children, std::in_place_type_t<TargetType>)
		: tree_fork(std::forward<Rng>(children)){
	}

	void record(draw_recorder& ctx) const{
		for(const auto& child : children){
			style::draw_record(child, ctx);
		}
	}
};

template <typename Rng>
tree_fork(Rng&&) -> tree_fork<std::decay_t<Rng>>;

template <typename Rng, typename T>
tree_fork(Rng&&, std::in_place_type_t<T>) -> tree_fork<std::decay_t<Rng>, T>;

export
template <typename... Children>
struct tree_tuple_fork{
	static_assert(sizeof...(Children) > 0);
	using target_type = typename node_trait<Children...>::target_type;
	static_assert((std::is_same_v<target_type, typename node_trait<Children>::target_type> && ...));

	ADAPTED_NO_UNIQUE_ADDRESS std::tuple<Children...> children;

	template <typename ...Ts>
	explicit tree_tuple_fork(Ts&&... ts) : children(std::forward<Ts>(ts)...){

	}

	template <typename ...Ts>
	explicit tree_tuple_fork(std::tuple<Ts...>&& ts) : children(std::move<Ts>(ts)){

	}
	template <typename ...Ts>
	explicit tree_tuple_fork(const std::tuple<Ts...>& ts) : children(ts){

	}

	void record(draw_recorder& ctx) const{
		std::apply([&ctx](const auto&... child){
			(style::draw_record(child, ctx), ...);
		}, children);
	}
};

template <typename ...Ts>
tree_tuple_fork(Ts&&...) -> tree_tuple_fork<std::decay_t<Ts>...>;

template <typename ...Ts>
tree_tuple_fork(std::tuple<Ts...>&&...) -> tree_tuple_fork<std::decay_t<Ts>...>;

template <typename ...Ts>
tree_tuple_fork(const std::tuple<Ts...>&...) -> tree_tuple_fork<std::decay_t<Ts>...>;

export
template <typename Child, typename OnEnter, typename OnLeave>
struct style_tree_scope{
	using target_type = node_trait<Child>::target_type;
	using enter_fn_type = OnEnter;
	using leave_fn_type = OnLeave;

	ADAPTED_NO_UNIQUE_ADDRESS Child child{};
	ADAPTED_NO_UNIQUE_ADDRESS enter_fn_type on_enter{};
	ADAPTED_NO_UNIQUE_ADDRESS leave_fn_type on_leave{};

	void record(draw_recorder& ctx) const{
		if(!present(child))return;

		ctx.push_call_enter(*this, [] (const style_tree_scope& self, const draw_call_param& p) static -> draw_call_param {
			return self.enter(p);
		});

		style::draw_record(child, ctx);

		if constexpr (std::is_null_pointer_v<OnLeave>){
			ctx.push_call_leave();
		} else{
			ctx.push_call_leave(*this, [] (const style_tree_scope& self, const draw_call_param& p) static {
				self.leave(p);
			});
		}
	}

private:
	[[nodiscard]] draw_call_param enter(const draw_call_param& p) const{
		if constexpr (std::is_null_pointer_v<OnEnter>){
			return p;
		} else{
			return std::invoke_r<draw_call_param>(on_enter, *this, typed_draw_param<target_type>{p});
		}
	}

	void leave(const draw_call_param& p) const{
		if constexpr (!std::is_null_pointer_v<OnLeave>){
			std::invoke(on_leave, *this, typed_draw_param<target_type>{p});
		}
	}
};

export
template <typename DrawFn, typename T = void>
struct style_tree_static_leaf{
	using target_type = std::conditional_t<
		std::is_void_v<T> && unambiguous_function<DrawFn>,
		node_trait<std::remove_cvref_t<typename function_arg_at_last<DrawFn>::type>>,
		T>;
	static_assert(!std::is_void_v<target_type>, "tree_fork must has a clear target type");
	static_assert(!std::invocable<const DrawFn&, typed_draw_param<target_type>>, "tree_fork must has a clear target type");


	ADAPTED_NO_UNIQUE_ADDRESS DrawFn draw_fn;

	void record(draw_recorder& ctx) const{
		ctx.push_call_noop(*this, [] (const style_tree_static_leaf& self, const draw_call_param& p) static {
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

template <typename DrawFn, typename T>
inline constexpr bool is_prefer_directly_draw_unwrap_v<style_tree_static_leaf<DrawFn, T>> = true;

export
template <typename Child, typename Predicate>
struct static_tree_router{
	using target_type = node_trait<Child>::target_type;
	ADAPTED_NO_UNIQUE_ADDRESS Predicate predicate{};
	ADAPTED_NO_UNIQUE_ADDRESS Child child{};

	void record(draw_recorder& ctx) const{
		if(!present(child))return;

		if(allow_record()){
			style::draw_record(child, ctx);
		}
	}

private:
	[[nodiscard]] constexpr bool allow_record() const{
		if constexpr (style::is_null_pred_v<Predicate>){
			return true;
		} else if constexpr (std::predicate<Predicate, const static_tree_router&>){
			return std::invoke(predicate, *this);
		} else if constexpr (std::predicate<Predicate, const Child&>){
			return std::invoke(predicate, child);
		} else if constexpr (std::predicate<Predicate>){
			return std::invoke(predicate);
		} else{
			static_assert(std::predicate<Predicate, const static_tree_router&> || std::predicate<Predicate, const Child&> || std::predicate<Predicate>,
				"Static router predicate must be nullptr, predicate<router>, predicate<child>, or predicate<>");
			return true;
		}
	}

};

export
template <typename Child, typename Predicate>
struct dynamic_tree_router{
	using target_type = node_trait<Child>::target_type;
	ADAPTED_NO_UNIQUE_ADDRESS Predicate predicate{};
	ADAPTED_NO_UNIQUE_ADDRESS Child child{};

	void record(draw_recorder& ctx) const{
		if(!present(child))return;

		if constexpr (is_prefer_directly_draw_unwrap_v<std::remove_cvref_t<Child>>){
			ctx.push_call_noop(*this, [] (const dynamic_tree_router& self, const draw_call_param& p) static {
				if(!self.allow_draw()){
					return;
				}

					//TODO make draw cpo?
				self.child.draw(p);
			});
		} else{
			ctx.push_call_enter(*this, [] (const dynamic_tree_router& self, const draw_call_param& p) static -> draw_call_param {
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
		if constexpr (style::is_null_pred_v<Predicate>){
			return true;
		} else if constexpr (std::predicate<Predicate, const dynamic_tree_router&, const typed_draw_param<target_type>&>){
			return std::invoke(predicate, *this, typed);
		} else if constexpr (std::predicate<Predicate, const typed_draw_param<target_type>&>){
			return std::invoke(predicate, typed);
		} else if constexpr (std::predicate<Predicate>){
			return std::invoke(predicate);
		} else{
			static_assert(false,
				"Dynamic router predicate must be nullptr or a predicate over router/child/draw param[/stack]");
			return true;
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

};

}
