module;

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.infrastructure:elem_async_task;

import std;
import mo_yanxi.allocator_aware_unique_ptr;
import mo_yanxi.concurrent.mpsc_queue;
import mo_yanxi.heterogeneous;
import :defines;
import :elem_ptr;

namespace mo_yanxi::gui{
export
struct elem_async_owner_id{
	std::size_t slot{std::numeric_limits<std::size_t>::max()};
	std::uint64_t generation{};

	[[nodiscard]] bool valid() const noexcept{
		return slot != std::numeric_limits<std::size_t>::max();
	}
};

export
struct elem_async_task_runtime_state{
	std::stop_source stop_source{};
	std::atomic_uint progress_current{};
	std::atomic_uint progress_total{};
	std::atomic_bool active{};
	std::atomic<std::uint64_t> generation{1u};
};

export
struct elem_async_task_handle{
private:
	elem_async_task_runtime_state* state_{};
	std::uint64_t generation_{};

	[[nodiscard]] bool valid_state() const noexcept{
		return state_ != nullptr
			&& state_->generation.load(std::memory_order_acquire) == generation_
			&& state_->active.load(std::memory_order_acquire);
	}

public:
	[[nodiscard]] elem_async_task_handle() = default;

	[[nodiscard]] explicit elem_async_task_handle(elem_async_task_runtime_state& state) noexcept
		: state_(std::addressof(state)),
		  generation_(state.generation.load(std::memory_order_acquire)){
	}

	void request_stop() const noexcept{
		if(this->valid_state()){
			state_->stop_source.request_stop();
		}
	}

	[[nodiscard]] bool stop_requested() const noexcept{
		return this->valid_state() && state_->stop_source.stop_requested();
	}

	[[nodiscard]] unsigned progress() const noexcept{
		if(state_ == nullptr || state_->generation.load(std::memory_order_acquire) != generation_){
			return 0u;
		}
		return state_->progress_current.load(std::memory_order_acquire);
	}

	[[nodiscard]] unsigned progress_total() const noexcept{
		if(state_ == nullptr || state_->generation.load(std::memory_order_acquire) != generation_){
			return 0u;
		}
		return state_->progress_total.load(std::memory_order_acquire);
	}

	[[nodiscard]] float progress_ratio() const noexcept{
		const unsigned total = this->progress_total();
		if(total == 0u){
			return 0.f;
		}
		return static_cast<float>(this->progress()) / static_cast<float>(total);
	}
};

export
struct elem_async_task_context{
private:
	std::stop_token owner_stop_token_{};
	std::stop_token task_stop_token_{};
	elem_async_task_runtime_state* runtime_state_{};

public:
	[[nodiscard]] elem_async_task_context() = default;

	[[nodiscard]] elem_async_task_context(
		std::stop_token owner_stop_token,
		std::stop_token task_stop_token,
		elem_async_task_runtime_state* runtime_state) noexcept
		: owner_stop_token_(std::move(owner_stop_token)),
		  task_stop_token_(std::move(task_stop_token)),
		  runtime_state_(runtime_state){
	}

	[[nodiscard]] bool stop_requested() const noexcept{
		return owner_stop_token_.stop_requested() || task_stop_token_.stop_requested();
	}

	[[nodiscard]] std::stop_token owner_stop_token() const noexcept{
		return owner_stop_token_;
	}

	[[nodiscard]] std::stop_token task_stop_token() const noexcept{
		return task_stop_token_;
	}

	void report_progress(unsigned current, unsigned total) const noexcept{
		if(runtime_state_ == nullptr){
			return;
		}
		if(total == 0u){
			current = 0u;
		}else{
			current = std::min(current, total);
		}
		runtime_state_->progress_total.store(total, std::memory_order_release);
		runtime_state_->progress_current.store(current, std::memory_order_release);
	}
};

export
struct basic_elem_async_task{
private:
	elem_async_owner_id owner_id_{};
	std::stop_token owner_stop_token_{};
	std::stop_token task_stop_token_{};
	elem_async_task_runtime_state* runtime_state_{};
	std::exception_ptr exception_{};

protected:
	virtual void process_impl(elem_async_task_context& context, scene& async_scene) = 0;

public:
	virtual ~basic_elem_async_task() = default;

	[[nodiscard]] basic_elem_async_task() = default;

	void bind_async_owner(
		const elem_async_owner_id owner_id,
		std::stop_token owner_stop_token,
		elem_async_task_runtime_state& runtime_state) noexcept{
		owner_id_ = owner_id;
		owner_stop_token_ = std::move(owner_stop_token);
		runtime_state_ = std::addressof(runtime_state);
		task_stop_token_ = runtime_state.stop_source.get_token();
	}

