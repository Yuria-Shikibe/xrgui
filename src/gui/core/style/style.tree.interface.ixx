module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.style.tree:interface;

import std;
import align;
import mo_yanxi.behavior.tree;
export import mo_yanxi.gui.style.interface;
export import mo_yanxi.gui.renderer.frontend;

namespace mo_yanxi::gui::style {

export
template <typename T>
struct typed_draw_param {
	using target_type = T;

	draw_call_param param;
	draw_immut_args immut_args;

	[[nodiscard]] const T* current_subject() const noexcept {
		return static_cast<const T*>(param.current_subject);
	}

	[[nodiscard]] const T& subject() const noexcept {
		assert(param.current_subject != nullptr);
		return *this->current_subject();
	}

	[[nodiscard]] renderer_frontend& renderer() const noexcept requires requires(const target_type& t) {
		{ t.renderer() } -> std::convertible_to<renderer_frontend&>;
	} {
		assert(param.current_subject != nullptr);
		return this->subject().renderer();
	}

	[[nodiscard]] bool has_subject() const noexcept {
		return this->current_subject() != nullptr;
	}

	[[nodiscard]] explicit constexpr operator bool() const noexcept {
		return static_cast<bool>(param);
	}

	auto* operator->(this auto& self) noexcept {
		return &self.param;
	}

	explicit(false) operator draw_call_param&() noexcept {
		return param;
	}

	explicit(false) operator const draw_call_param&() const noexcept {
		return param;
	}

	template <typename B>
		requires std::derived_from<T, B>
	explicit(false) operator typed_draw_param<B>() const noexcept {
		return typed_draw_param<B>{
			.param = draw_call_param{
				.current_subject = static_cast<B*>(this->current_subject()),
				.draw_bound = param.draw_bound,
				.opacity_scl = param.opacity_scl
			},
			.immut_args = immut_args
		};
	}
};

export
struct style_tree_metrics_query_param {
	const void* current_subject{};

	[[nodiscard]] explicit constexpr operator bool() const noexcept {
		return current_subject != nullptr;
	}

	[[nodiscard]] constexpr style_tree_metrics_query_param with_subject(const void* subject) const noexcept {
		auto rst = *this;
		rst.current_subject = subject;
		return rst;
	}
};

export
template <typename T>
struct typed_style_tree_metrics_query_param {
	using target_type = T;

	style_tree_metrics_query_param param;

	[[nodiscard]] const T* current_subject() const noexcept {
		return static_cast<const T*>(param.current_subject);
	}

	[[nodiscard]] bool has_subject() const noexcept {
		return this->current_subject() != nullptr;
	}

	[[nodiscard]] explicit constexpr operator bool() const noexcept {
		return static_cast<bool>(param);
	}
};

export
struct style_tree_metrics {
	align::spacing inset{};
	align::spacing inherited{};

	[[nodiscard]] constexpr bool empty() const noexcept {
		return inset.left == 0.f && inset.top == 0.f && inset.right == 0.f && inset.bottom == 0.f
			&& inherited.left == 0.f && inherited.top == 0.f && inherited.right == 0.f && inherited.bottom == 0.f;
	}

	constexpr void merge_from(const style_tree_metrics& rhs) noexcept {
		inset.left = (std::max)(inset.left, rhs.inset.left);
		inset.top = (std::max)(inset.top, rhs.inset.top);
		inset.right = (std::max)(inset.right, rhs.inset.right);
		inset.bottom = (std::max)(inset.bottom, rhs.inset.bottom);

		inherited.left += rhs.inherited.left;
		inherited.top += rhs.inherited.top;
		inherited.right += rhs.inherited.right;
		inherited.bottom += rhs.inherited.bottom;
	}

	[[nodiscard]] constexpr align::spacing total_inset() const noexcept {
		return {
			inset.left + inherited.left,
			inset.right + inherited.right,
			inset.bottom + inherited.bottom,
			inset.top + inherited.top
		};
	}

	[[nodiscard]] friend constexpr style_tree_metrics operator|(style_tree_metrics lhs,
	                                                            const style_tree_metrics& rhs) noexcept {
		lhs.merge_from(rhs);
		return lhs;
	}
};

export
[[nodiscard]] constexpr draw_call_param mask_out_draw(const draw_call_param& p) noexcept {
	auto blocked = p;
	blocked.current_subject = nullptr;
	return blocked;
}

export
struct gui_style_domain {
	using mutable_param = draw_call_param;
	using immutable_args = draw_immut_args;
	using recorder = draw_recorder;
	using query_param = style_tree_metrics_query_param;
	using query_result = style_tree_metrics;

	template <typename T>
	using typed_execute_param = typed_draw_param<T>;

	template <typename T>
	using typed_query_param = typed_style_tree_metrics_query_param<T>;

	template <typename Target>
	[[nodiscard]] static typed_execute_param<Target> make_execute_param(
		const mutable_param& param,
		const immutable_args& immut_args) noexcept {
		return {
			.param = param,
			.immut_args = immut_args
		};
	}

