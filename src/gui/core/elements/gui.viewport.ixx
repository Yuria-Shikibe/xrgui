//
// Created by Matrix on 2025/5/26.
//

export module mo_yanxi.gui.viewport;

export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.graphic.camera;

import std;

namespace mo_yanxi::gui{

	export constexpr float viewport_default_radius = 5000;

	export
	struct viewport : elem{
	private:
		math::vec2 last_camera_center_pos_{};

	protected:
		graphic::camera2 camera{};
		math::vec2 viewport_clamp_region{viewport_default_radius * 2, viewport_default_radius * 2};

	public:
		viewport(scene& scene, elem* parent)
			: elem(scene, parent){

			this->interactivity = interactivity_flag::enabled;
			camera.speed_scale = 0;
		}

	protected:
		bool update(const float delta_in_ticks) override{
			if(!elem::update(delta_in_ticks)) return false;
			const auto [w, h]{viewport_clamp_region - camera.get_viewport().extent()};
			camera.clamp_position({math::vec2{}, w, h});
			camera.update(delta_in_ticks);
			return true;
		}

		bool resize_impl(const math::vec2 size) override{
			if(elem::resize_impl(size)){
				auto [x, y] = this->content_extent();
				camera.resize_screen(x, y);
				return true;
			}
			return false;
		}

		void on_focus_changed(bool is_focused) override{
			// this->get_scene().set_camera_focus(is_focused ? &camera : nullptr);
			this->set_focused_scroll(is_focused);
			this->set_focused_key(is_focused);
		}

		events::op_afterwards on_scroll(const events::scroll event, std::span<elem* const> aboves) override{
			camera.set_scale_by_delta(event.delta.y * 0.05f);
			return events::op_afterwards::intercepted;
		}

		events::op_afterwards on_drag(const events::drag e) override{
			if(e.key.as_mouse() == input_handle::mouse::CMB){
				auto src = get_transferred_pos(e.src);
				auto dst = get_transferred_pos(e.dst);
				camera.set_center(last_camera_center_pos_ - (dst - src));
			}
			return events::op_afterwards::intercepted;
		}

		events::op_afterwards on_click(const events::click e, std::span<elem* const> aboves) override{
			if(e.key.as_mouse() == input_handle::mouse::CMB){
				last_camera_center_pos_ = camera.get_stable_center();
			}
			return events::op_afterwards::intercepted;
		}

		void viewport_begin() const {
			const auto proj = camera.get_world_to_clip();
			auto& r = renderer();

			r.push_viewport(this->content_bound_abs());
			r.push_scissor({this->content_bound_abs()});
			r.top_viewport().push_local_transform(proj);
		}

		void viewport_end() const {
			auto& r = renderer();

			r.top_viewport().pop_local_transform();
			r.pop_scissor();
			r.pop_viewport();
		}

		[[nodiscard]] math::vec2 get_transferred_pos(const math::vec2 content_local_pos) const noexcept{
			return camera.get_screen_to_world(content_local_pos, {});
		}

		[[nodiscard]] math::vec2 get_transferred_cursor_pos() const noexcept{
			return viewport::get_transferred_pos(this->get_scene().get_cursor_pos());
		}

	};
}
