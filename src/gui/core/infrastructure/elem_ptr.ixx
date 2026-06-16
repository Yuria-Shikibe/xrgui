module;

#include <cassert>

export module mo_yanxi.gui.infrastructure:elem_ptr;

export import mo_yanxi.math.vector2;
export import mo_yanxi.math.rect_ortho;

import mo_yanxi.gui.alloc;
import mo_yanxi.function_manipulate;
import mo_yanxi.concepts;
import mo_yanxi.transparent_span;
import std;

namespace mo_yanxi::gui{

export struct elem;
export struct scene;
export struct scene_base;

export
bool is_on_scene_thread(const scene_base& scene) noexcept;

export
enum struct elem_lifecycle_state : std::uint8_t{
	live,
	detached,
	destroying
};

export
struct elem_ref_access{
	static bool retain_live(elem* element) noexcept;
	static void retain_existing(elem* element) noexcept;
	static void release(elem* element) noexcept;
	static bool is_live(const elem* element) noexcept;
	static std::stop_token stop_token(const elem* element) noexcept;
};

export
template <std::derived_from<elem> T = elem>
struct elem_ref{
private:
	T* element_{};

public:
	[[nodiscard]] elem_ref() = default;

	template <std::derived_from<T> E>
	[[nodiscard]] explicit(false) elem_ref(E& element) noexcept
		: element_(std::addressof(element)){
		if(!elem_ref_access::retain_live(element_)){
			assert(false && "elem_ref can only be created from a live element");
			std::terminate();
		}
	}

	[[nodiscard]] elem_ref(const elem_ref& other) noexcept
		: element_(other.element_){
		if(element_ != nullptr){
			elem_ref_access::retain_existing(element_);
		}
	}

	template <std::derived_from<T> E>
	[[nodiscard]] elem_ref(const elem_ref<E>& other) noexcept
		: element_(other.get_retained()){
		if(element_ != nullptr){
			elem_ref_access::retain_existing(element_);
		}
	}

	[[nodiscard]] elem_ref(elem_ref&& other) noexcept
		: element_(std::exchange(other.element_, nullptr)){
	}

	template <std::derived_from<T> E>
	[[nodiscard]] elem_ref(elem_ref<E>&& other) noexcept
		: element_(std::exchange(other.element_, nullptr)){
	}

	~elem_ref(){
		if(element_ != nullptr){
			elem_ref_access::release(element_);
		}
	}

	elem_ref& operator=(const elem_ref& other) noexcept{
		if(this == std::addressof(other)){
			return *this;
		}
		elem_ref copy{other};
		this->swap(copy);
		return *this;
	}

	template <std::derived_from<T> E>
	elem_ref& operator=(const elem_ref<E>& other) noexcept{
		elem_ref copy{other};
		this->swap(copy);
		return *this;
	}

	elem_ref& operator=(elem_ref&& other) noexcept{
		if(this == std::addressof(other)){
			return *this;
		}
		elem_ref copy{std::move(other)};
		this->swap(copy);
		return *this;
	}

	template <std::derived_from<T> E>
	elem_ref& operator=(elem_ref<E>&& other) noexcept{
		elem_ref copy{std::move(other)};
		this->swap(copy);
		return *this;
	}

	void swap(elem_ref& other) noexcept{
		std::swap(element_, other.element_);
	}

	[[nodiscard]] T* get_live() const noexcept{
		return elem_ref_access::is_live(element_) ? element_ : nullptr;
	}

	[[nodiscard]] T* get_retained() const noexcept{
		return element_;
	}

	[[nodiscard]] explicit operator bool() const noexcept{
		return element_ != nullptr;
	}

	[[nodiscard]] T& operator*() const noexcept{
		auto* live = this->get_live();
		assert(live != nullptr && "detached elem_ref cannot be used for UI access");
		return *live;
	}

	[[nodiscard]] T* operator->() const noexcept{
		auto* live = this->get_live();
		assert(live != nullptr && "detached elem_ref cannot be used for UI access");
		return live;
	}

	template <std::derived_from<elem>>
	friend struct elem_ref;
};

export
template <std::derived_from<elem> T>
void swap(elem_ref<T>& lhs, elem_ref<T>& rhs) noexcept{
	lhs.swap(rhs);
}

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
template <typename Fn, typename... Args>
struct element_create_pacakge{
	using create_type = elem_init_func_create_t<Fn>;
	static constexpr bool is_const_invoke_op = std::invocable<const std::remove_cvref_t<Fn>&, create_type&>;

	Fn fn;
	std::tuple<Args ...> args;

	void operator()(this std::conditional_t<is_const_invoke_op, const element_create_pacakge, element_create_pacakge>& self, create_type& e){
		std::invoke(self.fn, e);
	}

	template <typename F, typename... Ts>
	explicit(false) constexpr element_create_pacakge(F&& f, Ts&&... ts)
		: fn(std::forward<F>(f)),
		  args(std::forward<Ts>(ts)...) {}
};

template <typename Fn, typename... Args>
decltype(auto) get_args_of_package(element_create_pacakge<Fn, Args...>&& p) noexcept{
	return std::move(p).args;
}

template <typename Fn, typename... Args>
decltype(auto) get_args_of_package(const element_create_pacakge<Fn, Args...>& p) noexcept{
	return p.args;
}

template <typename F, typename... Ts>
element_create_pacakge(F&&, Ts&&...) -> element_create_pacakge<std::decay_t<F>, std::decay_t<Ts>...>;

export
template <typename T>
concept elem_create_pacakge = invocable_elem_init_func<T> && spec_of<std::remove_cvref_t<T>, element_create_pacakge>;

export
struct elem_ptr{

	[[nodiscard]] elem_ptr() = default;

	[[nodiscard]] explicit elem_ptr(elem* element)
		: element{element}{
	}

	template <typename  InitFunc, typename... Args>
	requires (!spec_of<InitFunc, std::in_place_type_t> && invocable_elem_init_func<InitFunc>)
[[nodiscard]] elem_ptr(scene& scene, elem* parent, InitFunc&& initFunc, Args&&... args)
	: elem_ptr{
		scene, parent, std::in_place_type<elem_init_func_create_t<InitFunc>>, std::forward<Args>(args)...
	}{
		std::invoke(initFunc,
					static_cast<std::add_lvalue_reference_t<elem_init_func_create_t<InitFunc>>>(*
						element));
	}


	template <typename P>
		requires (!spec_of<P, std::in_place_type_t> && elem_create_pacakge<P>)
	[[nodiscard]] elem_ptr(scene& scene, elem* group, P&& pacakge){
		using create_t = elem_init_func_create_t<P>;
		element = std::apply([&]<typename ...T>(T&& ...args){
			return elem_ptr::new_elem<create_t>(scene, group, std::forward<T>(args)...);
		}, gui::get_args_of_package(std::forward<P>(pacakge)));
		try{
			pacakge(static_cast<create_t&>(*element));
		}catch(...){
			delete_elem(element);
			throw;
		}

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
			try{
				elem_ptr::dynamic_init(*p);
			}catch(...){
				std::destroy_at(p);
				throw;
			}
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

	template <std::derived_from<elem> T = elem>
	static void dynamic_init(T& ptr);

public:
	static constexpr auto cvt_mptr = transparent_convert<&elem_ptr::element>;
};
}
