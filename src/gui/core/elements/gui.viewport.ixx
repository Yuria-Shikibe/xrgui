//

//

export module mo_yanxi.gui.elem.viewport;

export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.graphic.camera2;

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
		inline viewport(scene& scene, elem* parent)
			: elem(scene, parent){
			this->interactivity = interactivity_flag::enabled;
			extend_focus_until_mouse_drop = true;
			camera.speed_scale = 0;
		}


		inline void on_display_state_changed(bool is_shown, bool is_scene_notified) override{
			elem::on_display_state_changed(is_shown, is_scene_notified);
			if(is_shown){
				util::update_insert(*this, update_channel::draw);
			}else{
				util::update_erase(*this, update_channel::draw);
			}
		}

		inline bool update(const float delta_in_ticks) override{
			if(!elem::update(delta_in_ticks)) return false;
			const auto [w, h] = viewport_clamp_region.copy().fdim(camera.get_viewport().extent());

			camera.clamp_position({math::vec2{}, w, h});
			camera.update(delta_in_ticks);
			return true;
		}

	protected:
		inline bool resize_impl(const math::vec2 size) override{
			if(elem::resize_impl(size)){
				auto [x, y] = this->content_extent();
				camera.resize_screen(x, y);
				return true;
			}
			return false;
		}
	public:

		inline void on_focus_changed(bool is_focused) override{

			this->set_focused_scroll(is_focused);
			this->set_focused_key(is_focused);
		}

		inline void on_wheel(events::event_context& ctx, const events::wheel_event& event) override{
			if(!ctx.is_target_or_bubble_phase()) return;
			camera.set_scale_by_delta(event.delta.y * 0.05f);
			ctx.consume(*this);
		}

		inline void on_pointer_drag(events::event_context& ctx, const events::pointer_drag_event& e) override{
			if(!ctx.is_target_or_bubble_phase()) return;
			if(e.key.as_mouse() == input_handle::mouse::CMB){
				auto src = get_transferred_pos(e.local_src);
				auto dst = get_transferred_pos(e.local_dst);
				camera.set_center(last_camera_center_pos_ - (dst - src));
			}
			ctx.consume(*this);
		}

		inline void on_pointer_button(events::event_context& ctx, const events::pointer_button_event& e) override{
			elem::on_pointer_button(ctx, e);
			if(!ctx.is_target_or_bubble_phase()) return;
			if(e.key.as_mouse() == input_handle::mouse::CMB){
				last_camera_center_pos_ = camera.get_stable_center();
			}
			ctx.consume(*this);
		}

		inline void viewport_begin() const {

			auto camera_vp = camera.get_v2v_mat(content_src_pos_abs());
			auto& r = renderer();



			r.push_scissor({this->content_bound_abs()});
			r.top_viewport().push_local_transform(camera_vp);
			r.notify_viewport_changed();
		}

		inline void viewport_end() const {
			auto& r = renderer();

			r.top_viewport().pop_local_transform();
			r.pop_scissor();
			r.notify_viewport_changed();
		}

		[[nodiscard]] inline math::vec2 get_transferred_pos(const math::vec2 content_local_pos) const noexcept{
			return camera.get_screen_to_world(content_local_pos, {});
		}

		[[nodiscard]] inline math::vec2 get_transferred_cursor_pos() const noexcept{
			return viewport::get_transferred_pos(this->get_scene().get_cursor_pos());
		}

	};
}
