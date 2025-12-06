module;

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.action;

import mo_yanxi.gui.alloc;
import mo_yanxi.math.timed;
import mo_yanxi.meta_programming;
import mo_yanxi.math.interpolation;
import std;

namespace mo_yanxi::gui::action{
export
template <typename T>
using interp_type = float(*)(float, const T&) noexcept;

export
template <typename T>
struct action_ptr;

export
template <typename T>
struct action{
	friend struct action_ptr<T>;

	using target_type = T;
	using interp_func_t = interp_type<T>;
	protected:
	mr::heap_allocator<> allocator{mr::get_default_heap_allocator()};
	math::timed scale{};
	void(*deleter)(action&) noexcept{};

public:
	interp_func_t interp_func{};

	virtual ~action() = default;

	action() = default;

	[[nodiscard]] explicit action(const mr::heap_allocator<>& allocator)
		: allocator(allocator){
	}

	action(const mr::heap_allocator<>& allocator, const float lifetime, interp_type<T> interpFunc)
	: allocator(allocator), scale(lifetime), interp_func(interpFunc){
	}

	action(const mr::heap_allocator<>& allocator, const float lifetime)
	: action(allocator, lifetime, nullptr){
	}

	/**
	 * @return The excess time, it > 0 means this action has been done
	 */
	virtual float update(const float delta, T& elem){
		float ret = -1.0f;

		if(scale.time == 0.0f){
			this->begin(elem);
			scale.time = std::numeric_limits<float>::min();
		}

		if(const auto remain = scale.time - scale.lifetime; remain >= 0){
			return remain + delta;
		}

		scale.time += delta;

		if(const auto remain = scale.time - scale.lifetime; remain >= 0){
			ret = remain;

			scale.time = scale.lifetime;
			this->apply(elem, this->get_progress_of(1.f, elem));
			this->end(elem);
		} else{
			this->apply(elem, this->get_progress_of(scale.get(), elem));
		}

		return ret;
	}

protected:
	[[nodiscard]] float get_progress_of(const float f, const T& elem) const{
		return interp_func ? interp_func(f, elem) : f;
	}

	virtual void apply(T& elem, float progress){
	}

	virtual void begin(T& elem){
	}

	virtual void end(T& elem){
	}
};

template <typename T>
struct action_ptr{
private:
	action<T>* action_;

public:
	template <std::derived_from<action<T>> Ty, typename ...Args>
		requires std::constructible_from<Ty, const mr::heap_allocator<Ty>&, Args&&...>
	[[nodiscard]] explicit action_ptr(std::in_place_type_t<Ty>, mr::heap_allocator<std::type_identity_t<Ty>> alloc, Args&& ...args){
		Ty* ptr = alloc.allocate(1);
		try{
			std::construct_at(ptr, alloc, std::forward<Args>(args)...);
		}catch(...){
			alloc.deallocate(ptr, 1);
			throw;
		}

		action_ = ptr;
		action_->deleter = +[](action<T>& action) static noexcept{
			const auto addr = std::addressof(static_cast<Ty&>(action));
			std::destroy_at(addr);
			mr::heap_allocator<Ty> alloc{action.allocator};
			std::allocator_traits<mr::heap_allocator<Ty>>::deallocate(alloc, addr, 1);
		};
	}

	explicit(false) action_ptr(std::nullptr_t) noexcept : action_(nullptr) {}

	action_ptr(action_ptr&& other) noexcept
		: action_{ std::exchange(other.action_, nullptr) } {
	}

	action_ptr& operator=(action_ptr&& other) noexcept {
		if(this == &other) return *this;
		if(action_)destory_();
		action_ = std::exchange(other.action_, {});
		return *this;
	}

	action_ptr(const action_ptr&) = delete;
	action_ptr& operator=(const action_ptr&) = delete;

	void reset(action<T>* ptr = nullptr) noexcept {
		if (action_) {
			destory_();
		}
		action_ = ptr;
	}