	[[nodiscard]] elem_async_owner_id owner_id() const noexcept{
		return owner_id_;
	}

	[[nodiscard]] bool stop_requested() const noexcept{
		return owner_stop_token_.stop_requested() || task_stop_token_.stop_requested();
	}

	[[nodiscard]] bool has_exception() const noexcept{
		return exception_ != nullptr;
	}

	[[nodiscard]] std::exception_ptr exception() const noexcept{
		return exception_;
	}

	void mark_finished() noexcept{
		if(runtime_state_ != nullptr){
			runtime_state_->active.store(false, std::memory_order_release);
		}
	}

	void process(scene& async_scene){
		elem_async_task_context context{owner_stop_token_, task_stop_token_, runtime_state_};
		if(context.stop_requested()){
			return;
		}
		try{
			this->process_impl(context, async_scene);
		}catch(...){
			exception_ = std::current_exception();
		}
	}

	virtual void on_done(elem& owner, scene& async_scene) = 0;

	virtual bool on_error(elem& owner, scene& async_scene, std::exception_ptr exception){
		(void)owner;
		(void)async_scene;
		(void)exception;
		return false;
	}
};

export
template <std::derived_from<elem> E, typename ProcessFn = void, typename DoneFn = void, typename ErrorFn = std::nullptr_t>
struct elem_async_task;

export
template <std::derived_from<elem> E, typename ProcessFn, typename DoneFn, typename ErrorFn>
struct elem_async_task : basic_elem_async_task{
	ADAPTED_NO_UNIQUE_ADDRESS ProcessFn process_fn_;
	ADAPTED_NO_UNIQUE_ADDRESS DoneFn done_fn_;
	ADAPTED_NO_UNIQUE_ADDRESS ErrorFn error_fn_{};

	template <std::invocable<elem_async_task_context&, scene&> ProcessFnTy, std::invocable<E&, scene&> DoneFnTy>
	[[nodiscard]] elem_async_task(ProcessFnTy&& process_fn, DoneFnTy&& done_fn)
		: process_fn_(std::forward<ProcessFnTy>(process_fn)),
		  done_fn_(std::forward<DoneFnTy>(done_fn)){
	}

	template <
		std::invocable<elem_async_task_context&, scene&> ProcessFnTy,
		std::invocable<E&, scene&> DoneFnTy,
		std::invocable<E&, scene&, std::exception_ptr> ErrorFnTy>
	[[nodiscard]] elem_async_task(ProcessFnTy&& process_fn, DoneFnTy&& done_fn, ErrorFnTy&& error_fn)
		: process_fn_(std::forward<ProcessFnTy>(process_fn)),
		  done_fn_(std::forward<DoneFnTy>(done_fn)),
		  error_fn_(std::forward<ErrorFnTy>(error_fn)){
	}

	void process_impl(elem_async_task_context& context, scene& async_scene) override{
		std::invoke(process_fn_, context, async_scene);
	}

	void on_done(elem& owner, scene& async_scene) override{
		std::invoke(done_fn_, static_cast<E&>(owner), async_scene);
	}

	bool on_error(elem& owner, scene& async_scene, std::exception_ptr exception) override{
		if constexpr (std::invocable<ErrorFn&, E&, scene&, std::exception_ptr>){
			std::invoke(error_fn_, static_cast<E&>(owner), async_scene, std::move(exception));
			return true;
		}else{
			(void)owner;
			(void)async_scene;
			(void)exception;
			return false;
		}
	}
};

template <std::derived_from<elem> E>
struct elem_async_task<E, void, void, std::nullptr_t> : basic_elem_async_task{
private:
	struct model_base{
		virtual ~model_base() = default;
		virtual void process(elem_async_task_context& context, scene& async_scene) = 0;
		virtual void done(E& owner, scene& async_scene) = 0;
		virtual bool error(E& owner, scene& async_scene, std::exception_ptr exception) = 0;
	};

	template <typename ProcessFn, typename DoneFn, typename ErrorFn>
	struct model : model_base{
		ADAPTED_NO_UNIQUE_ADDRESS ProcessFn process_fn_;
		ADAPTED_NO_UNIQUE_ADDRESS DoneFn done_fn_;
		ADAPTED_NO_UNIQUE_ADDRESS ErrorFn error_fn_{};

