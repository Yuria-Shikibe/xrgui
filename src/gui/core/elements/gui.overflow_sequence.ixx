export module mo_yanxi.gui.elem.overflow_sequence;

import mo_yanxi.gui.elem.sequence;
import std;
import mo_yanxi.gui.action.elem;

namespace mo_yanxi::gui {

export
template <typename Elem, typename Cell>
struct overflow_sequence_create_result{
	Elem& elem;
	Cell& cell;
};

export
struct overflow_sequence : sequence {
private:
	std::size_t split_index_{0};
	

	elem_ptr overflow_elem_{};
	cell_adaptor<layout::partial_mastering_cell> overflow_cell_{};


	mr::heap_vector<elem*> exposed_children_{get_heap_allocator<elem*>()};
	mr::heap_vector<adaptor_type*> exposed_cells_{get_heap_allocator<adaptor_type*>()};
	mr::heap_vector<elem*> old_exposed_children_{get_heap_allocator<elem*>()};

	bool is_overflowed_{false};
	bool requires_scissor_{false};

public:
	[[nodiscard]] overflow_sequence(scene& scene, elem* parent)
		: sequence(scene, parent){
		set_expand_policy(layout::expand_policy::passive);
	}
protected:

	void notify_and_remove_exposed(elem* target) {
		if (!target) return;

		auto it = std::ranges::find(exposed_children_, target);
		if (it != exposed_children_.end()) {

			target->on_display_state_changed(false, false);


			std::size_t idx = std::distance(exposed_children_.begin(), it);
			exposed_children_.erase(it);

			if (idx < exposed_cells_.size()) {
				exposed_cells_.erase(exposed_cells_.begin() + idx);
			}
		}
	}

public:
	void erase_afterward(std::size_t where) override {
		if (where < cells_.size()) {
			notify_and_remove_exposed(cells_[where].element);
		}
		sequence::erase_afterward(where);
	}

	void erase_instantly(std::size_t where) override {
		if (where < cells_.size()) {
			notify_and_remove_exposed(cells_[where].element);
		}
		sequence::erase_instantly(where);
	}

	elem_ptr exchange(std::size_t where, elem_ptr&& elem, bool force_isolated_notify) override {
		if (where < cells_.size()) {
			notify_and_remove_exposed(cells_[where].element);
		}
		return sequence::exchange(where, std::move(elem), force_isolated_notify);
	}


	void set_split_index(std::size_t index) {
		if (util::try_modify(split_index_, index)) {
			notify_isolated_layout_changed();
		}
	}

	void set_overflow_elem(elem_ptr&& elem) {

		if (overflow_elem_) {
			for (auto& p : exposed_children_) {
				if (p == overflow_elem_.get()) {
					p->on_display_state_changed(false, false);
					p = nullptr;
				}
			}
		}

		overflow_elem_ = std::move(elem);
		if (overflow_elem_) {
			overflow_elem_->set_parent(this);
			overflow_elem_->update_abs_src(content_src_pos_abs());
			overflow_cell_.element = overflow_elem_.get();
			overflow_cell_.cell = template_cell;
		}
		notify_isolated_layout_changed();
	}

	template <invocable_elem_init_func Fn, typename... Args>
	overflow_sequence_create_result<elem_init_func_create_t<Fn>, layout::partial_mastering_cell> create_overflow_elem(Fn&& init, Args&&... args){
		elem_ptr eptr{get_scene(), this, std::forward<Fn>(init), std::forward<Args>(args)...};
		set_overflow_elem(std::move(eptr));
		return overflow_sequence_create_result{static_cast<elem_init_func_create_t<Fn>&>(*overflow_elem_), overflow_cell_.cell};
	}

	template <std::invocable<layout::partial_mastering_cell&> Fn>
	void modify_overflow_elem_cell(Fn&& fn){
		if constexpr (std::predicate<Fn&&, layout::partial_mastering_cell&>){
			if(std::invoke(std::forward<Fn>(fn), overflow_cell_.cell)){
				notify_isolated_layout_changed();
			}
		}else{
			std::invoke(std::forward<Fn>(fn), overflow_cell_.cell);
			notify_isolated_layout_changed();
		}
	}


