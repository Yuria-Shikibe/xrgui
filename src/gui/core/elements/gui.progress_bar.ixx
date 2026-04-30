//

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
    approach_smooth_lerp 
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
       speed_scale = math::max(speed, 0.0f); 
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
       current_speed_ = 0; 
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

          
          if(math::zero(dist, 1e-4f) && math::zero(current_speed_, 1e-3f)){
              current = target;
              current_speed_ = 0.0f;
              return true;
          }

          
          auto accel = math::constrain_resolve::smooth_approach(dist, current_speed_, speed_scale);

          
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

       case progress_state::approach_smooth_lerp:{
          
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
struct progress_drawer_flat{
	using target_type = progress_bar;

	void operator()(const typed_draw_param<progress_bar>& p) const;
};

export
struct progress_drawer_arc{
	using target_type = progress_bar;

	math::based_section<float> radius{0, 1};
	math::based_section<float> angle_range{0, 1};
	align::pos align = align::pos::center;

	void operator()(const typed_draw_param<progress_bar>& p) const;
};

export inline auto make_default_progress_style(){
	return make_tree_node_ptr(tree_leaf{progress_drawer_flat{}});
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
private:
	style::target_known_node_ptr<progress_bar> content_style_{init_content_style_()};

	style::target_known_node_ptr<progress_bar> init_content_style_();

protected:
	progress_bar_terminal* notifier_{};

public:
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
	using elem::set_style;

	[[nodiscard]] const style::target_known_node_ptr<progress_bar>& get_content_style() const noexcept{
		return content_style_;
	}

	void set_style(style::target_known_node_ptr<progress_bar> style){
		if(util::try_modify(this->content_style_, std::move(style))){
			get_scene().notify_display_state_changed(get_channel());
		}
	}

	void set_progress_target(bar_progress::value_type value){
		if(progress.set_target(value)){
			util::update_insert(*this, update_channel::value_approach);
		}
	}

	void set_progress_state(progress_state state){
		if(progress.set_state(state)){
			util::update_insert(*this, update_channel::value_approach);
		}
	}

	void record_draw_layer(draw_recorder& call_stack_builder) const override{
		elem::record_draw_layer(call_stack_builder);
		if(style::present(content_style_))record_content_drawer_draw_context(call_stack_builder, [](const progress_bar& s, draw_recorder& r){
			style::draw_record(s.content_style_, r);
		});
	}

	bool update(float delta_in_ticks) override{
		if(elem::update(delta_in_ticks)){
			if(progress.update(delta_in_ticks)){
				util::update_erase(*this, update_channel::value_approach);
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
