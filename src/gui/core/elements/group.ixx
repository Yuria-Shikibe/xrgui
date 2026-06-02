module;

#include <cassert>

export module mo_yanxi.gui.elem.group;

import mo_yanxi.gui.infrastructure;
import std;

namespace mo_yanxi::gui{


/**
 *
 * @brief basic element group provide basic sequential operations.
 */
export
struct basic_group : public elem{
public:
	enum struct overflowed_state{
		fully_wrapped,
		leaked,
		ignore,
	};

protected:
	mr::heap_vector<elem_ptr> expired_{get_heap_allocator<elem_ptr>()};
	mr::heap_vector<elem_ptr> children_{get_heap_allocator<elem_ptr>()};
	overflowed_state overflowed_state_{overflowed_state::fully_wrapped};

	void layout_children() const{
		for(const auto& element : children_){
			element->try_layout();
		}
	}

	void refresh_overflowed_state_from_children(){
		if(is_overflow_ignored()) return;

		const auto content_bound = rect{tags::from_extent, {}, content_extent()};
		auto state = overflowed_state::fully_wrapped;

		for(const auto& element : exposed_children()){
			if(!element || !element->is_visible()) continue;
			if(!content_bound.contains_loose(element->bound_rel())){
				state = overflowed_state::leaked;
				break;
			}
		}

		util::try_modify(overflowed_state_, state);
	}

	virtual void notify_layout_changed_on_element_change(){
		notify_layout_changed(propagate_mask::upper | propagate_mask::force_upper);
	}

public:
	[[nodiscard]] basic_group(scene& scene, elem* parent)
		: elem(scene, parent){
		interactivity = interactivity_flag::children_only;
	}

#pragma region Erase
private:

public:
	virtual void clear(){
		expired_.clear();
		children_.clear();
		if(!is_overflow_ignored()){
			overflowed_state_ = overflowed_state::fully_wrapped;
		}

		notify_layout_changed_on_element_change();
	}

	virtual void erase_afterward(std::size_t where){
		assert(where < exposed_children().size());

		const auto itr = children_.begin() + where;
		expired_.push_back(std::move(*itr));
		children_.erase(itr);

		notify_layout_changed_on_element_change();
	}

	virtual void erase_instantly(std::size_t where){
		assert(where < exposed_children().size());

		const auto itr = children_.begin() + where;
		children_.erase(itr);

		notify_layout_changed_on_element_change();
	}

	virtual elem_ptr exchange(std::size_t where, elem_ptr&& elem, bool force_isolated_notify){
		assert(elem != nullptr);
		if(where >= children_.size()){
			throw std::out_of_range{"index out of range"};
		}

		auto& e = *elem;
		auto rst = std::exchange(children_[where], std::move(elem));
		if(force_isolated_notify){
			notify_isolated_layout_changed();
		} else{
			notify_layout_changed_on_element_change();
		}
		on_element_add(e);

		return rst;
	}

	elem& front() const noexcept{
		return *children_.front();
	}

	elem& back() const noexcept{
		return *children_.back();
	}

#pragma endregion

#pragma region Add
public:
	virtual elem& insert(std::size_t where, elem_ptr&& elemPtr){
		elem& e = **children_.insert(children_.begin() + std::min<std::size_t>(where, children_.size()), std::move(elemPtr));
		e.set_parent(this);
		notify_layout_changed_on_element_change();
		on_element_add(e);

		return e;
	}

	elem& push_back(elem_ptr&& elem){
		return insert(std::numeric_limits<std::size_t>::max(), std::move(elem));
	}

	template <std::derived_from<elem> E, typename... Args>
	E& emplace(std::size_t index, Args&&... args){
		auto& elem = insert(
			index,
			elem_ptr{get_scene(), this, std::in_place_type<E>, std::forward<Args>(args)...});
		return static_cast<E&>(elem);
	}
#pragma endregion

#pragma region Override
public:
	[[nodiscard]] elem_span exposed_children() const noexcept override{
		return {children_, elem_ptr::cvt_mptr};
	}

	bool update_abs_src(math::vec2 parent_content_src) noexcept override{
		if(elem::update_abs_src(parent_content_src)){
			for (const auto & child : children_){
				child->update_abs_src(content_src_pos_abs());
			}

			return true;
		}
		return false;
	}