	[[nodiscard]] bool is_scissor_required() const noexcept {
		return requires_scissor_;
	}

	[[nodiscard]] bool is_overflowed() const noexcept {
		return is_overflowed_;
	}


	[[nodiscard]] elem_span exposed_children() const noexcept final {

		return elem_span{exposed_children_.data(), exposed_children_.size()};
	}

	bool update_abs_src(math::vec2 parent_content_src) noexcept override {
		if (sequence::update_abs_src(parent_content_src)) {
			if (overflow_elem_) {
				overflow_elem_->update_abs_src(content_src_pos_abs());
			}
			return true;
		}
		return false;
	}

	void layout_elem() override {

		old_exposed_children_.clear();
		for (auto* e : exposed_children_) {

			if (e) old_exposed_children_.push_back(e);
		}

		exposed_children_.clear();
		exposed_cells_.clear();

		if (cells_.empty()) {
			is_overflowed_ = false;
			requires_scissor_ = false;
			if (auto pref = get_prefer_extent(); pref && get_expand_policy() == layout::expand_policy::prefer) {
				resize(pref.value(), propagate_mask::force_upper);
			}


			for (auto* e : old_exposed_children_) {
				e->on_display_state_changed(false, false);
			}

			old_exposed_children_.clear();
			return;
		}

		auto [majorTarget, minorTarget] = layout::get_vec_ptr(get_layout_policy());
		auto content_sz = content_extent();
		float major_size = content_sz.*majorTarget;
		float minor_scaling = get_scaling().*minorTarget;

		float max_minor = restriction_extent.potential_extent().*minorTarget;
		if (get_expand_policy() == layout::expand_policy::passive) {
			max_minor = std::min(max_minor, content_sz.*minorTarget);
		}

		mr::heap_vector<float> exposed_sizes{get_heap_allocator<float>()};

		auto info = calculate_overflow_layout(
			major_size, minor_scaling, max_minor,
			&exposed_children_, &exposed_cells_, &exposed_sizes
		);


		for (auto* e : old_exposed_children_) {
			if (std::ranges::find(exposed_children_, e) == exposed_children_.end()) {
				e->on_display_state_changed(false, false);
			}
		}

		for (auto* e : exposed_children_) {
			if (std::ranges::find(old_exposed_children_, e) == old_exposed_children_.end()) {
				e->push_action<action::alpha_ctx_fade_in_action>(15.f);
				e->on_display_state_changed(true, false);
			}
		}

		old_exposed_children_.clear();

		is_overflowed_ = info.is_overflowed;
		requires_scissor_ = info.requires_scissor;


		if (get_expand_policy() != layout::expand_policy::passive) {
			math::vec2 size;
			size.*majorTarget = content_sz.*majorTarget;
			size.*minorTarget = info.masterings;
			size += boarder().extent();

			if (get_expand_policy() == layout::expand_policy::prefer) {
				size.max(get_prefer_extent().value_or(math::vec2{}));
			}

			size.min(restriction_extent.potential_extent());
			resize(size, propagate_mask::force_upper);
			content_sz = content_extent();
		}


		float minor_offset = (is_align_to_tail() && info.passives == 0) ? content_sz.*minorTarget - info.masterings : 0;
		const auto remains = std::fdim(content_sz.*minorTarget, info.masterings);
		const auto passive_unit = info.passives > 0 ? remains / info.passives : 0;

		math::vec2 currentOff{};
		if (!exposed_cells_.empty()) {
			currentOff.*minorTarget = minor_offset - exposed_cells_.front()->cell.pad.pre;

			for (std::size_t i = 0; i < exposed_cells_.size(); ++i) {
				auto* cell = exposed_cells_[i];
				currentOff.*minorTarget += cell->cell.pad.pre;

				auto minor = exposed_sizes[i];
				if (cell->cell.stated_size.type == layout::size_category::passive) {
					minor *= passive_unit;
				}

				math::vec2 cell_sz;
				cell_sz.*majorTarget = content_sz.*majorTarget;
				cell_sz.*minorTarget = minor;

				cell->cell.allocated_region = {tags::from_extent, currentOff, cell_sz};

				cell_sz.*majorTarget = this->restriction_extent.potential_extent().*majorTarget;
				if (cell->cell.stated_size.pending()) cell_sz.*minorTarget = std::numeric_limits<float>::infinity();

				cell->apply(*this, cell_sz);

				auto pref = cell_sz;
				pref.*minorTarget = 0;
				cell->element->set_prefer_extent(pref);
				if (!is_pos_smooth()) cell->cell.update_relative_src(*cell->element, content_src_pos_abs());

				currentOff.*minorTarget += cell->cell.pad.post + minor;
			}
		}
	}