	template <typename Target>
	[[nodiscard]] static typed_query_param<Target> make_query_param(const query_param& param) noexcept {
		return {
			.param = param
		};
	}

	template <typename Target>
	[[nodiscard]] static mutable_param mutable_param_from_execute_param(
		const typed_execute_param<Target>& param) noexcept {
		return param.param;
	}

	template <typename Target>
	[[nodiscard]] static immutable_args immutable_args_from_execute_param(
		const typed_execute_param<Target>& param) noexcept {
		return param.immut_args;
	}

	template <typename Target>
	[[nodiscard]] static query_param query_param_from_typed_query_param(
		const typed_query_param<Target>& param) noexcept {
		return param.param;
	}

	[[nodiscard]] static constexpr query_result empty_query_result() noexcept {
		return {};
	}

	static constexpr void merge_query_result(query_result& lhs, const query_result& rhs) noexcept {
		lhs.merge_from(rhs);
	}

	template <typename EnterFn>
	[[nodiscard]] static query_result scope_query_result(const EnterFn& enter_fn) noexcept {
		if constexpr(requires { { enter_fn.scope_inset() } -> std::convertible_to<query_result>; }) {
			return enter_fn.scope_inset();
		} else {
			return {};
		}
	}

	[[nodiscard]] static constexpr mutable_param block_execute(const mutable_param& param) noexcept {
		return style::mask_out_draw(param);
	}
};

export
template <typename T, typename... Ts>
struct node_trait : behavior::node_trait<T, Ts...> {
};

export
template <typename T>
using node_target_t = behavior::node_target_t<T>;

export
template <typename T>
concept style_tree_bool_testable = behavior::tree_bool_testable<T>;

export
template <typename T>
concept style_tree_dereferenceable = behavior::tree_dereferenceable<T>;

export
template <typename T>
concept style_tree_typed_target = behavior::tree_typed_target<T>;

export
template <typename T>
concept style_tree_member_recordable = behavior::tree_member_recordable<gui_style_domain, T>;

export
template <typename T>
concept style_tree_member_record_drawable = requires(const T& value, draw_recorder& ctx) {
	value.record_draw(ctx);
};

export
template <typename T>
concept style_tree_member_metrics_queryable = behavior::tree_member_queryable<gui_style_domain, T>
	|| (style_tree_typed_target<T>
		&& requires(const T& value, const typed_style_tree_metrics_query_param<node_target_t<T>>& p) {
			{ value.query_metrics(p) } -> std::convertible_to<style_tree_metrics>;
		});

export
template <typename T>
concept style_tree_arrow_metrics_queryable = behavior::tree_arrow_queryable<gui_style_domain, T>
	|| (style_tree_typed_target<T>
		&& requires(const T& value, const typed_style_tree_metrics_query_param<node_target_t<T>>& p) {
			{ value->query_metrics(p) } -> std::convertible_to<style_tree_metrics>;
		});

export
template <typename T, typename Target>
concept style_tree_direct_drawable_typed = behavior::tree_executable_typed<gui_style_domain, T, Target>
	|| requires(const T& value, const typed_draw_param<Target>& p) {
		value.direct(p);
	};

export
template <typename T>
concept style_tree_direct_drawable = style_tree_typed_target<T>
	&& style_tree_direct_drawable_typed<T, node_target_t<T>>;

export
template <typename T>
concept style_tree_recordable = behavior::tree_recordable<gui_style_domain, T>
	|| style_tree_member_record_drawable<T>
	|| requires(const T& value, draw_recorder& r) { value->record_draw(r); };

export
template <typename T>
concept style_tree_metrics_queryable = style_tree_member_metrics_queryable<T>
	|| style_tree_arrow_metrics_queryable<T>
	|| style_tree_dereferenceable<T>;

inline namespace cpo {
struct present_t {
	template <typename T>
	[[nodiscard]] FORCE_INLINE constexpr bool operator()(const T& value) const noexcept {
		return behavior::present<gui_style_domain>(value);
	}
};

struct get_scope_inset_t {
	template <typename T>
	[[nodiscard]] FORCE_INLINE style_tree_metrics operator()(const T& enter_fn) const noexcept {
		return gui_style_domain::scope_query_result(enter_fn);
	}
};

struct query_metrics_t {
	template <typename T>
	[[nodiscard]] FORCE_INLINE style_tree_metrics operator()(
		const T& node,
		const typed_style_tree_metrics_query_param<node_target_t<T>>& p = {}) const noexcept {
		if(!behavior::present<gui_style_domain>(node)) {
			return {};
		}

		if constexpr(behavior::tree_member_queryable<gui_style_domain, T>) {
			return node.query(p);
		} else if constexpr(requires(const T& value) { value.query_metrics(p); }) {
			return node.query_metrics(p);
		} else if constexpr(behavior::tree_arrow_queryable<gui_style_domain, T>) {
			return node->query(p);
		} else if constexpr(requires(const T& value) { value->query_metrics(p); }) {
			return node->query_metrics(p);
		} else if constexpr(style_tree_dereferenceable<T>) {
			return this->operator()(*node, p);
		} else {
			return {};
		}
	}
};

struct draw_record_t {
	template <typename T>
	FORCE_INLINE void operator()(const T& node, draw_recorder& ctx) const {
		if(!behavior::present<gui_style_domain>(node)) {
			return;
		}

		if constexpr(behavior::tree_member_recordable<gui_style_domain, T>) {
			node.record(ctx);
		} else if constexpr(style_tree_member_record_drawable<T>) {
			node.record_draw(ctx);
		} else if constexpr(behavior::tree_arrow_recordable<gui_style_domain, T>) {
			node->record(ctx);
		} else if constexpr(requires(const T& value, draw_recorder& r) { value->record_draw(r); }) {
			node->record_draw(ctx);
		} else if constexpr(style_tree_dereferenceable<T>) {
			this->operator()(*node, ctx);
		} else {
			static_assert(style_tree_recordable<T>, "Style tree node must be recordable.");
		}
	}
};

struct draw_direct_t {
	template <typename Target, typename T>
		requires style_tree_direct_drawable_typed<T, Target> && std::derived_from<Target, node_target_t<T>>
	FORCE_INLINE void operator()(const T& node, const typed_draw_param<Target>& p) const {
		if(!behavior::present<gui_style_domain>(node)) {
			return;
		}

		if constexpr(behavior::tree_executable_typed<gui_style_domain, T, Target>) {
			node.execute(p);
		} else {
			node.direct(p);
		}
	}
};
}

export constexpr inline cpo::present_t present{};
export constexpr inline cpo::get_scope_inset_t get_scope_inset{};
export constexpr inline cpo::query_metrics_t query_metrics{};
export constexpr inline cpo::draw_record_t draw_record{};
export constexpr inline cpo::draw_direct_t draw_direct{};

export
using style_inplace_vtable = behavior::tree_inplace_vtable<gui_style_domain>;

export
using style_tree_node_deleter = behavior::tree_node_deleter<gui_style_domain>;

export
using style_tree_refable_node_base = behavior::tree_refable_node_base<gui_style_domain>;

export
using style_tree_type_erased_ptr = behavior::tree_type_erased_ptr<gui_style_domain>;

export
template <typename T>
struct target_known_node_ptr : behavior::typed_node_ptr<gui_style_domain, T> {
	using base_type = behavior::typed_node_ptr<gui_style_domain, T>;
	using target_type = T;

