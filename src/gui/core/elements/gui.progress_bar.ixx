//
// Created by Matrix on 2024/12/7.
//

export module mo_yanxi.gui.elem.progress_bar;

export import mo_yanxi.gui.infrastructure;
import mo_yanxi.graphic.color;
import mo_yanxi.graphic.gradient;
import mo_yanxi.math;

import std;

namespace mo_yanxi::gui{

export enum struct progress_state{
	exact,
	rough,
	staging,
	approach_linear,
	approach_scaled,

};

struct bar_progress{
	using value_type = float;

private:
	progress_state state{};
	value_type target{};
	value_type current{};

	float speed_scale{1};
public:
	constexpr void set_speed(value_type speed) noexcept{
		//TODO check NAN?
		speed_scale = speed;
	}

	constexpr void set_target(value_type target) noexcept{
		this->target = target;
	}

	constexpr void set_state(progress_state new_state) noexcept{
		if(new_state == progress_state::rough){
			current = {};
		}
	}

	constexpr void set_to_target() noexcept{
		current = target;
	}

	[[nodiscard]] constexpr progress_state get_state() const noexcept{
		return state;
	}

	[[nodiscard]] constexpr value_type get_target() const noexcept{
		return target;
	}

	[[nodiscard]] constexpr value_type get_current() const noexcept{
		return current;
	}

	constexpr void update(float delta_in_tick) noexcept{
		switch(state){
		case progress_state::staging: break;
		case progress_state::rough: current += delta_in_tick * speed_scale; break;
		case progress_state::exact: current = target; break;
		case progress_state::approach_linear: math::approach_inplace(current, target, delta_in_tick * speed_scale); break;
		case progress_state::approach_scaled:{
			auto dlt = target - current;
			if(math::abs(dlt) < std::numeric_limits<float>::epsilon()){
				current = target;
			}else{
				current = math::lerp(current, target, math::clamp(delta_in_tick * speed_scale));
			}
			break;
		}
		default: std::unreachable();
		}
	}
};

export
using progress_slot_1d = bar_progress;

export
struct progress_draw_config{
	graphic::color_gradient color{graphic::colors::dark_gray, graphic::colors::light_gray};
};

export struct progress_bar;

namespace style{

struct progress_drawer : style_drawer<progress_bar>{
	using style_drawer::style_drawer;
};

export
struct default_progress_drawer : progress_drawer{
	void draw(const progress_bar& element, math::frect region, float opacityScl) const override;
};

export inline constexpr default_progress_drawer default_progress_drawer;
export inline const progress_drawer* global_default_progress_drawer{};

export inline const progress_drawer* get_global_default_progress_drawer() noexcept{
	return global_default_progress_drawer == nullptr ? &default_progress_drawer : global_default_progress_drawer;
}

}

struct progress_bar_terminal final : react_flow::terminal<float>{
	progress_bar* target;

	[[nodiscard]] explicit progress_bar_terminal(progress_bar& target)	: target(std::addressof(target)){
	}

protected:
	void on_update(const float& data) override;
};

struct progress_bar : elem{
protected:
	const style::progress_drawer* drawer{style::get_global_default_progress_drawer()};
	progress_bar_terminal* notifier_{};

public:
	bar_progress progress{};
	progress_draw_config draw_config{};

	[[nodiscard]] progress_bar(scene& scene, elem* parent)
	: elem(scene, parent){
	}

	progress_bar_terminal& request_receiver(){
		if(notifier_){
			return *notifier_;
		}
		auto& node = get_scene().request_react_node<progress_bar_terminal>(*this);
		this->notifier_ = &node;
		return node;
	}

protected:
	void draw_content(const rect clipSpace) const override{
		draw_background();
		if(drawer){
			drawer->draw(*this, content_bound_abs(), get_draw_opacity());
		}
	}

public:
	bool update(float delta_in_ticks) override{
		if(elem::update(delta_in_ticks)){
			progress.update(delta_in_ticks);
			return true;
		}
		return false;
	}
};

void progress_bar_terminal::on_update(const float& data){
	target->progress.set_target(data);
}


}