	void clear() noexcept override {
		sequence::clear();
		exposed_cells_.clear();
		exposed_children_.clear();
		notify_isolated_layout_changed();
	}

	void record_draw_layer(draw_recorder& call_stack_builder) const override{
		elem::record_draw_layer(call_stack_builder);

		call_stack_builder.push_call_enter(*this, [](const overflow_sequence& s, const draw_call_param& p) static -> draw_call_param{
			const auto space = s.content_bound_abs().intersection_with(p.draw_bound);

			if (s.requires_scissor_) {
				s.renderer().push_scissor({s.content_bound_abs()});
				s.renderer().notify_viewport_changed();
			}

			return {
				.current_subject = &s,
				.draw_bound = s.content_bound_abs().intersection_with(p.draw_bound),
				.opacity_scl = p.opacity_scl * s.get_local_draw_opacity(),
				.layer_param = p.layer_param
			};
		});

		for(auto element : exposed_children_){
			element->record_draw_layer(call_stack_builder);
		}

		call_stack_builder.push_call_leave(*this, [](const overflow_sequence& s, const draw_call_param& p) static {

			if (s.requires_scissor_) {
				s.renderer().pop_scissor();
				s.renderer().notify_viewport_changed();
			}
		});
	}

	struct overflow_layout_info {
		bool is_overflowed{false};
		bool requires_scissor{false};
		float masterings{0.f};
		float passives{0.f};
	};



