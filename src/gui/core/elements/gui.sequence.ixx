export module mo_yanxi.gui.elem.sequence;

export import mo_yanxi.gui.celled_group;
import std;

namespace mo_yanxi::gui{

	struct sequence_pre_layout_result{
		float masterings;
		float passive;
	};

	export
	struct sequence : celled_group<cell_adaptor<layout::partial_mastering_cell>>{
	private:
		layout::layout_policy policy_{search_parent_layout_policy(false).value_or(layout::layout_policy::hori_major)};
		bool align_to_tail_{false};
		layout::expand_policy expand_policy_{};

	public:
		[[nodiscard]] sequence(scene& scene, elem* parent)
			: universal_group<layout::partial_mastering_cell>(scene, parent){
		}

	protected:
		std::optional<math::vec2> pre_acquire_size_impl(layout::optional_mastering_extent extent) override;

	public:
		void set_layout_policy(layout::layout_policy policy){
			if(util::try_modify(policy_, policy)){
				notify_isolated_layout_changed();
			}
		}

		void set_expand_policy(layout::expand_policy policy){
			if(util::try_modify(expand_policy_, policy)){
				notify_isolated_layout_changed();

				if(expand_policy_ == layout::expand_policy::passive){
					layout_state.inherent_accept_mask -= propagate_mask::child;
					layout_state.intercept_lower_to_isolated = true;
				}else{
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

		[[nodiscard]] layout::layout_policy get_layout_policy() const noexcept{
			return policy_;
		}

		[[nodiscard]] bool is_align_to_tail() const noexcept{
			return align_to_tail_;
		}

		[[nodiscard]] layout::expand_policy get_expand_policy() const noexcept{
			return expand_policy_;
		}

		void layout_elem() override;

	protected:
		[[nodiscard]] std::optional<layout::layout_policy> search_layout_policy_getter_impl() const noexcept override{
			return get_layout_policy();
		}
	};
}