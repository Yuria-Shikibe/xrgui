//
// Created by Matrix on 2024/9/24.
//

export module mo_yanxi.gui.elem.button;

export import mo_yanxi.gui.infrastructure;
import std;

namespace mo_yanxi::gui{
export
template <std::derived_from<elem> T = elem>
struct button : public T{
	using base_type = T;

protected:
	using callback_type = std::move_only_function<void(events::click, button<T>&)>;

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

	events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override{
		base_type::on_click(event, aboves);
		if(this->disabled) return events::op_afterwards::intercepted;
		if(callback && event.within_elem(*this)) callback(event, *this);
		return events::op_afterwards::intercepted;
	}

	void set_button_callback(callback_type&& func){
		callback = std::move(func);
	}

	template <std::invocable<> Func>
	void set_button_callback(Func&& fn){
		callback = [func = std::forward<Func>(fn)](events::click e, button&){
			if(e.key.on_release()){
				std::invoke(func);
			}
		};
	}

	template <std::invocable<button&> Func>
	void set_button_callback(Func&& fn){
		callback = [func = std::forward<Func>(fn)](events::click e, button& b){
			if(e.key.on_release()){
				std::invoke(func, b);
			}
		};
	}
};
}