	[[nodiscard]] action<T>* get() const noexcept {
		return action_;
	}

	[[nodiscard]] action<T>& operator*() const noexcept {
		assert(action_ != nullptr && "Cannot dereference a null action_ptr");
		return *action_;
	}

	[[nodiscard]] action<T>* operator->() const noexcept {
		assert(action_ != nullptr && "Cannot access members of a null action_ptr");
		return action_;
	}

	explicit operator bool() const noexcept {
		return action_ != nullptr;
	}

private:
	void destory_() noexcept{
		assert(action_->deleter != nullptr);
		action_->deleter(*action_);
	}
};

export
template <typename T>
struct parallel_action : action<T>{
protected:
	void select_max_lifetime(){
		for(auto& action : actions){
			this->scale.lifetime = std::max(this->scale.lifetimem, action->scale.lifetime);
		}
	}

public:
	std::vector<std::unique_ptr<action<T>>> actions{};

	explicit parallel_action(std::vector<std::unique_ptr<action<T>>>&& actions)
	: actions(std::move(actions)){
		select_max_lifetime();
	}

	explicit(false) parallel_action(const std::initializer_list<std::unique_ptr<action<T>>> actions)
	: actions(actions){
		select_max_lifetime();
	}

	float update(const float delta, T& elem) override{
		std::erase_if(actions, [&](std::unique_ptr<action<T>>& act){
			return act->update(delta, elem) >= 0;
		});

		return action<T>::update(delta, elem);
	}
};

export
template <typename T>
struct aligned_parallel_action : action<T>{
protected:
	void selectMaxLifetime(){
		for(auto& action : actions){
			this->scale.lifetime = std::max(this->scale.lifetimem, action->scale.lifetime);
		}
	}

public:
	std::vector<std::unique_ptr<action<T>>> actions{};

	explicit aligned_parallel_action(const float lifetime, const interp_type<T>& interpFunc,
		std::vector<std::unique_ptr<action<T>>>&& actions)
	: action<T>{lifetime, interpFunc}, actions(std::move(actions)){
		selectMaxLifetime();
	}

	aligned_parallel_action(const float lifetime, const interp_type<T>& interpFunc,
		const std::initializer_list<std::unique_ptr<action<T>>> actions)
	: action<T>{lifetime, interpFunc}, actions(actions){
		selectMaxLifetime();
	}

protected:
	void apply(T& elem, float progress) override{
		for(auto&& action : actions){
			action->apply(elem, action.get_progress_of(progress));
		}
	}

	void begin(T& elem) override{
		for(auto&& action : actions){
			action->begin(elem);
		}
	}

	void end(T& elem) override{
		for(auto&& action : actions){
			action->end(elem);
		}
	}
};

export
template <typename T>
struct delay_action : action<T>{
	explicit delay_action(const float lifetime) : action<T>(lifetime){
	}

	float update(const float delta, T& elem) override{
		this->scale.time += delta;

		if(const auto remain = this->scale.time - this->scale.lifetime; remain >= 0){
			return remain;
		}

		return -1.f;
	}
};

template <typename T>
struct EmptyApplyFunc{
	void operator()(T&, float){
	}
};

export
template <typename T, std::invocable<T&, float> FuncApply, std::invocable<T&> FuncBegin = std::identity, std::invocable<
	T&> FuncEnd = std::identity>
struct RunnableAction : action<T>{
	ADAPTED_NO_UNIQUE_ADDRESS std::decay_t<FuncApply> funcApply{};
	ADAPTED_NO_UNIQUE_ADDRESS std::decay_t<FuncBegin> funcBegin{};
	ADAPTED_NO_UNIQUE_ADDRESS std::decay_t<FuncEnd> funcEnd{};

	[[nodiscard]] RunnableAction() = default;

