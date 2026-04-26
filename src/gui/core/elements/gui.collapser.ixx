//

//

export module mo_yanxi.gui.elem.collapser;


export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.gui.layout.policies;
export import mo_yanxi.gui.elem.head_body_elem;

import std;
import mo_yanxi.gui.util.animator;

namespace mo_yanxi::gui{
export
//TODO as bit flag?
enum struct collapser_expand_cond : std::uint8_t{
	click,
	inbound,
	focus,
	pressed,
};


struct collapser_settings{
	//TODO move cond to this struct?

	float expand_enter_spacing{30.f};
	float expand_exit_spacing{30.f};
	float expand_speed{3.15f / 60.f};
};

export
struct collapser : head_body_base{
private:

	collapser_expand_cond expand_cond_{};
	util::delayed_animator<float> animator_{};

	bool clicked_{};
	bool update_opacity_during_expand_{};


	void update_collapse(float delta) noexcept;

protected:
	[[nodiscard]] bool is_clicked() const{
		return clicked_;
	}

public:

	collapser_settings settings{};

	[[nodiscard]] collapser(scene& scene, elem* parent, const layout::directional_layout_specifier layout_policy)
	: head_body_base(scene, parent, layout_policy){
		interactivity = gui::interactivity_flag::enabled;
		layout_state.intercept_lower_to_isolated = true;
		item_size[0] = {layout::size_category::pending, 1};
		item_size[1] = {layout::size_category::pending, 1};


	}

	[[nodiscard]] collapser(scene& scene, elem* parent)
		: collapser(scene, parent, layout::directional_layout_specifier::fixed(
			elem::search_layout_policy(parent, false).value_or(layout::layout_policy::hori_major)
		)){
	}

	using head_body_base::create;
	using head_body_base::emplace;
	using head_body_base::emplace_head;
	using head_body_base::emplace_body;
	using head_body_base::create_head;
	using head_body_base::create_body;

	[[nodiscard]] collapser_expand_cond get_expand_cond() const noexcept{
		return expand_cond_;
	}

	void set_expand_cond(const collapser_expand_cond expand_cond){
		expand_cond_ = expand_cond;
	}

	[[nodiscard]] bool expandable() const noexcept{
		//TODO maintain if children has tooltip?
		switch(expand_cond_){
		case collapser_expand_cond::inbound:
			return cursor_states_.inbound;
		case collapser_expand_cond::focus:
			return cursor_states_.focused;
		case collapser_expand_cond::pressed:
			return cursor_states_.pressed;
		default:
			return clicked_;
		}
	}

	[[nodiscard]] bool is_update_opacity_during_expand() const{
		return update_opacity_during_expand_;
	}

	void set_update_opacity_during_expand(const bool update_opacity_during_expand) noexcept{
		if(util::try_modify(update_opacity_during_expand_, update_opacity_during_expand) && items[1]){
			if(update_opacity_during_expand){
				body().set_propagate_opacity(get_interped_progress());
			} else{
				body().set_propagate_opacity(1.f);
			}
		}
	}

	void layout_elem() override{
		elem::layout_elem();
		layout_children(layout::expand_policy::passive);

		for(auto& item : items){
			item->try_layout();
		}
	}

	events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override{
		if(expand_cond_ == collapser_expand_cond::click){
			if((!aboves.empty() && aboves.front() == items[0].get())){
				if(event.key.action == input_handle::act::release){
					clicked_ = !clicked_;
					util::update_insert(*this, update_channel::layout);
				}
				return events::op_afterwards::intercepted;
			} else if(head().contains(event.pos)){
				cursor_states_.update_press(event.key);
				if(event.key.action == input_handle::act::release){
					clicked_ = !clicked_;
					util::update_insert(*this, update_channel::layout);
				}
				return events::op_afterwards::intercepted;
			}

			return events::op_afterwards::fall_through;
		}else{
			util::update_insert(*this, update_channel::layout);
			return elem::on_click(event, aboves);
		}
	}

	void on_inbound_changed(bool is_inbounded, bool changed) override{
		head_body_base::on_inbound_changed(is_inbounded, changed);
		if(expand_cond_ == collapser_expand_cond::inbound){
			util::update_insert(*this, update_channel::layout);
		}
	}

	void on_focus_changed(bool is_focused) override{
		head_body_base::on_focus_changed(is_focused);
		if(expand_cond_ == collapser_expand_cond::focus){
			util::update_insert(*this, update_channel::layout);
		}
	}


	bool update(float delta_in_ticks) override{
		if(!head_body_base::update(delta_in_ticks)) return false;

		update_collapse(delta_in_ticks);

		const auto st = animator_.get_state();
		body().invisible = (st == util::anim_state::idle || st == util::anim_state::waiting_to_enter);
		return true;
	}

	void record_draw_layer(draw_recorder& call_stack_builder) const override;

protected:
	void set_item_size(bool isContent, layout::stated_size size) override{
		if(size.type == layout::size_category::passive){
			throw layout::illegal_layout{"Passive is not allowd here"};
		}

		head_body_base::set_item_size(isContent, size);
	}

	std::optional<math::vec2> pre_acquire_size_impl(layout::optional_mastering_extent extent) override;


	void set_children_src() const final{
		auto [_, minor] = layout::get_vec_ptr(get_layout_policy());

		math::vec2 relOff{};

		if(transpose_head_and_body_){
			const auto sz = items[1]->extent();
			relOff.*minor += (pad_ + sz.*minor) * get_interped_progress();
		}else{
			const auto sz = items[0]->extent();
			relOff.*minor += pad_ + sz.*minor;
		}

		items[transpose_head_and_body_]->set_rel_pos({});
		items[!transpose_head_and_body_]->set_rel_pos(relOff);

		auto src = content_src_pos_abs();
		items[0]->update_abs_src(src);
		items[1]->update_abs_src(src);
	}

	[[nodiscard]] float get_interped_progress() const noexcept;

	[[nodiscard]] bool is_expanding() const noexcept{
		return animator_.is_animating();
	}

	[[nodiscard]] rect get_expand_region() const noexcept;

};

}
