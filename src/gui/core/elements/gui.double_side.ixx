//
// Created by Matrix on 2026/3/23.
//

export module mo_yanxi.gui.elem.double_side;

import std;
import mo_yanxi.gui.infrastructure;
import mo_yanxi.gui.layout.policies;

namespace mo_yanxi::gui{
export
template <std::size_t N>
	requires (N > 0)
struct double_side : elem{
	std::array<elem_ptr, N> candidates_{};
	std::size_t current_active_index_{};

private:
	layout::expand_policy expand_policy_{};

	elem_ptr& get_active_elem_ptr(){
		return candidates_[current_active_index_];
	}

	const elem_ptr& get_active_elem_ptr() const{
		return candidates_[current_active_index_];
	}

public:
	[[nodiscard]] double_side(scene& scene, elem* parent)
		: elem(scene, parent){
	}

	elem& get_current_active() const noexcept{
		return *get_active_elem_ptr();
	}

	[[nodiscard]] layout::expand_policy get_expand_policy() const noexcept{
		return expand_policy_;
	}

	void set_expand_policy(const layout::expand_policy expand_policy){
		if(util::try_modify(expand_policy_, expand_policy)){
			notify_layout_changed(propagate_mask::upper);
			if(expand_policy == layout::expand_policy::passive){
				layout_state.intercept_lower_to_isolated = true;
			} else{
				layout_state.intercept_lower_to_isolated = false;
			}
		}
	}

	template <std::derived_from<elem> E, typename... Args>
		requires (std::constructible_from<E, scene&, elem*, Args...>)
	E& emplace(std::size_t index, Args&&... args){
		if(index >= candidates_.size()){
			throw std::out_of_range{"index out of elem range"};
		}
		candidates_[index] = elem_ptr{get_scene(), this, std::in_place_type<E>, std::forward<Args>(args)...};
		return static_cast<E&>(*candidates_[index]);
	}

	template <invocable_elem_init_func Fn, typename... Args>
	elem_init_func_create_t<Fn>& create(std::size_t index, Fn&& init, Args&&... args){
		if(index >= candidates_.size()){
			throw std::out_of_range{"index out of elem range"};
		}
		candidates_[index] = elem_ptr{get_scene(), this, std::forward<Fn>(init), std::forward<Args>(args)...};
		return static_cast<elem_init_func_create_t<Fn>&>(*candidates_[index]);
	}

	elem_span children() const noexcept final{
		return elem_span{get_active_elem_ptr(), elem_ptr::cvt_mptr};
	}

	virtual void record_draw_layer(draw_call_stack_recorder& call_stack_builder){
		this->push_draw_func_to_stack_recorder(call_stack_builder);
	}

	void draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const override{
		elem::draw_layer(clipSpace, param);
		get_active_elem_ptr()->try_draw_layer(clipSpace, param);
	}

	void layout_elem() override{
		elem::layout_elem();
		get_active_elem_ptr()->try_layout();
	}

	bool update(float delta_in_ticks) override{
		if(elem::update(delta_in_ticks)){
			auto& cur = *get_active_elem_ptr();
			if(!cur.update_flag.is_update_required()) return true;
			cur.update(delta_in_ticks);
			return true;
		}
		return false;
	}

	bool update_abs_src(math::vec2 parent_content_src) noexcept override{
		if(elem::update_abs_src(parent_content_src)){
			auto& cur = *get_active_elem_ptr();
			cur.update_abs_src(content_src_pos_abs());

			return true;
		}
		return false;
	}

protected:
	bool resize_impl(const math::vec2 size) override{
		if(elem::resize_impl(size)){
			auto& cur = *get_active_elem_ptr();
			cur.restriction_extent = clip_boarder_from(restriction_extent, boarder_extent());
			cur.resize(content_extent());

			return true;
		}
		return false;
	}

public:
	void on_context_sync_bind() override{
		elem::on_context_sync_bind();
		for (const elem_ptr& child : candidates_){
			if(child)child->on_context_sync_bind();
		}
	}

	[[nodiscard]] std::size_t get_current_active_index() const noexcept {
		return current_active_index_;
	}

	template <std::derived_from<elem> E = elem, bool unchecked = false>
	E& at(std::size_t index){
		return gui::elem_cast<E, unchecked>(*candidates_.at(index));
	}

	void switch_to(std::size_t index){
		if(index >= candidates_.size()){
			throw std::out_of_range{"index out of elem range"};
		}

		if(current_active_index_ != index){
			get_active_elem_ptr()->on_display_state_changed(false, false);
			current_active_index_ = index;
			auto& cur = *get_active_elem_ptr();
			cur.on_display_state_changed(true, false);
			cur.restriction_extent = clip_boarder_from(restriction_extent, boarder_extent());
			cur.update_abs_src(content_src_pos_abs());
			cur.resize(content_extent());
			notify_isolated_layout_changed();
		}
	}

protected:
	std::optional<math::vec2> pre_acquire_size_impl(layout::optional_mastering_extent extent) override{
		if(expand_policy_ == layout::expand_policy::passive) return std::nullopt;

		auto& cur = *get_active_elem_ptr();
		auto rst = cur.pre_acquire_size(extent);
		if(!rst) return rst;
		return util::select_prefer_extent(expand_policy_ == layout::expand_policy::prefer, rst.value(),
			get_prefer_extent());
	}
};
}
