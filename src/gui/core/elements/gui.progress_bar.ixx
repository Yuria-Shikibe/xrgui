//
// Created by Matrix on 2024/12/7.
//

export module mo_yanxi.gui.elem.progress_bar;

export import mo_yanxi.gui.infrastructure;
import mo_yanxi.graphic.color;
import mo_yanxi.graphic.gradient;
import mo_yanxi.math;
import mo_yanxi.math.constrained_system;

import std;

namespace mo_yanxi::gui{
export enum struct progress_state{
    exact,
    rough,
    staging,
    approach_linear,
    approach_scaled,
    approach_smooth,
    approach_smooth_lerp // 可选：新增基于 lerp 的极致平滑状态
};

struct bar_progress{
    using value_type = float;

private:
    progress_state state{};
    value_type target{};
    value_type current{};

    float speed_scale{1};
    float current_speed_{};

public:
    constexpr void set_speed(value_type speed) noexcept{
       speed_scale = math::max(speed, 0.0f); // 防御性编程：避免负数缩放导致物理系统崩溃
    }

    constexpr bool set_target(value_type target) noexcept{
       return util::try_modify(this->target, target);
    }


    constexpr bool set_state(progress_state new_state) noexcept{
       auto rst = util::try_modify(state, new_state);
       if(new_state == progress_state::approach_smooth ||
          new_state == progress_state::approach_smooth_lerp){
           current_speed_ = 0;
       }
       if(new_state == progress_state::rough){
          current = {};
       }
       return rst;
    }

    constexpr void set_to_target() noexcept{
       current = target;
       current_speed_ = 0; // 重置目标时，一并消除动能
    }

    [[nodiscard]] constexpr progress_state get_state() const noexcept{ return state; }
    [[nodiscard]] constexpr value_type get_target() const noexcept{ return target; }
    [[nodiscard]] constexpr value_type get_current() const noexcept{ return current; }

    constexpr bool update(float delta_in_tick) noexcept{
       switch(state){
       case progress_state::staging: return true;

       case progress_state::rough:
           current += delta_in_tick * speed_scale;
           return false;

       case progress_state::exact:
           current = target;
           return true;

       case progress_state::approach_linear:
          math::approach_inplace(current, target, delta_in_tick * speed_scale);
          return current == target;

       case progress_state::approach_scaled:{
          auto dlt = target - current;
          if(math::zero(dlt, std::numeric_limits<float>::epsilon() * 1024)){
             current = target;
             return true;
          }
          current = math::lerp(current, target, math::clamp(delta_in_tick * speed_scale));
          return false;
       }

       case progress_state::approach_smooth:{
          const auto dist = target - current;

          // 提前终止条件：如果距离足够近，且速度几乎为零，则直接吸附
          if(math::zero(dist, 1e-4f) && math::zero(current_speed_, 1e-3f)){
              current = target;
              current_speed_ = 0.0f;
              return true;
          }

          // 获取系统约束下的最优加速度
          auto accel = math::constrain_resolve::smooth_approach(dist, current_speed_, speed_scale);

          // 半隐式欧拉积分：先更新速度
          current_speed_ += accel * delta_in_tick;
          const auto delta_pos = current_speed_ * delta_in_tick;

          // 防超调 (Anti-overshoot) 逻辑：如果这一步会跨越目标，强制截断并停滞
          if ((dist > 0 && delta_pos >= dist) || (dist < 0 && delta_pos <= dist)) {
              current = target;
              current_speed_ = 0.0f;
          } else {
              current += delta_pos;
          }

          return current == target;
       }

       case progress_state::approach_smooth_lerp:{
          // 利用 constrain_resolve 中的 smooth_approach_lerp 提供另一种阻尼感更强的平滑
          const auto dist = target - current;

          if(math::zero(dist, 1e-4f) && math::zero(current_speed_, 1e-3f)){
              current = target;
              current_speed_ = 0.0f;
              return true;
          }

          auto accel = math::constrain_resolve::smooth_approach_lerp(dist, current_speed_, speed_scale, 0.01f);

          current_speed_ += accel * delta_in_tick;
          const auto delta_pos = current_speed_ * delta_in_tick;

          if ((dist > 0 && delta_pos >= dist) || (dist < 0 && delta_pos <= dist)) {
              current = target;
              current_speed_ = 0.0f;
          } else {
              current += delta_pos;
          }

          return current == target;
       }

       default: std::unreachable();
       }
       return true;
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

export
struct progress_drawer : style_drawer<progress_bar>{
	using style_drawer::style_drawer;
};

export
struct progress_drawer_flat : progress_drawer{
	using progress_drawer::progress_drawer;

protected:
	void draw_layer_impl(const progress_bar& element, math::frect region, float opacityScl, fx::layer_param layer_param) const override;
};

export
struct progress_drawer_arc : progress_drawer{
	math::based_section<float> radius{0, 1};
	math::based_section<float> angle_range{0, 1};
	align::pos align = align::pos::center;

	using progress_drawer::progress_drawer;

protected:
	void draw_layer_impl(const progress_bar& element, math::frect region, float opacityScl, fx::layer_param layer_param) const override;
};

export inline constexpr progress_drawer_flat default_progress_drawer{tags::persistent, layer_top_only};
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
	void on_update(react_flow::data_carrier<float>& data) override;
};

struct progress_bar : elem{
protected:
	progress_bar_terminal* notifier_{};

public:
	referenced_ptr<const style::progress_drawer> drawer{style::get_global_default_progress_drawer()};
	/**
	 * @brief Directly access to progress does not maintains the invariant of update.
	 */
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

public:
	void set_progress_target(bar_progress::value_type value){
		if(progress.set_target(value)){
			set_update_required(update_channel::value_approach);
		}
	}

	void set_progress_state(progress_state state){
		if(progress.set_state(state)){
			set_update_required(update_channel::value_approach);
		}
	}

	void draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const override{
		draw_style(param);
		if(drawer){
			drawer->draw_layer(*this, content_bound_abs(), get_draw_opacity(), param);
		}
	}

	bool update(float delta_in_ticks) override{
		if(elem::update(delta_in_ticks)){
			if(progress.update(delta_in_ticks)){
				set_update_disabled(update_channel::value_approach);
			}
			return true;
		}
		return false;
	}
};

void progress_bar_terminal::on_update(react_flow::data_carrier<float>& data){
	target->set_progress_target(data.get());
}


}
