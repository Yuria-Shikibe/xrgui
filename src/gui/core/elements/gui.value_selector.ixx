//

//

export module mo_yanxi.gui.elem.value_selector;

import std;
import mo_yanxi.gui.infrastructure;
import mo_yanxi.cond_exist;

namespace mo_yanxi::gui{

export
template <std::derived_from<elem> T = elem, unsigned MaxSize = 0 /*0 to specify dynamic*/>
struct dispersed_value_selector : public T{
	using base_type = T;

	template <typename ...Args>
		requires std::constructible_from<base_type, scene&, elem*, Args&&...>
	[[nodiscard]] dispersed_value_selector(scene& scene, elem* parent, Args&& ...args)
		: base_type(scene, parent, std::forward<Args>(args)...){
		this->interactivity = interactivity_flag::enabled;
	}

private:
	static constexpr bool isDynamic = MaxSize == 0;
	cond_exist<unsigned, isDynamic> max_val_{2};
	unsigned cur_val_{0};

protected:
	virtual void on_selected_val_updated(unsigned value){

	}

public:
	constexpr unsigned get_selected_value_sentinel() const noexcept{
		if constexpr (isDynamic){
			return max_val_;
		}else{
			return MaxSize;
		}
	}

	void incr_selected_value(){
		cur_val_++;
		if(cur_val_ >= get_selected_value_sentinel()){
			cur_val_ = 0;
		}
		on_selected_val_updated(cur_val_);
	}

	void decr_selected_value(){
		if(cur_val_ == 0){
			cur_val_ = get_selected_value_sentinel();
		}
		cur_val_--;
		on_selected_val_updated(cur_val_);
	}

	void set_sentinel(unsigned val) requires(isDynamic) {
		if(val == 0)throw std::invalid_argument("value must > 0");
		max_val_ = val;
		if(cur_val_ >= max_val_){
			cur_val_ = max_val_ - 1;
			on_selected_val_updated(cur_val_);
		}
	}

	void set_current_value(unsigned val){
		const auto last = cur_val_;
		cur_val_ = val;
		if(cur_val_ >= get_selected_value_sentinel()){
			cur_val_ = get_selected_value_sentinel() - 1;
		}
		if(last != cur_val_)on_selected_val_updated(cur_val_);
	}

	unsigned get_current_value() const noexcept{
		return cur_val_;
	}

	void on_pointer_button(events::event_context& ctx, const events::pointer_button_event& event) override{
		base_type::on_pointer_button(ctx, event);
		if(!ctx.is_target_or_bubble_phase()) return;
		if(!this->is_disabled() && event.key.on_release() && event.within_elem(ctx, *this)){
			if(event.key.as_mouse() == input_handle::mouse::LMB){
				incr_selected_value();
			}
			if(event.key.as_mouse() == input_handle::mouse::RMB){
				decr_selected_value();
			}
		}
		ctx.consume(*this);
	}

	void on_wheel(events::event_context& ctx, const events::wheel_event& event) override{
		if(!ctx.is_target_or_bubble_phase()) return;
		if(event.delta.y > 0)incr_selected_value();
		else if(event.delta.y < 0) decr_selected_value();

		ctx.consume(*this);
	}
};

export
template <typename T>
using binary_value_selector = dispersed_value_selector<T, 2>;

}
