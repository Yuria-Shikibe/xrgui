module;

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.infrastructure:elem_async_task;

import std;
import :defines;
import :elem_ptr;

namespace mo_yanxi::gui{
export
struct basic_elem_async_task{
private:
	elem* owner{};
	unsigned total_{};
	std::atomic_uint progress_{};

protected:
	void set_progress(unsigned current) noexcept{
		progress_.store(std::min(current, total_), std::memory_order_release);
	}

public:
	virtual ~basic_elem_async_task() = default;

	[[nodiscard]] explicit basic_elem_async_task(elem& owner)
		: owner(std::addressof(owner)){
	}

	auto get_progress() const noexcept{
		return progress_.load(std::memory_order_relaxed);
	}

	auto get_progress_ratio() const noexcept{
		return static_cast<float>(get_progress()) / static_cast<float>(total_);
	}

	elem& get_owner() const noexcept{
		return *owner;
	}

	virtual void process() = 0;

	virtual void on_done() = 0;
};

export
template <std::derived_from<elem> E, typename ProcessFn, typename DoneFn>
struct elem_async_task : basic_elem_async_task{
	ADAPTED_NO_UNIQUE_ADDRESS ProcessFn process_fn_;
	ADAPTED_NO_UNIQUE_ADDRESS DoneFn done_fn_;

	template <std::invocable<E&> ProcessFnTy, std::invocable<E&> DoneFnTy>
	[[nodiscard]] elem_async_task(E& owner, ProcessFnTy&& process_fn, DoneFnTy&& done_fn)
		: basic_elem_async_task(owner),
		  process_fn_(std::forward<ProcessFnTy>(process_fn)),
		  done_fn_(std::forward<DoneFnTy>(done_fn)){
	}

	void process() override{
		std::invoke(process_fn_, static_cast<E&>(get_owner()));
	}
	void on_done() override{
		std::invoke(done_fn_, static_cast<E&>(get_owner()));
	}
};

template <std::derived_from<elem> E, typename ProcessFnTy, typename DoneFnTy>
elem_async_task(E&, ProcessFnTy, DoneFnTy) -> elem_async_task<E, std::decay_t<ProcessFnTy>, std::decay_t<DoneFnTy>>;


export
template <std::derived_from<elem> E, typename ProcessFn, typename DoneFn>
struct elem_async_yield_task : basic_elem_async_task{
	using yield_result_t = std::invoke_result_t<ProcessFn&, E&>;
	static_assert(std::is_move_assignable_v<yield_result_t>);

	ADAPTED_NO_UNIQUE_ADDRESS ProcessFn process_fn_;
	ADAPTED_NO_UNIQUE_ADDRESS DoneFn done_fn_;
	ADAPTED_NO_UNIQUE_ADDRESS yield_result_t result_;

	template <std::invocable<E&> ProcessFnTy, std::invocable<E&, std::invoke_result_t<ProcessFn&, E&>&&> DoneFnTy>
	[[nodiscard]] elem_async_yield_task(E& owner, ProcessFnTy&& process_fn, DoneFnTy&& done_fn)
		: basic_elem_async_task(owner),
		  process_fn_(std::forward<ProcessFnTy>(process_fn)),
		  done_fn_(std::forward<DoneFnTy>(done_fn)){
	}

	void process() override{
		result_ = std::invoke_r<yield_result_t>(process_fn_, static_cast<E&>(get_owner()));
	}

	void on_done() override{
		std::invoke(done_fn_, static_cast<E&>(get_owner()), std::move(result_));
	}
};

template <std::derived_from<elem> E, typename ProcessFnTy, typename DoneFnTy>
elem_async_yield_task(E&, ProcessFnTy, DoneFnTy) -> elem_async_yield_task<E, std::decay_t<ProcessFnTy>, std::decay_t<DoneFnTy>>;



}
