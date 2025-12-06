module;

#include <cassert>

export module mo_yanxi.gui.infrastructure.group;

import mo_yanxi.gui.infrastructure;
import std;

namespace mo_yanxi::gui{


/**
 *
 * @brief basic element group provide basic sequential operations.
 */
export
struct basic_group : public elem{
protected:
	mr::heap_vector<elem_ptr> expired_{get_heap_allocator<elem_ptr>()};
	mr::heap_vector<elem_ptr> children_{get_heap_allocator<elem_ptr>()};

	void update_children(const float delta_in_ticks) const{
		for(const auto& element : children_){
			element->update(delta_in_ticks);
		}
	}

	void layout_children() const{
		for(const auto& element : children_){
			element->try_layout();
		}
	}

	void draw_children(const rect clipSpace) const{
		for(const auto& element : children()){
			element->try_draw(clipSpace);
		}
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
	virtual void clear(){
		expired_.clear();
		children_.clear();
		notify_layout_changed_on_element_change();
	}

	virtual void erase_afterward(std::size_t where){
		assert(where < children().size());

		const auto itr = children_.begin() + where;
		expired_.push_back(std::move(*itr));
		children_.erase(itr);

		notify_layout_changed_on_element_change();
	}

	virtual void erase_instantly(std::size_t where){
		assert(where < children().size());

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
	[[nodiscard]] std::span<const elem_ptr> children() const noexcept final{
		return children_;
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

		if(!elem::update(delta_in_ticks))return false;
		update_children(delta_in_ticks);

		return true;
	}

	void draw_content(const rect clipSpace) const override{
		elem::draw_content(clipSpace);
		const auto space = content_bound_abs().intersection_with(clipSpace);
		draw_children(space);
	}

	void layout_elem() override{
		elem::layout_elem();
		layout_children();
	}

protected:
	bool resize_impl(const math::vec2 size) override{
		if(elem::resize_impl(size)){
			const auto newSize = content_extent();
			bool any = false;
			for(auto& element : children()){
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
	virtual void on_element_add(elem& elem) const{
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

protected:
	void notify_layout_changed_on_element_change() override{
		notify_isolated_layout_changed();
	}
};

}
