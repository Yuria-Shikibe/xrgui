module;

export module mo_yanxi.gui.infrastructure;

export import :defines;
export import :elem_ptr;
export import :element;
export import :events;
export import :scene;
export import :ui_manager;
export import :tooltip_interface;
export import :tooltip_manager;
export import :dialog_manager;
export import :cursor;
export import :flags;
export import :elem_async_task;

export import mo_yanxi.gui.window_thread_dispatcher;
export import mo_yanxi.gui.renderer.frontend;
export import mo_yanxi.input_handle;
export import mo_yanxi.gui.alloc;
export import align;

namespace mo_yanxi::gui{
export namespace align = ::mo_yanxi::align;

template <typename Owner>
concept react_flow_scene_owner = std::derived_from<std::remove_cvref_t<Owner>, scene_base>;

template <typename Owner>
concept react_flow_elem_owner = std::derived_from<std::remove_cvref_t<Owner>, elem>;

template <typename Owner>
concept react_flow_owner = react_flow_scene_owner<Owner> || react_flow_elem_owner<Owner>;

template <react_flow_owner Owner>
scene_base& react_flow_owner_scene_(Owner& owner) noexcept{
	if constexpr (react_flow_elem_owner<Owner>){
		return owner.get_scene();
	}else{
		return owner;
	}
}

template <react_flow_owner Owner>
const elem* react_flow_owner_elem_(Owner& owner) noexcept{
	if constexpr (react_flow_elem_owner<Owner>){
		return std::addressof(static_cast<elem&>(owner));
	}else{
		return nullptr;
	}
}

struct react_flow_create_access{
	template <typename AddFn>
	static decltype(auto) add_node(scene_base& scene, const elem* owner, AddFn&& add){
		return scene.react_flow_add_node_(owner, std::forward<AddFn>(add));
	}

	static bool erase_node(scene_base& scene, react_flow::node& node){
		return scene.react_flow_erase_node_(node);
	}
};

export
template <react_flow_owner Owner, std::derived_from<react_flow::node> NodeType, typename... Args>
decltype(auto) react_flow_attach_impl(Owner& owner, std::in_place_type_t<NodeType>, Args&&... args){
	auto& scene = gui::react_flow_owner_scene_(owner);
	const elem* owner_elem = gui::react_flow_owner_elem_(owner);

	return react_flow_create_access::add_node(scene, owner_elem, [&](react_flow::manager& manager) -> NodeType&{
		return manager.template add_node<NodeType>(std::forward<Args>(args)...);
	});
}

export
template <react_flow_owner Owner, typename Node>
	requires std::derived_from<std::remove_cvref_t<Node>, react_flow::node>
decltype(auto) react_flow_attach_impl(Owner& owner, Node&& node){
	auto& scene = gui::react_flow_owner_scene_(owner);
	const elem* owner_elem = gui::react_flow_owner_elem_(owner);

	return react_flow_create_access::add_node(scene, owner_elem, [&](react_flow::manager& manager) -> decltype(auto){
		return manager.add_node(std::forward<Node>(node));
	});
}

export
template <react_flow_owner Owner>
decltype(auto) react_flow_attach_impl(Owner& owner, react_flow::node_pointer node){
	auto& scene = gui::react_flow_owner_scene_(owner);
	const elem* owner_elem = gui::react_flow_owner_elem_(owner);

	return react_flow_create_access::add_node(scene, owner_elem, [&](react_flow::manager& manager) -> decltype(auto){
		return manager.add_node(std::move(node));
	});
}

export
template <react_flow_scene_owner Owner>
bool react_flow_erase_impl(Owner& owner, react_flow::node& node){
	return react_flow_create_access::erase_node(owner, node);
}


template <typename E, typename Fn>
void native_communicator::request_clipboard(E& owner, Fn&& on_ready){
	this->request_clipboard_impl(
		owner.get_scene().make_native_clipboard_request(owner, std::forward<Fn>(on_ready)));
}

template <typename E, std::invocable<E&> Fn>
void elem::post_task(this E& e, Fn&& fn){
	static_cast<const elem&>(e).get_scene().post(e, std::forward<Fn>(fn));
}

template <typename E, std::invocable<> Fn>
void elem::post_task(this E& e, Fn&& fn){
	static_cast<const elem&>(e).get_scene().post(e, std::forward<Fn>(fn));
}

namespace util{

export
template <typename E>
	requires (std::is_enum_v<E> && std::convertible_to<std::underlying_type_t<E>, std::size_t>)
void sync_set_elem_style(elem& e, E v){
	e.sync_run([v](elem& el){
		el.set_style(el.get_style_tree_manager().get_default<elem>(v));
	});
}
export
template <typename E>
	requires (std::is_enum_v<E> && std::convertible_to<std::underlying_type_t<E>, std::size_t>)
void sync_set_elem_style(elem& e, E v, std::string_view style_family_name){
	e.sync_run([v, style_family_name](elem& el){
		el.set_style(el.get_style_tree_manager().get_slice<elem>().value().get_or_default(style_family_name, std::to_underlying(v)));
	});
}

export
template <std::derived_from<elem> E, std::invocable<E&> Prov>
elem_async_task_handle post_elem_async_task(E& e, Prov&& prov){
	return static_cast<const elem&>(e).get_scene().post_elem_async_task(e, std::forward<Prov>(prov));
}
}

}
