module;

#include <cassert>

export module mo_yanxi.gui.infrastructure:elem_ptr;

export import mo_yanxi.math.vector2;
export import mo_yanxi.math.rect_ortho;

import mo_yanxi.gui.alloc;
import mo_yanxi.func_initialzer;
import mo_yanxi.concepts;
import std;

namespace mo_yanxi::gui{

export struct elem;
export struct scene;


export
template <typename Elem, typename ...Args>
concept constructible_elem = std::constructible_from<Elem, scene&, elem*, Args&&...>;

export
template <typename Fn>
concept elem_init_func = func_initializer_of<std::remove_const_t<Fn>, elem>;

export
template <typename Fn>
concept invocable_elem_init_func = invocable_func_initializer_of<std::remove_const_t<Fn>, elem>;

export
template <typename InitFunc>
struct elem_init_func_trait : protected func_initializer_trait<std::remove_cvref_t<InitFunc>>{
	using elem_type = typename func_initializer_trait<std::remove_cvref_t<InitFunc>>::target_type;
};

export
template <typename InitFunc>
using elem_init_func_create_t = typename elem_init_func_trait<InitFunc>::elem_type;

export
struct elem_ptr{
	[[nodiscard]] elem_ptr() = default;

	[[nodiscard]] explicit elem_ptr(elem* element)
		: element{element}{
	}

	template <typename InitFunc, typename ...Args>
		requires (!spec_of<InitFunc, std::in_place_type_t> && invocable_elem_init_func<InitFunc>)
	[[nodiscard]] elem_ptr(scene& scene, elem* parent, InitFunc&& initFunc, Args&& ...args)
		: elem_ptr{
			scene, parent, std::in_place_type<elem_init_func_create_t<InitFunc>>, std::forward<Args>(args)...
		}{
		std::invoke(initFunc,
		            static_cast<std::add_lvalue_reference_t<elem_init_func_create_t<InitFunc>>>(*
			            element));
	}

	template <typename T, typename... Args>
		requires (constructible_elem<T, Args&&...>)
	[[nodiscard]] elem_ptr(scene& scene, elem* group, std::in_place_type_t<T>, Args&&... args)
		: element{elem_ptr::new_elem<T>(scene, group, std::forward<Args>(args)...)}{
	}

	elem& operator*() const noexcept{
		assert(element != nullptr && "dereference on a null element");
		return *element;
	}

	elem* operator->() const noexcept{
		return element;
	}

	explicit operator bool() const noexcept{
		return element != nullptr;
	}

	[[nodiscard]] elem* get() const noexcept{
		return element;
	}

	[[nodiscard]] elem* release() noexcept{
		return std::exchange(element, nullptr);
	}

	void reset() noexcept{
		this->operator=(elem_ptr{});
	}

	void reset(elem* e) noexcept{
		this->operator=(elem_ptr{e});
	}

	~elem_ptr(){
		if(element) delete_elem(element);
	}

	friend bool operator==(const elem_ptr& lhs, const elem_ptr& rhs) noexcept = default;

	bool operator==(std::nullptr_t) const noexcept{
		return element == nullptr;
	}

	elem_ptr(const elem_ptr& other) = delete;

	elem_ptr(elem_ptr&& other) noexcept
		: element{other.release()}{
	}

	elem_ptr& operator=(elem_ptr&& other) noexcept{
		if(this == &other) return *this;
		if(element) delete_elem(element);
		this->element = other.release();
		return *this;
	}

private:
	elem* element{};

	template <typename T, typename... Args>
	static T* new_elem(scene& scene, elem* parent, Args&&... args){
		using Alloc = mr::heap_allocator<T>;
		Alloc alloc{alloc_of(scene)};
		T* p = std::allocator_traits<Alloc>::allocate(alloc, 1);

		try{
			std::construct_at(p, scene, parent, std::forward<Args>(args)...);
		}catch(...){
			std::allocator_traits<Alloc>::deallocate(alloc, p, 1);
			throw;
		}

		elem_ptr::set_deleter(p, +[](elem* e) noexcept {
			Alloc a{alloc_of(e)};
			std::destroy_at(static_cast<T*>(e));
			std::allocator_traits<Alloc>::deallocate(a, static_cast<T*>(e), 1);
		});
		return p;
	}

	static mr::heap_allocator<elem> alloc_of(const scene& s) noexcept;

	static mr::heap_allocator<elem> alloc_of(const elem* ptr) noexcept;

	static void set_deleter(elem* element, void(*p)(elem*) noexcept) noexcept;

	static void delete_elem(elem* ptr) noexcept;
};

}