		[[nodiscard]] model(ProcessFn process_fn, DoneFn done_fn, ErrorFn error_fn)
			: process_fn_(std::move(process_fn)),
			  done_fn_(std::move(done_fn)),
			  error_fn_(std::move(error_fn)){
		}

		void process(elem_async_task_context& context, scene& async_scene) override{
			std::invoke(process_fn_, context, async_scene);
		}

		void done(E& owner, scene& async_scene) override{
			std::invoke(done_fn_, owner, async_scene);
		}

		bool error(E& owner, scene& async_scene, std::exception_ptr exception) override{
			if constexpr (std::invocable<ErrorFn&, E&, scene&, std::exception_ptr>){
				std::invoke(error_fn_, owner, async_scene, std::move(exception));
				return true;
			}else{
				(void)owner;
				(void)async_scene;
				(void)exception;
				return false;
			}
		}
	};

	std::unique_ptr<model_base> model_{};

public:
	template <std::invocable<elem_async_task_context&, scene&> ProcessFnTy, std::invocable<E&, scene&> DoneFnTy>
	[[nodiscard]] elem_async_task(ProcessFnTy&& process_fn, DoneFnTy&& done_fn)
		: model_(std::make_unique<model<std::decay_t<ProcessFnTy>, std::decay_t<DoneFnTy>, std::nullptr_t>>(
			std::forward<ProcessFnTy>(process_fn), std::forward<DoneFnTy>(done_fn), nullptr)){
	}

	template <
		std::invocable<elem_async_task_context&, scene&> ProcessFnTy,
		std::invocable<E&, scene&> DoneFnTy,
		std::invocable<E&, scene&, std::exception_ptr> ErrorFnTy>
	[[nodiscard]] elem_async_task(ProcessFnTy&& process_fn, DoneFnTy&& done_fn, ErrorFnTy&& error_fn)
		: model_(std::make_unique<model<std::decay_t<ProcessFnTy>, std::decay_t<DoneFnTy>, std::decay_t<ErrorFnTy>>>(
			std::forward<ProcessFnTy>(process_fn), std::forward<DoneFnTy>(done_fn), std::forward<ErrorFnTy>(error_fn))){
	}

	void process_impl(elem_async_task_context& context, scene& async_scene) override{
		model_->process(context, async_scene);
	}

	void on_done(elem& owner, scene& async_scene) override{
		model_->done(static_cast<E&>(owner), async_scene);
	}

	bool on_error(elem& owner, scene& async_scene, std::exception_ptr exception) override{
		return model_->error(static_cast<E&>(owner), async_scene, std::move(exception));
	}
};

export
template <std::derived_from<elem> E, typename ProcessFn = void, typename DoneFn = void, typename ErrorFn = std::nullptr_t>
struct elem_async_yield_task;

export
template <std::derived_from<elem> E, typename ProcessFn, typename DoneFn, typename ErrorFn>
struct elem_async_yield_task : basic_elem_async_task{
	using yield_result_t = std::invoke_result_t<ProcessFn&, elem_async_task_context&, scene&>;
	static_assert(!std::is_void_v<yield_result_t>);

	ADAPTED_NO_UNIQUE_ADDRESS ProcessFn process_fn_;
	ADAPTED_NO_UNIQUE_ADDRESS DoneFn done_fn_;
	ADAPTED_NO_UNIQUE_ADDRESS ErrorFn error_fn_{};
	std::optional<yield_result_t> result_{};

	template <std::invocable<elem_async_task_context&, scene&> ProcessFnTy, std::invocable<E&, scene&, yield_result_t&&> DoneFnTy>
	[[nodiscard]] elem_async_yield_task(ProcessFnTy&& process_fn, DoneFnTy&& done_fn)
		: process_fn_(std::forward<ProcessFnTy>(process_fn)),
		  done_fn_(std::forward<DoneFnTy>(done_fn)){
	}

	template <
		std::invocable<elem_async_task_context&, scene&> ProcessFnTy,
		std::invocable<E&, scene&, yield_result_t&&> DoneFnTy,
		std::invocable<E&, scene&, std::exception_ptr> ErrorFnTy>
	[[nodiscard]] elem_async_yield_task(ProcessFnTy&& process_fn, DoneFnTy&& done_fn, ErrorFnTy&& error_fn)
		: process_fn_(std::forward<ProcessFnTy>(process_fn)),
		  done_fn_(std::forward<DoneFnTy>(done_fn)),
		  error_fn_(std::forward<ErrorFnTy>(error_fn)){
	}

