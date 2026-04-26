export module mo_yanxi.gui.elem.sequence;

export import mo_yanxi.gui.elem.celled_group;
import std;

namespace mo_yanxi::gui{


struct sequence_pre_layout_result{
	float masterings;
	float passive;
};

//TODO when is passive and mastering exceeded the given content size, set a scissor

export
struct sequence : celled_group<cell_adaptor<layout::partial_mastering_cell>>{
private:
	layout::directional_layout_specifier policy_{util::cache_layout_specifier_from_parent(
		layout::directional_layout_specifier::identity(),
		search_parent_layout_policy(true)
	)};
	bool align_to_tail_{false};
	layout::expand_policy expand_policy_{};

public:
	[[nodiscard]] sequence(scene& scene, elem* parent, const layout::directional_layout_specifier policy)
		: universal_group(scene, parent),
		  policy_(policy){
	}

	[[nodiscard]] sequence(scene& scene, elem* parent)
		: sequence(scene, parent, layout::directional_layout_specifier::fixed(layout::layout_policy::hori_major)){
	}

protected:
	std::optional<math::vec2> pre_acquire_size_impl(layout::optional_mastering_extent extent) override;

public:
	[[nodiscard]] layout::directional_layout_specifier get_layout_specifier() const noexcept{
		return policy_;
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

	void set_align_to_tail(bool align_to_tail){
		if(util::try_modify(align_to_tail_, align_to_tail)){
			notify_isolated_layout_changed();
		}
	}

	[[nodiscard]] layout::layout_policy get_layout_policy() const noexcept override{
		return policy_.self();
	}

	[[nodiscard]] bool is_align_to_tail() const noexcept{
		return align_to_tail_;
	}

	[[nodiscard]] layout::expand_policy get_expand_policy() const noexcept{
		return expand_policy_;
	}

	void layout_elem() override;

protected:
	bool set_layout_policy_impl(const layout::layout_policy_setting setting) override{
		return util::update_layout_policy_setting(
			setting,
			policy_,
			[this]{ return util::layout_policy_or_none(search_parent_layout_policy(true)); },
			[](const layout::layout_policy parent_policy, const layout::layout_specifier specifier){
				return layout::directional_layout_specifier{specifier}.cache_from(parent_policy);
			},
			[this](const layout::layout_policy policy){
				return policy_.cache_from(policy);
			},
			[this](const auto&){
				notify_isolated_layout_changed();
			}
		);
	}
};
}
