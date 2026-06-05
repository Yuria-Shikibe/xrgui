module;

export module mo_yanxi.gui.elem.element_slot;

export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.gui.layout.policies;

import std;

namespace mo_yanxi::gui{

export
template <typename T, typename Owner = elem>
struct elem_slot_interface_schema{
	using element_type = T;
	using owner_type = Owner;

	void update(element_type& element, float delta_in_tick) = delete;
	void draw_layer(
		const element_type& element,
		const owner_type& owner,
		rect clipSpace,
		float opacityScl,
		fx::layer_param_pass_t param) const = delete;
	void layout_elem(element_type& element) = delete;
	void set_prefer_extent(element_type& element, math::vec2 ext) = delete;
	void on_context_sync_bind(element_type& element) = delete;
	void update_abs_src(element_type& element, math::vec2 pos) = delete;
	std::optional<math::vec2> pre_acq_size(element_type& element, layout::optional_mastering_extent bound) = delete;
	bool resize(element_type& element, math::vec2 size, propagate_mask temp_mask) = delete;
	math::vec2 get_extent(const element_type& element) const = delete;
};

export
template <typename Interface, typename Owner>
struct elem_slot_interface_trait{
	using interface_type = Interface;
	using owner_type = Owner;
	using element_type = typename interface_type::element_type;

	static constexpr bool has_update = requires(interface_type& i, element_type& e, float d){
		i.update(e, d);
	};

	static constexpr bool has_draw_layer = requires(
		const interface_type& i,
		const owner_type& owner,
		const element_type& e,
		rect c,
		float o,
		fx::layer_param_pass_t p){
		i.draw_layer(e, owner, c, o, p);
	};

	static constexpr bool has_layout_elem = requires(interface_type& i, element_type& e){
		i.layout_elem(e);
	};

	static constexpr bool has_set_prefer_extent = requires(interface_type& i, element_type& e, math::vec2 ext){
		i.set_prefer_extent(e, ext);
	};

	static constexpr bool has_on_context_sync_bind = requires(interface_type& i, element_type& e){
		i.on_context_sync_bind(e);
	};

	static constexpr bool has_update_abs_src = requires(interface_type& i, element_type& e, math::vec2 pos){
		i.update_abs_src(e, pos);
	};

	static constexpr bool has_pre_acq_size = requires(
		interface_type& i,
		element_type& e,
		layout::optional_mastering_extent b){
		i.pre_acq_size(e, b);
	};

	static constexpr bool has_resize = requires(interface_type& i, element_type& e, math::vec2 s, propagate_mask m){
		i.resize(e, s, m);
	};

	static constexpr bool has_get_extent = requires(const interface_type& i, const element_type& e){
		i.get_extent(e);
	};
};

export
template <typename ElementType>
struct elem_slot_kind{
	using element_type = ElementType;

	static constexpr bool is_elem_ptr = std::is_same_v<element_type, elem_ptr>;
	static constexpr bool is_elem_value = std::derived_from<element_type, elem>;
	static constexpr bool is_elem_child = is_elem_ptr || is_elem_value;

	static_assert(is_elem_value || std::is_default_constructible_v<element_type>,
	              "if element is not a T : elem, it must be default constructible");
};

export
template <typename ElementType, typename Interface, typename Owner>
struct elem_slot_access : elem_slot_kind<ElementType>{
	using element_type = ElementType;
	using interface_type = Interface;
	using owner_type = Owner;
	using interface_trait = elem_slot_interface_trait<interface_type, owner_type>;

	template <typename Item>
	static auto& get_item(Item& item) requires(!elem_slot_access::is_elem_ptr){
		return item;
	}

	template <typename Item>
	static decltype(auto) get_elem(Item& item) noexcept requires(elem_slot_access::is_elem_child){
		if constexpr(requires(Item& e){
			{ *e } -> std::convertible_to<elem&>;
		}){
			return *item;
		} else if constexpr(elem_slot_access::is_elem_value){
			return item;
		}
	}

	template <typename Item, typename Fn>
	static void for_elem(Item& item, Fn&& fn){
		if constexpr(elem_slot_access::is_elem_child){
			std::invoke(std::forward<Fn>(fn), elem_slot_access::get_elem(item));
		}
	}

	template <typename Item, typename FallbackFn, typename ElemFn>
	static decltype(auto) elem_or(Item& item, FallbackFn&& fallback_fn, ElemFn&& elem_fn){
		if constexpr(elem_slot_access::is_elem_child){
			return std::invoke(std::forward<ElemFn>(elem_fn), elem_slot_access::get_elem(item));
		} else{
			return std::invoke(std::forward<FallbackFn>(fallback_fn));
		}
	}