	bool update(const float delta_in_ticks) override{
		expired_.clear();

		return elem::update(delta_in_ticks);
	}



	void layout_elem() override{
		elem::layout_elem();
		layout_children();
		refresh_overflowed_state_from_children();
	}

	void record_draw_layer(draw_recorder& call_stack_builder) const override{
		elem::record_draw_layer(call_stack_builder);

		call_stack_builder.push_call_enter(*this, [](const basic_group& s, const draw_call_param& p) static -> draw_call_param{
			const auto content_bound = s.content_bound_abs();
			const auto draw_bound = content_bound.intersection_with(p.draw_bound);
			const float opacity_scl = util::get_final_draw_opacity(s, p);

			if(opacity_scl < 0.f || draw_bound.is_roughly_zero_area(0.01f)){
				return {
						.current_subject = nullptr,
						.draw_bound = draw_bound,
						.opacity_scl = opacity_scl
					};
			}

			if(s.should_clip_overflow()){
				s.renderer().push_scissor({content_bound});
				s.renderer().notify_viewport_changed();
			}

			return {
				.current_subject = &s,
				.draw_bound = draw_bound,
				.opacity_scl = opacity_scl
			};
		});

		for(const auto& element : exposed_children()){
			element->record_draw_layer(call_stack_builder);
		}

		call_stack_builder.push_call_leave(*this, [](const basic_group& s, const draw_call_param&) static {
			if(s.should_clip_overflow()){
				s.renderer().pop_scissor();
				s.renderer().notify_viewport_changed();
			}
		});
	}

protected:

	bool resize_impl(const math::vec2 size) override{
		if(elem::resize_impl(size)){
			const auto newSize = content_extent();
			bool any = false;
			for(auto& element : exposed_children()){
				any = any || util::set_fill_parent(*element, newSize);
			}
			if(any)notify_layout_changed_on_element_change();
			return true;
		}

		return false;
	}

#pragma endregion

#pragma region Access
public:
	[[nodiscard]] overflowed_state get_overflowed_state() const noexcept{
		return overflowed_state_;
	}

	[[nodiscard]] bool is_overflow_ignored() const noexcept{
		return overflowed_state_ == overflowed_state::ignore;
	}

	void set_overflow_ignored(const bool ignore){
		if(ignore){
			util::try_modify(overflowed_state_, overflowed_state::ignore);
		} else if(overflowed_state_ == overflowed_state::ignore){
			overflowed_state_ = overflowed_state::fully_wrapped;
			notify_isolated_layout_changed();
		}
	}

	[[nodiscard]] elem& operator[](std::size_t index) const noexcept{
		return *children_[index];
	}

	template <std::derived_from<elem> Ty = elem, bool unchecked = false>
	[[nodiscard]] Ty& at(const std::size_t index) const noexcept(unchecked || std::same_as<Ty, elem>){
		return elem_cast<Ty, unchecked>(*children_.at(index));
	}

	decltype(children_)::iterator find(elem* elem) noexcept{
		return std::ranges::find(children_, elem, &elem_ptr::get);
	}

	std::size_t find_index(elem* elem) noexcept{
		return std::ranges::distance(children_.begin(), find(elem));
	}
#pragma endregion

protected:
	[[nodiscard]] virtual bool should_clip_overflow() const noexcept{
		return overflowed_state_ == overflowed_state::leaked;
	}

	void on_element_add(elem& elem) const{
		util::set_fill_parent(elem, content_extent());
		elem.update_abs_src(content_src_pos_abs());
	}

};

/**
 *
 * @brief element in this group have no layout relation with the group, except fill parent.
 */
export
struct loose_group : basic_group{
	[[nodiscard]] loose_group(scene& scene, elem* parent)
		: basic_group(scene, parent){
		layout_state.ignore_children();
		layout_state.inherent_broadcast_mask -= propagate_mask::child;
		layout_state.intercept_lower_to_isolated = true;
	}

	void layout_elem() override{
		for(const auto& element : children_){
			util::set_fill_parent(*element, content_extent());
			element->try_layout();
			element->update_abs_src(content_src_pos_abs());
		}

		elem::layout_elem();
		refresh_overflowed_state_from_children();
	}

protected:
	void notify_layout_changed_on_element_change() override{
		notify_isolated_layout_changed();
	}

};

}
