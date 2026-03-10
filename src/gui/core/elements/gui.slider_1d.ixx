export module mo_yanxi.gui.elem.slider_1d;

export import mo_yanxi.gui.elem.slider_base;
import mo_yanxi.snap_shot;
import mo_yanxi.math;
import std;

namespace mo_yanxi::gui{

export
struct slider_1d;

namespace style{
export
struct slider_1d_drawer : style_drawer<slider_1d>{
	using style_drawer::style_drawer;
};

export
struct
default_slider_1d_drawer : slider_1d_drawer{
	[[nodiscard]] constexpr default_slider_1d_drawer()
	: slider_1d_drawer(tags::persistent, layer_top_only){
	}

	void draw_layer_impl(
		const slider_1d& element,
		math::frect region,
		float opacityScl,
		fx::layer_param layer_param) const override;

	void draw(const slider_1d& element, math::frect region, float opacityScl) const;
};

export inline constexpr default_slider_1d_drawer default_slider_1d_drawer_instance;

export inline const slider_1d_drawer* global_default_slider_1d_drawer{};

export inline const slider_1d_drawer* get_global_default_slider_1d_drawer() noexcept{
	return global_default_slider_1d_drawer == nullptr ? &default_slider_1d_drawer_instance : global_default_slider_1d_drawer;
}

}

export
struct slider_1d : slider_base<slider_1d, slider1d_slot, float, math::vec2, std::optional<float>>{
protected:
	referenced_ptr<const style::slider_1d_drawer> drawer_{get_scene().style_manager.get_default<style::slider_1d_drawer>()};

public:
	bool is_vertical = false;

	[[nodiscard]] slider_1d(scene& scene, elem* parent)
	: slider_base(scene, parent){
		scroll_sensitivity = 1 / 25.0f;
		sensitivity = 1.0f;
		bar_handle_extent = {10.0f, 10.0f};
		drag_src_ = std::nullopt;
	}

	[[nodiscard]] const style::slider_1d_drawer* get_drawer() const noexcept{
		return drawer_.get();
	}

	void set_drawer(const referenced_ptr<const style::slider_1d_drawer>& drawer){
		drawer_ = drawer;
		drawer->apply_to(*this);
	}

	void set_drawer(const style::slider_1d_drawer& drawer){
		set_drawer(&drawer);
	}

	void set_clamp_from_layout_policy(layout::layout_policy policy, float target_sensitivity = 1.0f){
		sensitivity = target_sensitivity;
		switch(policy){
		case layout::layout_policy::none: break;
		case layout::layout_policy::vert_major: is_vertical = true; break;
		case layout::layout_policy::hori_major: is_vertical = false; break;
		default: std::unreachable();
		}
	}

	[[nodiscard]] math::vec2 get_bar_handle_movable_extent() const noexcept{
		return content_extent().fdim(get_bar_handle_extent());
	}

	[[nodiscard]] constexpr math::vec2 get_bar_handle_extent() const noexcept{
		return get_bar_handle_extent(content_extent());
	}

	[[nodiscard]] constexpr math::vec2 get_bar_handle_extent(math::vec2 content_extent) const noexcept{
		return {
				is_vertical ? content_extent.x : bar_handle_extent.x,
				!is_vertical ? content_extent.y : bar_handle_extent.y,
			};
	}

protected:
	void draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const override;

public:
	events::op_afterwards on_scroll(const events::scroll event, std::span<elem* const> aboves) override{
		float move = is_vertical ? event.delta.y : event.delta.x;

		if(input_handle::matched(event.mode, input_handle::mode::shift)){
			move = is_vertical ? event.delta.x : event.delta.y;
		}

		const auto mov = -scroll_sensitivity * sensitivity * move;
		bar.move_minimum_delta(mov, smooth_scroll_ ? &snap_shot<float>::temp : &snap_shot<float>::base);

		if(!smooth_scroll_) check_apply();
		else update_approach_state();

		return events::op_afterwards::intercepted;
	}

	events::op_afterwards on_drag(const events::drag event) override{
		if(event.key.mode_bits == input_handle::mode::ctrl)return events::op_afterwards::fall_through;

		if(!drag_src_)return events::op_afterwards::intercepted;

		if(event.key.as_mouse() == input_handle::mouse::LMB){
            float delta = is_vertical ? event.delta().y : event.delta().x;
            float ce = is_vertical ? content_extent().y : content_extent().x;
			bar.move_progress(delta * sensitivity / ce, drag_src_.value());
		}else if(event.key.as_mouse() == input_handle::mouse::RMB){
			bar.resume();
		}

		update_approach_state();

		return events::op_afterwards::intercepted;
	}

	events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override{
		elem::on_click(event, aboves);
		const auto [key, action, mode] = event.key;

		switch(static_cast<input_handle::mouse>(key)){
		case input_handle::mouse::_1 :{
			switch(action){
			case input_handle::act::press :{
				drag_src_ = bar.get_temp_progress();

				if(mode == input_handle::mode::ctrl){
					float pos = is_vertical ? event.get_content_pos(*this).y : event.get_content_pos(*this).x;
					float ce = is_vertical ? content_extent().y : content_extent().x;
					float sign = sensitivity > 0 ? 1.0f : (sensitivity < 0 ? -1.0f : 0.0f);
					const auto move = (pos - get_progress() * ce) / ce;
					bar.move_progress(move * sign);

					if(!smooth_jump_){
						check_apply();
					} else{
						update_approach_state();
					}
				}
				break;
			}

			case input_handle::act::release :{
				drag_src_.reset();
				if(!smooth_drag_) check_apply();
				else update_approach_state();
				break;
			}

			default : break;
			}
			break;
		}
		case input_handle::mouse::_2 :{
			drag_src_.reset();
			bar.resume();
			break;
		}
		default: break;
		}

		return events::op_afterwards::intercepted;
	}
};

}