	static void update(interface_type& interface, element_type& item, const float delta_in_ticks){
		if constexpr(interface_trait::has_update){
			interface.update(item, delta_in_ticks);
		}
	}

	static void draw_layer(
		const interface_type& interface,
		const element_type& item,
		const owner_type& owner,
		rect clipSpace,
		float opacityScl,
		fx::layer_param_pass_t param){
		if constexpr(interface_trait::has_draw_layer){
			interface.draw_layer(item, owner, clipSpace, opacityScl, param);
		}
	}

	template <typename Host, typename DrawLayerFn>
	static void record_draw_layer(
		[[maybe_unused]] const interface_type& interface,
		[[maybe_unused]] const element_type& item,
		const Host& owner,
		draw_recorder& call_stack_builder,
		DrawLayerFn&& draw_layer_fn){
		if constexpr(interface_trait::has_draw_layer){
			call_stack_builder.push_call_noop(owner, std::forward<DrawLayerFn>(draw_layer_fn));
		} else{
			elem_slot_access::for_elem(item, [&](const elem& element){
				element.record_draw_layer(call_stack_builder);
			});
		}
	}

	static void layout_elem(interface_type& interface, element_type& item){
		if constexpr(interface_trait::has_layout_elem){
			interface.layout_elem(item);
		} else if constexpr(elem_slot_access::is_elem_child){
			elem_slot_access::get_elem(item).layout_elem();
		}
	}

	static void set_prefer_extent(interface_type& interface, element_type& item, math::vec2 ext){
		if constexpr(interface_trait::has_set_prefer_extent){
			interface.set_prefer_extent(item, ext);
		} else if constexpr(elem_slot_access::is_elem_child){
			elem_slot_access::get_elem(item).set_prefer_extent(ext);
		}
	}

	static void on_context_sync_bind(interface_type& interface, element_type& item){
		if constexpr(interface_trait::has_on_context_sync_bind){
			interface.on_context_sync_bind(item);
		} else if constexpr(elem_slot_access::is_elem_child){
			elem_slot_access::get_elem(item).on_context_sync_bind();
		}
	}

	static void update_abs_src(interface_type& interface, element_type& item, math::vec2 pos) noexcept{
		if constexpr(interface_trait::has_update_abs_src){
			interface.update_abs_src(item, pos);
		} else if constexpr(elem_slot_access::is_elem_child){
			elem_slot_access::get_elem(item).update_abs_src(pos);
		}
	}

	template <typename ValueChildCache>
	static elem_span exposed_children(const element_type& item, const ValueChildCache& value_child_cache) noexcept{
		if constexpr(elem_slot_access::is_elem_ptr){
			return {item, elem_ptr::cvt_mptr};
		} else if constexpr(elem_slot_access::is_elem_value){
			return {&*value_child_cache, 1};
		} else{
			return {};
		}
	}

	static elem_span elem_ptr_children(const element_type& item) noexcept requires(elem_slot_access::is_elem_ptr){
		return {item, elem_ptr::cvt_mptr};
	}

	static elem_span elem_value_children(elem* const* child) noexcept requires(elem_slot_access::is_elem_value){
		return {child, 1};
	}

	[[nodiscard]] static std::optional<layout::layout_policy> layout_policy(const element_type& item) noexcept{
		return elem_slot_access::elem_or(
			item,
			[]() -> std::optional<layout::layout_policy>{
				return std::nullopt;
			},
			[](const elem& element) -> std::optional<layout::layout_policy>{
				return element.get_layout_policy();
			}
		);
	}

	static std::optional<math::vec2> pre_acq_size(
		interface_type& interface,
		element_type& item,
		layout::optional_mastering_extent bound){
		if constexpr(interface_trait::has_pre_acq_size){
			return interface.pre_acq_size(item, bound);
		} else if constexpr(elem_slot_access::is_elem_child){
			return elem_slot_access::get_elem(item).pre_acquire_size(bound);
		} else{
			return std::nullopt;
		}
	}

	static bool resize(interface_type& interface, element_type& item, const math::vec2 size, propagate_mask temp_mask){
		if constexpr(interface_trait::has_resize){
			return interface.resize(item, size, temp_mask);
		} else if constexpr(elem_slot_access::is_elem_child){
			return elem_slot_access::get_elem(item).resize(size, temp_mask);
		} else{
			return true;
		}
	}

	[[nodiscard]] static math::vec2 get_extent(const interface_type& interface, const element_type& item) noexcept{
		if constexpr(interface_trait::has_get_extent){
			return interface.get_extent(item);
		} else if constexpr(elem_slot_access::is_elem_child){
			return elem_slot_access::get_elem(item).extent();
		} else{
			return {};
		}
	}
};

}
