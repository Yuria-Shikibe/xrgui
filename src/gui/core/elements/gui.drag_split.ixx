//
// Created by Matrix on 2026/1/29.
//

export module mo_yanxi.gui.elem.drag_split;

import mo_yanxi.gui.elem.two_segment_elem;
import mo_yanxi.gui.layout.policies;
import mo_yanxi.snap_shot;
import std;

import mo_yanxi.graphic.draw.instruction;

namespace mo_yanxi::gui{

export
struct drag_split : head_body_no_invariant{
private:
	snap_shot<float> seperator_position_{.5f};

	//TODO minimal size

	void update_seperator(){
		set_head_size({layout::size_category::passive, seperator_position_.base});
		set_body_size({layout::size_category::passive, 1.f - seperator_position_.base});
		notify_isolated_layout_changed();
	}

public:

	drag_split(scene& scene, elem* parent, layout::layout_policy layout_policy)
		: head_body_no_invariant(scene, parent, layout_policy){
		extend_focus_until_mouse_drop = true;
		interactivity = interactivity_flag::enabled;
		emplace_head<gui::elem>();
		emplace_body<gui::elem>();
		set_head_size({layout::size_category::passive, .5f});
		set_body_size({layout::size_category::passive, .5f});
	}

	drag_split(scene& scene, elem* parent)
		: drag_split(scene, parent, layout::layout_policy::vert_major){
	}

	events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override{
		auto ret = head_body::on_click(event, aboves);
		if(event.key.on_release() && seperator_position_.is_dirty()){
			seperator_position_.apply();
			update_seperator();
			return events::op_afterwards::intercepted;
		}
		return ret;
	}

	events::op_afterwards on_drag(const events::drag event) override{
		const auto cursorlocal = event.src;
		const auto region = get_seperator_region_contnet_local();

		if(!region.contains_loose(cursorlocal))return events::op_afterwards::fall_through;

		auto [major_p, minor_p] = layout::get_vec_ptr(get_layout_policy());
		auto offset_in_minor = event.delta().*minor_p;
		auto minor_ext = content_extent().*minor_p - pad_;

		auto delta_offset = offset_in_minor / minor_ext;
		seperator_position_.temp = math::clamp(seperator_position_.base + delta_offset);
		return events::op_afterwards::intercepted;
	}

	void draw_layer(const rect clipSpace, gfx_config::layer_param_pass_t param) const override{
		head_body::draw_layer(clipSpace, param);
		if(param == 0){
			auto region = get_seperator_region_contnet_local();
			auto cursorlocal = util::transform_from_root_to_current(this, get_scene().get_cursor_pos());
			auto color = region.contains_loose(cursorlocal) ? graphic::colors::pale_green : graphic::colors::YELLOW;
			region.move(content_src_pos_abs());

			get_scene().renderer().push(graphic::draw::instruction::rect_aabb{
				.v00 = region.vert_00(),
				.v11 = region.vert_11(),
				.vert_color = {color.copy_set_a(.5)}
			});
		}

		if(seperator_position_.is_dirty()){
			const auto [major_p, minor_p] = layout::get_vec_ptr(get_layout_policy());
			auto src = content_src_pos_abs();

			math::vec2 off{};
			off.*minor_p = seperator_position_.temp * content_extent().*minor_p;

			src += off;


			get_scene().renderer().push(graphic::draw::instruction::line{
				.src = src,
				.dst = src + math::vec2{0, content_height()},
				.color = {graphic::colors::white, graphic::colors::white},
				.stroke = 4,
			});
		}
	}
};
}