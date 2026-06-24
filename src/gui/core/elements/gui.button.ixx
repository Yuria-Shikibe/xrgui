export module mo_yanxi.gui.elem.button;

export import mo_yanxi.gui.infrastructure;
import std;

// MSVC module builds have been unreliable with std function wrappers here.

template <std::size_t sso_size, typename signature>
class fixed_function;

template <std::size_t sso_size, typename r, typename... args>
class fixed_function<sso_size, r(args...)> {
	struct callable_base{
		virtual ~callable_base() = default;
		virtual r invoke(args... arguments) = 0;
	};

	template <typename t>
	struct callable final : callable_base{
		t functor;

		explicit callable(t fn)
			: functor(std::move(fn)){
		}

		r invoke(args... arguments) override{
			if constexpr (std::is_void_v<r>){
				std::invoke(functor, std::forward<args>(arguments)...);
			}else{
				return std::invoke(functor, std::forward<args>(arguments)...);
			}
		}
	};

	std::unique_ptr<callable_base> callable_{};

	template <typename f>
	void construct_from(f&& functor){
		using decayed = std::remove_cvref_t<f>;
		static_assert(std::invocable<decayed&, args...>);
		if constexpr (!std::is_void_v<r>){
			static_assert(std::convertible_to<std::invoke_result_t<decayed&, args...>, r>);
		}
		callable_ = std::make_unique<callable<decayed>>(decayed(std::forward<f>(functor)));
	}

public:
	fixed_function() noexcept = default;
	~fixed_function() = default;

	template <typename f>
		requires (!std::same_as<std::remove_cvref_t<f>, fixed_function>)
	fixed_function(f&& functor){
		construct_from(std::forward<f>(functor));
	}

	fixed_function(const fixed_function&) = delete;
	fixed_function& operator=(const fixed_function&) = delete;
	fixed_function(fixed_function&& other) noexcept = default;
	fixed_function& operator=(fixed_function&& other) noexcept = default;

	template <typename f>
		requires (!std::same_as<std::remove_cvref_t<f>, fixed_function>)
	fixed_function& operator=(f&& functor){
		construct_from(std::forward<f>(functor));
		return *this;
	}

	r operator()(args... arguments){
		if(callable_ == nullptr){
			throw std::bad_function_call();
		}
		if constexpr (std::is_void_v<r>){
			callable_->invoke(std::forward<args>(arguments)...);
		}else{
			return callable_->invoke(std::forward<args>(arguments)...);
		}
	}

	explicit operator bool() const noexcept{
		return callable_ != nullptr;
	}

	void reset() noexcept{
		callable_.reset();
	}
};

namespace mo_yanxi::gui{
export
template <std::derived_from<elem> T = elem>
struct button : public T{
	using base_type = T;

protected:
	using callback_type = fixed_function<32, void(const events::pointer_button_event&, elem&)>;

	callback_type callback{};

	void add_button_prop(){
		elem::interactivity = interactivity_flag::enabled;
		elem::extend_focus_until_mouse_drop = true;
	}

public:
	template <typename... Args>
		requires (std::constructible_from<base_type, scene&, elem*, Args&&...>)
	[[nodiscard]] button(scene& scene, elem* parent, Args&&... args)
		: base_type(scene, parent, std::forward<Args>(args)...){
		add_button_prop();
	}

	void on_pointer_button(events::event_context& ctx, const events::pointer_button_event& event) override{
		base_type::on_pointer_button(ctx, event);
		if(!ctx.is_target_or_bubble_phase()) return;
		if(this->is_disabled()){
			ctx.consume(*this);
			return;
		}
		if(event.within_elem(ctx, *this)){
			if(callback) callback(event, *this);
		}
		ctx.consume(*this);
	}

	void set_button_callback(callback_type&& func){
		callback = std::move(func);
	}

	template <std::invocable<> Func>
	void set_button_callback(Func&& fn){
		callback = [func = std::forward<Func>(fn)](const events::pointer_button_event& e, elem&){
			if(e.key.on_release()){
				std::invoke(func);
			}
		};
	}

	template <std::invocable<button&> Func>
	void set_button_callback(Func&& fn){
		callback = [func = std::forward<Func>(fn)](const events::pointer_button_event& e, elem& element){
			if(e.key.on_release()){
				auto& b = static_cast<button&>(element);
				std::invoke(func, b);
			}
		};
	}
};
}
