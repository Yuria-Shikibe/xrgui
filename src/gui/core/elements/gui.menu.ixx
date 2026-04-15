export module mo_yanxi.gui.elem.menu;

import mo_yanxi.gui.elem.sequence;
import mo_yanxi.gui.elem.scroll_pane;
import mo_yanxi.gui.elem.head_body_elem;
import std;

namespace mo_yanxi::gui{
//the menu's layout is completely same as the sequence


template <typename HeadTy = gui::elem, typename BodyTy = gui::elem>
struct menu_create_result{
	create_handle<HeadTy, sequence::cell_type> head;
	BodyTy& body;
};



export
struct menu : head_body{
	using button_panel_type = scroll_adaptor<sequence>;

private:

	/**
	 * @brief Current Entry Index, or the index of the end of the entries when use default element.
	 */
	std::size_t current_showing_{};
	mr::heap_vector<elem_ptr> entries{get_scene().get_heap_allocator()};

	void init(elem_ptr&& default_content){
		interactivity = interactivity_flag::children_only;
		create_head([&](button_panel_type& pane){
			sequence& sequence = pane.get_elem();
			sequence.set_layout_policy(layout::transpose_layout(layout_policy_));
			sequence.interactivity = interactivity_flag::children_only;
			sequence.set_style();
			sequence.set_expand_policy(layout::expand_policy::prefer);
		}, layout::transpose_layout(layout_policy_));
		items[1] = std::move(default_content);
	}


public:
	template <typename... Args>
		requires (std::constructible_from<elem_ptr, scene&, elem*, Args&&...>)
	[[nodiscard]] menu(
		scene& scene, elem* parent,
		layout::layout_policy layout_policy,
		Args&&... args
	)
	: head_body(scene, parent, layout_policy){
		init(elem_ptr{scene, this, std::forward<Args>(args)...});
	}

	[[nodiscard]] menu(scene& scene, elem* parent)
	: menu(scene, parent, layout::layout_policy::hori_major, std::in_place_type<elem>){
	}

	[[nodiscard]] button_panel_type& get_button_pane() const{
		return elem_cast<button_panel_type, true>(*items[0]);
	}

	layout::partial_mastering_cell& get_head_template_cell() const noexcept{
		return get_button_pane().get_elem().template_cell;
	}

	menu_create_result<elem, elem> insert(std::size_t index, elem_ptr&& head, elem_ptr&& body){
		index = std::min(index, entries.size());
		if(current_showing_ <= index){
			++current_showing_;
		}
		head->interactivity = interactivity_flag::enabled;

		auto hdl = get_button_pane().get_elem().insert_and_get(index, std::move(head));
		auto& body_ref = *body;
		entries.insert(entries.begin() + index, std::move(body));
		return {std::move(hdl), body_ref};
	}

	menu_create_result<elem, elem> push_back(elem_ptr&& head, elem_ptr&& body){
		return insert(entries.size(), std::move(head), std::move(body));
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

	void switch_to(std::size_t index){
		if(index == current_showing_) return;
		if(index > entries.size()){
			throw std::out_of_range{"index out of range"};
		}


		auto* old_item = items[1].get();

		if(index == entries.size()){
			std::swap(entries[current_showing_], items[1]);
		} else{
			get_button_pane().get_elem().exposed_children()[index]->set_toggled(true);

			if(current_showing_ == entries.size()){
				std::swap(entries[index], items[1]);
			} else{
				std::swap(entries[current_showing_], items[1]);
				std::swap(items[1], entries[index]);
			}
		}


		auto* new_item = items[1].get();


		if(old_item != new_item){
			if(old_item){
				old_item->on_display_state_changed(false, false);
			}
			if(new_item){
				new_item->on_display_state_changed(true, false);
			}
		}

		notify_layout_changed(propagate_mask::lower);
		notify_isolated_layout_changed();
		if(current_showing_ != entries.size()){
			get_button_pane().get_elem().exposed_children()[current_showing_]->set_toggled(false);
		}
		current_showing_ = index;
	}

	element_collect_buffer collect_children() const override{
		return element_collect_buffer{elem_wrapper{elem_span{items, elem_ptr::cvt_mptr}}, elem_wrapper{elem_span{entries, elem_ptr::cvt_mptr}}};
	}

protected:
	void on_layout_policy_changed(const layout::layout_policy layout_policy) override{
		head_body::on_layout_policy_changed(layout_policy);

		const auto trsp = layout::transpose_layout(layout_policy);
		elem_cast<button_panel_type, true>(*items[0]).set_layout_policy(trsp);
		get_button_pane().get_elem().set_layout_policy(trsp);
	}

	events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override{
		if(aboves.size() < 3)return events::op_afterwards::fall_through;
		//this -> scroll[0] -> sequence[1] -> actual button[2]
		auto* elem = aboves[2];
		auto& seq = get_button_pane().get_elem();
		auto idx = seq.find_index(elem);
		if(idx == seq.exposed_children().size())return events::op_afterwards::fall_through;

		if(event.key.on_release()){
			if(idx == current_showing_){
				switch_to(entries.size());
			}else{
				switch_to(idx);
			}
		}

		return events::op_afterwards::intercepted;
	}

};


}
