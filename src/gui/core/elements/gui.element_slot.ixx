module;

export module mo_yanxi.gui.elem.element_slot;

export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.gui.layout.policies;

import std;

namespace mo_yanxi::gui{

export
template <typename ElementType>
struct elem_slot_kind{
	using element_type = ElementType;

	static constexpr bool is_elem_ptr = std::is_same_v<element_type, elem_ptr>;
	static constexpr bool is_elem_value = std::derived_from<element_type, elem>;
	static constexpr bool is_elem_child = is_elem_ptr || is_elem_value;

	static_assert(is_elem_value || std::is_default_constructible_v<element_type>,
	              "if element is not a T : elem, it must be default constructible");

	template <typename Item>
	static auto& get_item(Item& item) requires(!is_elem_ptr){
		return item;
	}

	template <typename Item>
	static decltype(auto) get_elem(Item& item) noexcept requires(is_elem_child){
		if constexpr(requires(Item& e){
			{ *e } -> std::convertible_to<elem&>;
		}){
			return *item;
		} else if constexpr(is_elem_value){
			return item;
		}
	}

	template <typename Item, typename Fn>
	static void for_elem(Item& item, Fn&& fn){
		if constexpr(is_elem_child){
			std::invoke(std::forward<Fn>(fn), elem_slot_kind::get_elem(item));
		}
	}

	template <typename Item, typename FallbackFn, typename ElemFn>
	static decltype(auto) elem_or(Item& item, FallbackFn&& fallback_fn, ElemFn&& elem_fn){
		if constexpr(is_elem_child){
			return std::invoke(std::forward<ElemFn>(elem_fn), elem_slot_kind::get_elem(item));
		} else{
			return std::invoke(std::forward<FallbackFn>(fallback_fn));
		}
	}
};

export
template <typename T, typename Owner = elem>
struct elem_slot_interface_schema : elem_slot_kind<T>{
	using element_type = T;
	using owner_type = Owner;
	using slot_kind = elem_slot_kind<element_type>;

	void update(element_type&, float){
	}

	void draw_layer(
		const element_type& element,
		const owner_type& owner,
		rect clipSpace,
		float opacityScl,
		fx::layer_param_pass_t param) const = delete;

	template <typename Self, typename Host, typename DrawLayerFn>
	void record_draw_layer(
		this const Self& self,
		const element_type& element,
		const Host& owner,
		draw_recorder& call_stack_builder,
		DrawLayerFn&& draw_layer_fn){
		if constexpr(requires(
			const Self& interface,
			const element_type& item,
			const owner_type& owner_ref,
			rect clipSpace,
			float opacityScl,
			fx::layer_param_pass_t param){
			interface.draw_layer(item, owner_ref, clipSpace, opacityScl, param);
		}){
			call_stack_builder.push_call_noop(owner, std::forward<DrawLayerFn>(draw_layer_fn));
		} else{
			slot_kind::for_elem(element, [&](const elem& elem_child){
				elem_child.record_draw_layer(call_stack_builder);
			});
		}
	}

	void layout_elem(element_type& element){
		slot_kind::for_elem(element, [](elem& elem_child){
			elem_child.layout_elem();
		});
	}

	void set_prefer_extent(element_type& element, math::vec2 ext){
		slot_kind::for_elem(element, [ext](elem& elem_child){
			elem_child.set_prefer_extent(ext);
		});
	}

	void on_context_sync_bind(element_type& element){
		slot_kind::for_elem(element, [](auto& elem_child){
			if constexpr(requires{
				elem_child.on_context_sync_bind();
			}){
				elem_child.on_context_sync_bind();
			}
		});
	}

	void update_abs_src(element_type& element, math::vec2 pos) noexcept{
		slot_kind::for_elem(element, [pos](elem& elem_child){
			elem_child.update_abs_src(pos);
		});
	}

	[[nodiscard]] std::optional<layout::layout_policy> layout_policy(const element_type& element) const noexcept{
		return slot_kind::elem_or(
			element,
			[]() -> std::optional<layout::layout_policy>{
				return std::nullopt;
			},
			[](const elem& elem_child) -> std::optional<layout::layout_policy>{
				return elem_child.get_layout_policy();
			}
		);
	}

	std::optional<math::vec2> pre_acq_size(element_type& element, layout::optional_mastering_extent bound){
		return slot_kind::elem_or(
			element,
			[]() -> std::optional<math::vec2>{
				return std::nullopt;
			},
			[bound](elem& elem_child) mutable -> std::optional<math::vec2>{
				return elem_child.pre_acquire_size(bound);
			}
		);
	}

	bool resize(element_type& element, math::vec2 size, propagate_mask temp_mask){
		return slot_kind::elem_or(
			element,
			[](){
				return true;
			},
			[size, temp_mask](elem& elem_child){
				return elem_child.resize(size, temp_mask);
			}
		);
	}