	explicit RunnableAction(FuncApply&& apply, FuncBegin&& begin = {}, FuncEnd&& end = {}) :
	funcApply{std::forward<FuncApply>(apply)},
	funcBegin{std::forward<FuncBegin>(begin)},
	funcEnd{std::forward<FuncEnd>(end)}{
	}

	explicit RunnableAction(FuncBegin&& begin = {}, FuncEnd&& end = {}) :
	funcBegin{std::forward<FuncBegin>(begin)},
	funcEnd{std::forward<FuncEnd>(end)}{
	}

	RunnableAction(const RunnableAction& other)
		noexcept(std::is_nothrow_copy_constructible_v<FuncApply> && std::is_nothrow_copy_constructible_v<FuncBegin> &&
			std::is_nothrow_copy_constructible_v<FuncEnd>)
		requires (std::is_copy_constructible_v<FuncApply> && std::is_copy_constructible_v<FuncBegin> &&
			std::is_copy_constructible_v<FuncEnd>) = default;

	RunnableAction(RunnableAction&& other)
		noexcept(std::is_nothrow_move_constructible_v<FuncApply> && std::is_nothrow_move_constructible_v<FuncBegin> &&
			std::is_nothrow_move_constructible_v<FuncEnd>)
		requires (std::is_move_constructible_v<FuncApply> && std::is_move_constructible_v<FuncBegin> &&
			std::is_move_constructible_v<FuncEnd>) = default;

	RunnableAction& operator=(const RunnableAction& other)
		noexcept(std::is_nothrow_copy_assignable_v<FuncApply> && std::is_nothrow_copy_assignable_v<FuncBegin> &&
			std::is_nothrow_copy_assignable_v<FuncEnd>)
		requires (std::is_copy_assignable_v<FuncApply> && std::is_copy_assignable_v<FuncBegin> &&
			std::is_copy_assignable_v<FuncEnd>) = default;

	RunnableAction& operator=(RunnableAction&& other)
		noexcept(std::is_nothrow_move_assignable_v<FuncApply> && std::is_nothrow_move_assignable_v<FuncBegin> &&
			std::is_nothrow_move_assignable_v<FuncEnd>)
		requires (std::is_move_assignable_v<FuncApply> && std::is_move_assignable_v<FuncBegin> &&
			std::is_move_assignable_v<FuncEnd>) = default;

	void begin(T& elem) override{
		std::invoke(funcBegin, elem);
	}

	void end(T& elem) override{
		std::invoke(funcEnd, elem);
	}

	void apply(T& elem, float v) override{
		std::invoke(funcApply, elem, v);
	}
};

template <typename FuncApply, typename FuncBegin = std::identity, typename FuncEnd = std::identity>
RunnableAction(FuncApply&&, FuncBegin&&, FuncEnd&&) ->
	RunnableAction<std::decay_t<std::tuple_element_t<0, remove_mfptr_this_args<std::decay_t<FuncApply>>>>,
		FuncApply, FuncBegin, FuncEnd>;

template <typename FuncBegin = std::identity, typename FuncEnd = std::identity>
RunnableAction(FuncBegin&&, FuncEnd&&) ->
	RunnableAction<
		std::decay_t<std::tuple_element_t<0, remove_mfptr_this_args<std::decay_t<FuncBegin>>>>,
		EmptyApplyFunc<std::decay_t<std::tuple_element_t<0, remove_mfptr_this_args<std::decay_t<FuncBegin>>>>>,
		FuncBegin,
		FuncEnd>;

template <typename FuncBegin = std::identity, typename FuncEnd = std::identity>
RunnableAction(FuncBegin&&, FuncEnd&&) ->
	RunnableAction<
		std::decay_t<std::tuple_element_t<0, remove_mfptr_this_args<std::decay_t<FuncEnd>>>>,
		EmptyApplyFunc<std::decay_t<std::tuple_element_t<0, remove_mfptr_this_args<std::decay_t<FuncEnd>>>>>,
		FuncBegin,
		FuncEnd>;
}
