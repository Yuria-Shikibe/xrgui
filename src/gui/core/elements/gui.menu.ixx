//
// Created by Matrix on 2025/6/1.
//

export module mo_yanxi.gui.elem.menu;

import mo_yanxi.gui.elem.sequence;
import mo_yanxi.gui.elem.scroll_pane;
import mo_yanxi.gui.elem.two_segment_elem;
import std;

namespace mo_yanxi::gui{
//the menu's layout is completely same as the sequence


template <typename HeadTy = gui::elem, typename BodyTy = gui::elem>
struct menu_create_result{
	create_handle<HeadTy, sequence::cell_type> head;
	BodyTy& body;
};



export
struct menu : two_segment_elem{
private:
	layout::expand_policy expand_policy_{};

	/**
	 * @brief Current Entry Index, or the index of the end of the entries when use default element.
	 */
	std::size_t current_showing_{};
	mr::heap_vector<elem_ptr> entries{get_scene().get_heap_allocator()};

	void init(elem_ptr&& default_content){
		interactivity = interactivity_flag::children_only;
		create_head([](scroll_pane& scroll_pane){
			scroll_pane.create([](sequence& sequence){
				sequence.interactivity = interactivity_flag::children_only;
				sequence.set_style();
				sequence.set_expand_policy(layout::expand_policy::prefer);
			});
		}, layout::transpose_layout(layout_policy_));
		items[1] = std::move(default_content);
	}

	[[nodiscard]] sequence& get_button_sequence() const{
		return elem_cast<sequence, true>(elem_cast<scroll_pane, true>(*items[0]).get_item());
	}

public:
	template <typename... Args>
		requires (std::constructible_from<elem_ptr, scene&, elem*, Args&&...>)
	[[nodiscard]] menu(
		scene& scene, elem* parent,
		layout::layout_policy layout_policy,
		Args&&... args
	)
	: two_segment_elem(scene, parent, layout_policy){
		init(elem_ptr{scene, this, std::forward<Args>(args)...});
	}

	[[nodiscard]] menu(scene& scene, elem* parent)
	: menu(scene, parent, layout::layout_policy::hori_major, std::in_place_type<elem>){
	}

	layout::partial_mastering_cell& get_head_template_cell() const noexcept{
		return get_button_sequence().template_cell;
	}

	menu_create_result<elem, elem> insert(std::size_t index, elem_ptr&& head, elem_ptr&& body){
		index = std::min(index, entries.size());
		if(current_showing_ <= index){
			++current_showing_;
		}
		auto hdl = get_button_sequence().insert_and_get(index, std::move(head));
		auto& body_ref = *body;
		entries.insert(entries.begin() + index, std::move(body));
		return {std::move(hdl), body_ref};
	}

	template <elem_init_func HeadInit, elem_init_func BodyInit>
	menu_create_result<elem_init_func_create_t<HeadInit>, elem_init_func_create_t<BodyInit>>
		create(std::size_t index, HeadInit&& head, BodyInit&& body){
		auto rst = insert(index,
			elem_ptr{get_scene(), this, std::forward<HeadInit>(head)},
			elem_ptr{get_scene(), this, std::forward<BodyInit>(body)}
		);

		return {
			rst.head.cast_to<elem_init_func_create_t<HeadInit>>(),
			elem_cast<elem_init_func_create_t<BodyInit>, true>(rst.body)
		};
	}

	template <elem_init_func HeadInit, elem_init_func BodyInit>
	menu_create_result<elem_init_func_create_t<HeadInit>, elem_init_func_create_t<BodyInit>>
	create_back(HeadInit&& head, BodyInit&& body){
		return this->create(entries.size(),
			std::forward<HeadInit>(head), std::forward<BodyInit>(body)
		);
	}

	void set_expand_policy(layout::expand_policy policy){
		if(util::try_modify(expand_policy_, policy)){
			notify_isolated_layout_changed();

			if(expand_policy_ == layout::expand_policy::passive){
				layout_state.inherent_accept_mask -= propagate_mask::child;
				layout_state.intercept_lower_to_isolated = true;
			} else{
				layout_state.inherent_accept_mask |= propagate_mask::child;
				layout_state.intercept_lower_to_isolated = false;
			}
		}
	}

	[[nodiscard]] layout::expand_policy get_expand_policy() const noexcept{
		return expand_policy_;
	}

	void set_layout_policy(const layout::layout_policy layout_policy){
		//TODO ban none
		if(util::try_modify(layout_policy_, layout_policy)){
			notify_isolated_layout_changed();
			auto trsp = layout::transpose_layout(layout_policy);
			elem_cast<scroll_pane, true>(*items[0]).set_layout_policy(trsp);
			get_button_sequence().set_layout_policy(trsp);
		}
	}

	void layout_elem() override{
		elem::layout_elem();
		layout_children(expand_policy_);

		for(auto& item : items){
			item->try_layout();
		}
	}

	void switch_to(std::size_t index){
		if(index == current_showing_)return;
		if(index > entries.size()){
			throw std::out_of_range{"index out of range"};
		}
		if(index == entries.size()){
			std::swap(entries[current_showing_], items[1]);
		}else{
			get_button_sequence().children()[index]->set_toggled(true);

			if(current_showing_ == entries.size()){
				std::swap(entries[index], items[1]);
			}else{
				std::swap(entries[current_showing_], items[1]);
				std::swap(items[1], entries[index]);
			}
		}

		notify_layout_changed(propagate_mask::lower);
		notify_isolated_layout_changed();
		if(current_showing_ != entries.size()){
			get_button_sequence().children()[current_showing_]->set_toggled(false);
		}
		current_showing_ = index;
	}

	events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override{
		if(aboves.size() < 3)return events::op_afterwards::fall_through;
		//this -> scroll[0] -> sequence[1] -> actual button[2]
		auto* elem = aboves[2];
		auto& seq = get_button_sequence();
		auto idx = seq.find_index(elem);
		if(idx == seq.children().size())return events::op_afterwards::fall_through;

		if(event.key.on_release()){
			if(idx == current_showing_){
				switch_to(entries.size());
			}else{
				switch_to(idx);
			}
		}

		return events::op_afterwards::intercepted;
	}

protected:
	std::optional<math::vec2> pre_acquire_size_impl(layout::optional_mastering_extent extent) override{
		switch(layout_policy_){
		case layout::layout_policy::hori_major :{
			if(extent.width_pending()) return std::nullopt;
			break;
		}
		case layout::layout_policy::vert_major :{
			if(extent.height_pending()) return std::nullopt;
			break;
		}
		case layout::layout_policy::none :{
			if(extent.fully_mastering()) return extent.potential_extent();
			return std::nullopt;
		}
		default : std::unreachable();
		}

		auto potential = extent.potential_extent();
		const auto dep = extent.get_pending();

		auto [majorTargetDep, minorTargetDep] = layout::get_vec_ptr<bool>(layout_policy_);

		if(dep.*minorTargetDep){
			auto [majorTarget, minorTarget] = layout::get_vec_ptr(layout_policy_);

			if(expand_policy_ == layout::expand_policy::passive){
				potential.*minorTarget = this->content_extent().*minorTarget;
			} else{
				potential.*minorTarget = get_layout_minor_dim_config(potential.*majorTarget).masterings;
			}
		}

		if(auto pref = get_prefer_content_extent(); pref && expand_policy_ == layout::expand_policy::prefer){
			potential.max(pref.value());
		}

		return potential;
	}
};
}