	void process_impl(elem_async_task_context& context, scene& async_scene) override{
		result_.emplace(std::invoke_r<yield_result_t>(process_fn_, context, async_scene));
	}

	void on_done(elem& owner, scene& async_scene) override{
		if(result_.has_value()){
			std::invoke(done_fn_, static_cast<E&>(owner), async_scene, std::move(*result_));
		}
	}

	bool on_error(elem& owner, scene& async_scene, std::exception_ptr exception) override{
		if constexpr (std::invocable<ErrorFn&, E&, scene&, std::exception_ptr>){
			std::invoke(error_fn_, static_cast<E&>(owner), async_scene, std::move(exception));
			return true;
		}else{
			(void)owner;
			(void)async_scene;
			(void)exception;
			return false;
		}
	}
};

template <std::derived_from<elem> E>
struct elem_async_yield_task<E, void, void, std::nullptr_t> : basic_elem_async_task{
private:
	struct model_base{
		virtual ~model_base() = default;
		virtual void process(elem_async_task_context& context, scene& async_scene) = 0;
		virtual void done(E& owner, scene& async_scene) = 0;
		virtual bool error(E& owner, scene& async_scene, std::exception_ptr exception) = 0;
	};

	template <typename ProcessFn, typename DoneFn, typename ErrorFn>
	struct model : model_base{
		using yield_result_t = std::invoke_result_t<ProcessFn&, elem_async_task_context&, scene&>;
		static_assert(!std::is_void_v<yield_result_t>);
		static_assert(!std::is_reference_v<yield_result_t>);

		ADAPTED_NO_UNIQUE_ADDRESS ProcessFn process_fn_;
		ADAPTED_NO_UNIQUE_ADDRESS DoneFn done_fn_;
		ADAPTED_NO_UNIQUE_ADDRESS ErrorFn error_fn_{};
		std::optional<yield_result_t> result_{};

		[[nodiscard]] model(ProcessFn process_fn, DoneFn done_fn, ErrorFn error_fn)
			: process_fn_(std::move(process_fn)),
			  done_fn_(std::move(done_fn)),
			  error_fn_(std::move(error_fn)){
		}

		void process(elem_async_task_context& context, scene& async_scene) override{
			result_.emplace(std::invoke_r<yield_result_t>(process_fn_, context, async_scene));
		}

		void done(E& owner, scene& async_scene) override{
			if(result_.has_value()){
				std::invoke(done_fn_, owner, async_scene, std::move(*result_));
			}
		}

		bool error(E& owner, scene& async_scene, std::exception_ptr exception) override{
			if constexpr (std::invocable<ErrorFn&, E&, scene&, std::exception_ptr>){
				std::invoke(error_fn_, owner, async_scene, std::move(exception));
				return true;
			}else{
				(void)owner;
				(void)async_scene;
				(void)exception;
				return false;
			}
		}
	};

	std::unique_ptr<model_base> model_{};

public:
	template <typename ProcessFnTy, typename DoneFnTy>
		requires std::invocable<ProcessFnTy&, elem_async_task_context&, scene&>
	[[nodiscard]] elem_async_yield_task(ProcessFnTy&& process_fn, DoneFnTy&& done_fn)
		: model_(std::make_unique<model<std::decay_t<ProcessFnTy>, std::decay_t<DoneFnTy>, std::nullptr_t>>(
			std::forward<ProcessFnTy>(process_fn), std::forward<DoneFnTy>(done_fn), nullptr)){
	}

	template <typename ProcessFnTy, typename DoneFnTy, typename ErrorFnTy>
		requires std::invocable<ProcessFnTy&, elem_async_task_context&, scene&>
	[[nodiscard]] elem_async_yield_task(ProcessFnTy&& process_fn, DoneFnTy&& done_fn, ErrorFnTy&& error_fn)
		: model_(std::make_unique<model<std::decay_t<ProcessFnTy>, std::decay_t<DoneFnTy>, std::decay_t<ErrorFnTy>>>(
			std::forward<ProcessFnTy>(process_fn), std::forward<DoneFnTy>(done_fn), std::forward<ErrorFnTy>(error_fn))){
	}

	void process_impl(elem_async_task_context& context, scene& async_scene) override{
		model_->process(context, async_scene);
	}

	void on_done(elem& owner, scene& async_scene) override{
		model_->done(static_cast<E&>(owner), async_scene);
	}

	bool on_error(elem& owner, scene& async_scene, std::exception_ptr exception) override{
		return model_->error(static_cast<E&>(owner), async_scene, std::move(exception));
	}
};

}
