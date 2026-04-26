module;

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.elem.drag_split;

import mo_yanxi.gui.elem.head_body_elem;
import mo_yanxi.gui.layout.policies;
import mo_yanxi.snap_shot;
import std;

import mo_yanxi.graphic.draw.instruction;

namespace mo_yanxi::gui{
export
struct split_pane : head_body_no_invariant{
private:
	snap_shot<float> seperator_position_{.5f};
	math::range min_margin{0.1f, 0.1f};


	enum class drag_state{
		idle,
		entering,
		dragging,
		exiting
	};

	drag_state current_drag_state_{drag_state::idle};
	float drag_progress_{0.f};

	void update_seperator(){
		set_head_size({layout::size_category::passive, seperator_position_.base});
		set_body_size({layout::size_category::passive, 1.f - seperator_position_.base});
		notify_isolated_layout_changed();
	}

	void move_seperator(math::vec2 delta){
		auto [major_p, minor_p] = layout::get_vec_ptr(get_layout_policy());
		auto offset_in_minor = delta.*minor_p;
		auto minor_ext = content_extent().*minor_p - pad_;
		auto delta_offset = offset_in_minor / minor_ext;



		util::try_modify(seperator_position_.temp,
		                 math::clamp(seperator_position_.base + delta_offset, min_margin.from, 1.f - min_margin.to));
	}

public:
	split_pane(scene& scene, elem* parent, const layout::directional_layout_specifier layout_policy)
		: head_body_no_invariant(scene, parent, layout_policy){
		extend_focus_until_mouse_drop = true;
		interactivity = interactivity_flag::enabled;
		set_head_size({layout::size_category::passive, .5f});
		set_body_size({layout::size_category::passive, .5f});
	}

	split_pane(scene& scene, elem* parent)
		: split_pane(scene, parent, layout::directional_layout_specifier::fixed(layout::layout_policy::vert_major)){
	}

	bool update(float delta_in_ticks) override{
		if(head_body_no_invariant::update(delta_in_ticks)){
			update_state(delta_in_ticks / 45.f);
			return true;
		} else{
			return false;
		}
	}


	void update_state(float dt){
		constexpr float fade_speed = 5.0f;
		bool changed = false;

		if(current_drag_state_ == drag_state::entering){
			drag_progress_ += dt * fade_speed;
			if(drag_progress_ >= 1.0f){
				drag_progress_ = 1.0f;
				current_drag_state_ = drag_state::dragging;
				util::update_erase(*this, update_channel::layout);
			}
			changed = true;
		} else if(current_drag_state_ == drag_state::exiting){
			drag_progress_ -= dt * fade_speed;
			if(drag_progress_ <= 0.0f){
				drag_progress_ = 0.0f;
				current_drag_state_ = drag_state::idle;
				util::update_erase(*this, update_channel::layout);
			}
			changed = true;
		}

		if(changed){

			set_children_opacity_with_scl(math::lerp(1.0f, 0.2f, drag_progress_));
		}
	}

	void set_split_pos(float p){
		if(util::try_modify(seperator_position_.base, math::clamp(p, min_margin.from, 1.f - min_margin.to))){
			seperator_position_.resume();
			update_seperator();
		}
	}

	[[nodiscard]] math::range get_min_margin() const{
		return min_margin;
	}

	void set_min_margin(math::range min_margin){
		if(min_margin.from + min_margin.to > 1.001f){
			throw std::out_of_range("margin sum > 1");
		}

		min_margin.from = math::clamp(min_margin.from);
		min_margin.to = math::clamp(min_margin.to);
		if(util::try_modify(this->min_margin, min_margin)){
		}
	}

	events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override{
		auto ret = head_body::on_click(event, aboves);
		if(event.key.on_release()){

			if(current_drag_state_ == drag_state::dragging || current_drag_state_ == drag_state::entering){
				current_drag_state_ = drag_state::exiting;
				util::update_insert(*this, update_channel::layout);
			}

			if(seperator_position_.is_dirty()){
				seperator_position_.apply();
				update_seperator();
				return events::op_afterwards::intercepted;
			}
		}
		return ret;
	}

	events::op_afterwards on_drag(const events::drag event) override{

		if(current_drag_state_ == drag_state::idle || current_drag_state_ == drag_state::exiting){
			const auto cursorlocal = event.src;
			const auto region = get_seperator_region_element_local();

			if(!region.contains_loose(cursorlocal - content_src_offset())){
				return events::op_afterwards::fall_through;
			}


			current_drag_state_ = drag_state::entering;
			util::update_insert(*this, update_channel::layout);
		}


		move_seperator(event.delta());
		return events::op_afterwards::intercepted;
	}

	void record_draw_layer(draw_call_stack_recorder& call_stack_builder) const override{
		head_body::record_draw_layer(call_stack_builder);

		call_stack_builder.push_call_noop(
			*this, [](const split_pane& s, const draw_call_param& p, draw_call_stack&) static{
				const rect bound = s.bound_abs();
				bool enable_split_draw = p.draw_bound.overlap_exclusive(bound) && (s.seperator_position_.is_dirty() || s
					.drag_progress_ > 0.0f);
				if(!enable_split_draw) return;
				const float paneOpacity = util::get_final_draw_opacity(s, p);
				const auto [major_p, minor_p] = layout::get_vec_ptr(s.get_layout_policy());
				auto src = s.content_src_pos_abs();

				math::vec2 off{};
				math::vec2 ext{};
				off.*minor_p = s.seperator_position_.temp * s.content_extent().*minor_p;
				ext.*major_p = s.content_extent().*major_p;

				src += off;
				bool any = s.head().style || s.body().style;

				if(!any){
					s.renderer().push(graphic::draw::instruction::line{
							.src = src,
							.dst = src + ext,
							.color = {graphic::colors::white, graphic::colors::white},
							.stroke = 4,
						});
				}

				ext.*minor_p -= s.get_pad() / 2.f;
				if(s.head().style)
					s.head().draw_style({tags::from_vertex, s.content_src_pos_abs(), src + ext}, p.layer_param,
					                    paneOpacity * s.drag_progress_ * 4.f);
				src.*minor_p += s.get_pad() / 2.f;
				if(s.body().style)
					s.body().draw_style({
					                    tags::from_vertex, s.content_src_pos_abs() + s.content_extent(),
					                    src
					                }, p.layer_param, paneOpacity * s.drag_progress_ * 4.f);
			});
	}


	style::cursor_style get_cursor_type(math::vec2 cursor_pos_at_content_local) const noexcept override{
		const auto region = get_seperator_region_element_local();
		const bool hit = current_drag_state_ == drag_state::dragging || current_drag_state_ == drag_state::entering || region.contains_loose(cursor_pos_at_content_local - content_src_offset());

		if(hit){
			style::cursor_style rst{style::cursor_type::none};
			if(get_layout_policy() == layout::layout_policy::vert_major){
				rst.push_dcor(style::cursor_decoration_type::to_left);
				rst.push_dcor(style::cursor_decoration_type::to_right);
			} else{
				rst.push_dcor(style::cursor_decoration_type::to_up);
				rst.push_dcor(style::cursor_decoration_type::to_down);
			}
			return rst;
		} else{
			return style::cursor_style{style::cursor_type::regular};
		}
	}
};
}
