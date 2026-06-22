module;

export module mo_yanxi.gui.infrastructure:async_task;

import std;
import mo_yanxi.gui.util.task_queue;
import :elem_ptr;

namespace mo_yanxi::gui{
export
struct async_task_context{
private:
	std::stop_token owner_stop_token_{};
	std::stop_token task_stop_token_{};
	async_operation_state* runtime_state_{};

public:
	[[nodiscard]] async_task_context() = default;

	[[nodiscard]] async_task_context(
		std::stop_token owner_stop_token,
		std::stop_token task_stop_token,
		async_operation_state* runtime_state) noexcept
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
		runtime_state_->report_progress(current, total);
	}
};

struct forked_scene_task_base{
private:
	elem_ref<> owner_ref_{};
	std::stop_token owner_stop_token_{};
	std::stop_token task_stop_token_{};
	async_operation_state_ptr runtime_state_{};
	std::exception_ptr exception_{};

protected:
	virtual void process_impl(async_task_context& context, scene& async_scene) = 0;

public:
	virtual ~forked_scene_task_base() = default;

	[[nodiscard]] forked_scene_task_base() = default;

	void bind_async_owner(
		elem_ref<> owner_ref,
		std::stop_token owner_stop_token,
		async_operation_state_ptr runtime_state) noexcept{
		owner_ref_ = std::move(owner_ref);
		owner_stop_token_ = std::move(owner_stop_token);
		task_stop_token_ = runtime_state ? runtime_state->stop_token() : std::stop_token{};
		runtime_state_ = std::move(runtime_state);
	}

	[[nodiscard]] elem* owner() const noexcept{
		return owner_ref_.get_live();
	}

	[[nodiscard]] bool owned_by(const elem* owner) const noexcept{
		return owner_ref_.get_retained() == owner;
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
		if(runtime_state_){
			if(this->stop_requested()){
				runtime_state_->mark_cancelled();
			}else if(this->has_exception()){
				runtime_state_->mark_failed();
			}else{
				runtime_state_->mark_completed();
			}
		}
	}

	void mark_cancelled() noexcept{
		if(runtime_state_){
			runtime_state_->mark_cancelled();
		}
	}

	void process(scene& async_scene){
		async_task_context context{owner_stop_token_, task_stop_token_, runtime_state_.get()};
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

}