	element_collect_buffer collect_children() const override{
		element_collect_buffer rst{};
		rst.push_back(elem_span{children_, elem_ptr::cvt_mptr});
		if(overflow_elem_)rst.push_back(*overflow_elem_);
		return rst;
	}


protected:
	overflow_layout_info calculate_overflow_layout(
		float major_size,
		float minor_scaling,
		float max_minor,
		mr::heap_vector<elem*>* out_children = nullptr,
		mr::heap_vector<adaptor_type*>* out_cells = nullptr,
		mr::heap_vector<float>* out_inner_sizes = nullptr
	) {
		overflow_layout_info info{};
		auto [majorTarget, minorTarget] = layout::get_vec_ptr(get_layout_policy());


		auto calc_inner_minor = [&](elem* e, const adaptor_type& c) {
			float sz = 0.f;
			switch (c.cell.stated_size.type) {
				case layout::size_category::pending: {
					math::vec2 vec;
					vec.*majorTarget = major_size;
					vec.*minorTarget = std::numeric_limits<float>::infinity();
					sz = e->pre_acquire_size(vec).value_or(math::vec2{}).*minorTarget;
					break;
				}
				case layout::size_category::mastering:
					sz = c.cell.stated_size.value * minor_scaling;
					break;
				case layout::size_category::passive:
					break;
				case layout::size_category::scaling:
					sz = c.cell.stated_size.value * major_size;
					break;
			}
			return sz;
		};


		mr::heap_vector<float> inner_sizes{get_heap_allocator<float>()};
		inner_sizes.reserve(cells_.size());
		float sum_all_minor = 0.f;

		for (auto& cell : cells_) {
			float inner = calc_inner_minor(cell.element, cell);
			inner_sizes.push_back(inner);

			sum_all_minor += inner + cell.cell.pad.length();
		}

		float all_actual_minor = sum_all_minor;
		if (!cells_.empty()) {

			all_actual_minor -= cells_.front().cell.pad.pre;
			all_actual_minor -= cells_.back().cell.pad.post;
		}

		const std::size_t valid_split = std::min(split_index_, cells_.size());

		float first_pad_pre = 0.f;
		float last_pad_post = 0.f;
		bool has_any = false;


		auto add_exposed = [&](elem* e, adaptor_type* c, float inner_sz) {
			if (out_children) out_children->push_back(e);
			if (out_cells) out_cells->push_back(c);
			if (out_inner_sizes) out_inner_sizes->push_back(inner_sz);


			info.masterings += inner_sz + c->cell.pad.length();
			if (c->cell.stated_size.type == layout::size_category::passive) {
				info.passives += c->cell.stated_size.value;
			}

			if (!has_any) {
				first_pad_pre = c->cell.pad.pre;
				has_any = true;
			}
			last_pad_post = c->cell.pad.post;
		};


		if (all_actual_minor <= max_minor || get_expand_policy() != layout::expand_policy::passive) {
			for (std::size_t i = 0; i < cells_.size(); ++i) {
				add_exposed(cells_[i].element, &cells_[i], inner_sizes[i]);
			}
		} else {
			info.is_overflowed = true;


			float current_sum = 0.f;
			float current_first_pre = 0.f;


			for (std::size_t i = 0; i < valid_split; ++i) {
				current_sum += inner_sizes[i] + cells_[i].cell.pad.length();
				if (i == 0) current_first_pre = cells_[i].cell.pad.pre;
			}


			float e_inner = 0.f;
			if (overflow_elem_) {
				e_inner = calc_inner_minor(overflow_elem_.get(), overflow_cell_);
				current_sum += e_inner + overflow_cell_.cell.pad.length();
				if (valid_split == 0) current_first_pre = overflow_cell_.cell.pad.pre;
			}


			mr::heap_vector<std::size_t> post_indices{get_heap_allocator<std::size_t>()};
			for (std::size_t i = cells_.size(); i > valid_split; --i) {
				std::size_t idx = i - 1;
				float cell_total = inner_sizes[idx] + cells_[idx].cell.pad.length();


				float candidate_sum = current_sum + cell_total;
				float candidate_actual = candidate_sum - current_first_pre - cells_[idx].cell.pad.post;

				if (candidate_actual <= max_minor) {
					current_sum = candidate_sum;
					post_indices.push_back(idx);
				} else {
					break;
				}
			}
			std::ranges::reverse(post_indices);


			for (std::size_t i = 0; i < valid_split; ++i) {
				add_exposed(cells_[i].element, &cells_[i], inner_sizes[i]);
			}
			if (overflow_elem_) {
				add_exposed(overflow_elem_.get(), &overflow_cell_, e_inner);
			}
			for (std::size_t idx : post_indices) {
				add_exposed(cells_[idx].element, &cells_[idx], inner_sizes[idx]);
			}
		}


		if (has_any) {
			info.masterings -= first_pad_pre;
			info.masterings -= last_pad_post;
		}


		if (info.is_overflowed && info.masterings > max_minor) {
			info.requires_scissor = true;
		}

		return info;
	}

	std::optional<math::vec2> pre_acquire_size_impl(layout::optional_mastering_extent extent) override {
		switch(get_layout_policy()){
		case layout::layout_policy::hori_major:
			if(extent.width_pending()) return std::nullopt;
			break;
		case layout::layout_policy::vert_major:
			if(extent.height_pending()) return std::nullopt;
			break;
		case layout::layout_policy::none:
			if(extent.fully_mastering()) return extent.potential_extent();
			return std::nullopt;
		default: std::unreachable();
		}

		if(cells_.empty()) return std::nullopt;

		auto potential = extent.potential_extent();
		const auto dep = extent.get_pending();
		auto [majorTargetDep, minorTargetDep] = layout::get_vec_ptr<bool>(get_layout_policy());

		if(dep.*minorTargetDep){
			auto [majorTarget, minorTarget] = layout::get_vec_ptr(get_layout_policy());

			if(get_expand_policy() == layout::expand_policy::passive){
				potential.*minorTarget = this->content_extent().*minorTarget;
			} else {
				float major_size = potential.*majorTarget;
				float minor_scaling = get_scaling().*minorTarget;

				float max_minor = potential.*minorTarget;


				auto info = calculate_overflow_layout(major_size, minor_scaling, max_minor);
				potential.*minorTarget = info.masterings;
			}
		}

		if(auto pref = get_prefer_content_extent(); pref && get_expand_policy() == layout::expand_policy::prefer){
			potential.max(pref.value());
		}

		return potential;
	}
};

}
