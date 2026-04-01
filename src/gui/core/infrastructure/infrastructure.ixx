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

export import mo_yanxi.gui.renderer.frontend;
export import mo_yanxi.input_handle;
export import mo_yanxi.gui.alloc;
export import align;

namespace mo_yanxi::gui{
export namespace align = ::mo_yanxi::align;


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
template <std::derived_from<elem> E, std::invocable<E&> Prov>
void post_elem_async_task(E& e, Prov&& prov){
	static_cast<const elem&>(e).get_scene().post_elem_async_task(e, std::forward<Prov>(prov));
}
}

}