	[[nodiscard]] math::vec2 get_extent(const element_type& element) const noexcept{
		return slot_kind::elem_or(
			element,
			[](){
				return math::vec2{};
			},
			[](const elem& elem_child){
				return elem_child.extent();
			}
		);
	}
};

export
template <typename T, typename Owner = elem>
struct elem_slot_interface_schema_spec : elem_slot_interface_schema<T, Owner>{
};

namespace detail{
struct elem_slot_schema_spec_probe{};
}

template <>
struct elem_slot_interface_schema_spec<detail::elem_slot_schema_spec_probe, elem>
	: elem_slot_interface_schema<detail::elem_slot_schema_spec_probe, elem>{
	[[nodiscard]] math::vec2 get_extent(const detail::elem_slot_schema_spec_probe&) const noexcept{
		return {1.f, 1.f};
	}
};

namespace detail{
static_assert(requires(
	elem_slot_interface_schema_spec<elem_slot_schema_spec_probe, elem>& interface,
	elem_slot_schema_spec_probe& item,
	layout::optional_mastering_extent bound){
	interface.update(item, 0.f);
	interface.layout_elem(item);
	interface.set_prefer_extent(item, {});
	interface.on_context_sync_bind(item);
	interface.update_abs_src(item, {});
	interface.pre_acq_size(item, bound);
	interface.resize(item, {}, propagate_mask::none);
	interface.get_extent(item);
});
}

export
template <typename ElementType, typename Interface, typename Owner>
struct elem_slot_access : elem_slot_kind<ElementType>{
	using element_type = ElementType;
	using interface_type = Interface;
	using owner_type = Owner;
	using slot_kind = elem_slot_kind<element_type>;

	template <typename Item>
	static auto& get_item(Item& item) requires(!elem_slot_access::is_elem_ptr){
		return slot_kind::get_item(item);
	}

	template <typename Item>
	static decltype(auto) get_elem(Item& item) noexcept requires(elem_slot_access::is_elem_child){
		return slot_kind::get_elem(item);
	}

	template <typename Item, typename Fn>
	static void for_elem(Item& item, Fn&& fn){
		slot_kind::for_elem(item, std::forward<Fn>(fn));
	}

	template <typename Item, typename FallbackFn, typename ElemFn>
	static decltype(auto) elem_or(Item& item, FallbackFn&& fallback_fn, ElemFn&& elem_fn){
		return slot_kind::elem_or(item, std::forward<FallbackFn>(fallback_fn), std::forward<ElemFn>(elem_fn));
	}

	static void update(interface_type& interface, element_type& item, const float delta_in_ticks){
		interface.update(item, delta_in_ticks);
	}

	static void draw_layer(
		const interface_type& interface,
		const element_type& item,
		const owner_type& owner,
		rect clipSpace,
		float opacityScl,
		fx::layer_param_pass_t param){
		if constexpr(requires{
			interface.draw_layer(item, owner, clipSpace, opacityScl, param);
		}){
			interface.draw_layer(item, owner, clipSpace, opacityScl, param);
		}
	}

	template <typename Host, typename DrawLayerFn>
	static void record_draw_layer(
		const interface_type& interface,
		const element_type& item,
		const Host& owner,
		draw_recorder& call_stack_builder,
		DrawLayerFn&& draw_layer_fn){
		interface.record_draw_layer(item, owner, call_stack_builder, std::forward<DrawLayerFn>(draw_layer_fn));
	}

	static void layout_elem(interface_type& interface, element_type& item){
		interface.layout_elem(item);
	}

	static void set_prefer_extent(interface_type& interface, element_type& item, math::vec2 ext){
		interface.set_prefer_extent(item, ext);
	}

	static void on_context_sync_bind(interface_type& interface, element_type& item){
		interface.on_context_sync_bind(item);
	}

	static void update_abs_src(interface_type& interface, element_type& item, math::vec2 pos) noexcept{
		interface.update_abs_src(item, pos);
	}

	template <typename ValueChildCache>
	static elem_span exposed_children(const element_type& item, const ValueChildCache& value_child_cache) noexcept{
		if constexpr(elem_slot_access::is_elem_ptr){
			return {item.raw_addr(), 1};
		} else if constexpr(elem_slot_access::is_elem_value){
			return {&*value_child_cache, 1};
		} else{
			return {};
		}
	}

	static elem_span elem_ptr_children(const element_type& item) noexcept requires(elem_slot_access::is_elem_ptr){
		return {item.raw_addr(), 1};
	}

	static elem_span elem_value_children(elem* const* child) noexcept requires(elem_slot_access::is_elem_value){
		return {child, 1};
	}

	[[nodiscard]] static std::optional<layout::layout_policy> layout_policy(
		const interface_type& interface,
		const element_type& item) noexcept{
		return interface.layout_policy(item);
	}

	static std::optional<math::vec2> pre_acq_size(
		interface_type& interface,
		element_type& item,
		layout::optional_mastering_extent bound){
		return interface.pre_acq_size(item, bound);
	}

	static bool resize(interface_type& interface, element_type& item, const math::vec2 size, propagate_mask temp_mask){
		return interface.resize(item, size, temp_mask);
	}

	[[nodiscard]] static math::vec2 get_extent(const interface_type& interface, const element_type& item) noexcept{
		return interface.get_extent(item);
	}
};

}