	[[nodiscard]] target_known_node_ptr() = default;

	[[nodiscard]] explicit(false) target_known_node_ptr(style_tree_type_erased_ptr value) noexcept
		: base_type(std::move(value)) {
	}

	void direct(const typed_draw_param<T>& p) const {
		this->execute(p);
	}

	[[nodiscard]] style_tree_metrics query_metrics(
		const typed_style_tree_metrics_query_param<target_type>& p = {}) const noexcept {
		return this->query(p);
	}

	[[nodiscard]] target_known_node_ptr copy() const {
		return target_known_node_ptr{{*this->ptr->clone_with_ownership()}};
	}
};

export
template <typename Derived>
struct style_tree_node : behavior::tree_node<gui_style_domain, Derived> {
	using base_type = behavior::tree_node<gui_style_domain, Derived>;
	using base_type::base_type;
};

export
template <typename Comp>
struct bound_tree_node : style_tree_node<bound_tree_node<Comp>> {
	Comp comp;
	using target_type = node_trait<Comp>::target_type;

	[[nodiscard]] bound_tree_node() = default;

	template <typename T>
		requires (!std::same_as<bound_tree_node<Comp>, std::remove_cvref_t<T>>)
			&& std::constructible_from<Comp, T&&>
	[[nodiscard]] explicit(false) bound_tree_node(T&& value)
		: comp(std::forward<T>(value)) {
	}

	void record(draw_recorder& ctx) const {
		style::draw_record(comp, ctx);
	}

	void execute(const typed_draw_param<target_type>& p) const {
		style::draw_direct(comp, p);
	}

	void direct(const typed_draw_param<target_type>& p) const {
		this->execute(p);
	}

	[[nodiscard]] style_tree_metrics query(
		const typed_style_tree_metrics_query_param<target_type>& p = {}) const noexcept {
		return style::query_metrics(comp, p);
	}

	[[nodiscard]] style_tree_metrics query_metrics(
		const typed_style_tree_metrics_query_param<target_type>& p = {}) const noexcept {
		return this->query(p);
	}
};

template <typename T>
bound_tree_node(T&&) -> bound_tree_node<std::decay_t<T>>;

export
template <typename T>
[[nodiscard]] auto make_tree_node_ptr(T&& value) {
	auto ptr = std::make_unique<bound_tree_node<std::decay_t<T>>>(std::forward<T>(value));
	auto rst = target_known_node_ptr<typename node_trait<std::remove_cvref_t<T>>::target_type>{ptr->make_shared_ptr()};
	ptr.release();
	return rst;
}

}
