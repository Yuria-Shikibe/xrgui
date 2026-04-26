module;

#include <cassert>

export module mo_yanxi.gui.elem.head_body_elem;

export import mo_yanxi.gui.infrastructure;
import std;

namespace mo_yanxi::gui{

//TODO head base has the potential to become a static generic type

export
struct head_body_base : elem{
protected:
	std::array<elem_ptr, 2> items{};
	std::array<layout::stated_size, 2> item_size{};
	layout::directional_layout_specifier layout_policy_{layout::directional_layout_specifier::fixed(layout::layout_policy::hori_major)};

	float pad_ = 8;
	bool transpose_head_and_body_{};

	virtual void set_item_size(bool isContent, layout::stated_size size){
		if(util::try_modify(item_size[isContent], size)){
			notify_isolated_layout_changed();
		}
	}

public:
	[[nodiscard]] head_body_base(scene& scene, elem* parent)
	: elem(scene, parent){
		interactivity = interactivity_flag::children_only;
	}

	[[nodiscard]] head_body_base(scene& scene, elem* parent, const layout::directional_layout_specifier layout_policy)
	: elem(scene, parent),
	layout_policy_(layout_policy){
		interactivity = interactivity_flag::children_only;
	}


	[[nodiscard]] elem& head() const noexcept{
		return *items[0];
	}

	[[nodiscard]] elem& body() const noexcept{
		return *items[1];
	}

	void set_head_size(layout::stated_size size){
		set_item_size(false, size);
	}

	void set_body_size(layout::stated_size size){
		set_item_size(true, size);
	}

	void set_head_size(float size){
		set_item_size(false, {layout::size_category::mastering, size});
	}

	void set_body_size(float size){
		set_item_size(true, {layout::size_category::mastering, size});
	}

	[[nodiscard]] layout::layout_policy get_layout_policy() const noexcept final{
		return layout_policy_.self();
	}

	[[nodiscard]] layout::layout_specifier get_layout_specifier() const noexcept{
		return layout_policy_;
	}

	[[nodiscard]] float get_pad() const noexcept{
		return pad_;
	}

	void set_pad(const float pad){
		if(util::try_modify(pad_, pad)){
			notify_isolated_layout_changed();
		}
	}

	[[nodiscard]] bool is_head_body_transpose() const noexcept{
		return transpose_head_and_body_;
	}

	void set_head_body_transpose(const bool transpose_head_and_content){
		if(util::try_modify(transpose_head_and_body_, transpose_head_and_content)){
			set_children_src();
		}
	}

	[[nodiscard]] elem_span exposed_children() const noexcept final{
		return {items, elem_ptr::cvt_mptr};
	}

public:
	void record_draw_layer(draw_recorder& call_stack_builder) const override{
		elem::record_draw_layer(call_stack_builder);
		call_stack_builder.push_call_enter(
			*this, [](const elem& s, const draw_call_param& p, draw_call_stack&) static -> draw_call_param{
				const auto space = s.content_bound_abs().intersection_with(p.draw_bound);
				return {
						.current_subject = &s,
						.draw_bound = space,
						.opacity_scl = p.opacity_scl * s.get_local_draw_opacity(),
						.layer_param = p.layer_param
					};
			});

		items[0]->record_draw_layer(call_stack_builder);
		items[1]->record_draw_layer(call_stack_builder);

		call_stack_builder.push_call_leave();
	}

	rect get_seperator_region_element_local() const noexcept{
		const auto [major, minor] = layout::get_vec_ptr(get_layout_policy());
		auto head_extent = head().extent();

		math::vec2 region_ext;
		math::vec2 offset{};
		region_ext.*major = extent().*major - boarder().extent().* major;
		region_ext.*minor = pad_;

		offset.*minor += head_extent.*minor;

		return rect{tags::from_extent, offset, region_ext};
	}


protected:
	bool set_layout_policy_impl(const layout::layout_policy_setting setting) override{
		return util::update_layout_policy_setting(
			setting,
			layout_policy_,
			[this]{ return util::layout_policy_or_none(search_parent_layout_policy(true)); },
			[](const layout::layout_policy parent_policy, const layout::layout_specifier specifier){
				return layout::directional_layout_specifier{specifier}.cache_from(parent_policy);
			},
			[this](const layout::layout_policy policy){
				return layout_policy_.cache_from(policy);
			},
			[this](const layout::directional_layout_specifier candidate){
				on_layout_policy_changed(candidate.self());
			}
		);
	}

	template <std::derived_from<elem> E, typename... Args>
		requires (std::constructible_from<E, scene&, elem*, Args...>)
	E& emplace(bool as_body, Args&&... args){
		items[as_body] = elem_ptr{get_scene(), this, std::in_place_type<E>, std::forward<Args>(args)...};
		notify_isolated_layout_changed();
		return static_cast<E&>(*items[as_body]);
	}

	template <invocable_elem_init_func Fn, typename... Args>
	auto& create(
		bool as_body,
		Fn&& init,
		Args&&... args
	){
		items[as_body] = elem_ptr{get_scene(), this, std::forward<Fn>(init), std::forward<Args>(args)...};
		notify_isolated_layout_changed();
		return static_cast<elem_init_func_create_t<Fn>&>(*items[as_body]);
	}

	template <std::derived_from<elem> E, typename... Args>
		requires (std::constructible_from<E, scene&, elem*, Args...>)
	E& emplace_head(Args&&... args){
		return this->emplace<E>(false, std::forward<Args>(args)...);
	}

	template <std::derived_from<elem> E, typename... Args>
		requires (std::constructible_from<E, scene&, elem*, Args...>)
	E& emplace_body(Args&&... args){
		return this->emplace<E>(true, std::forward<Args>(args)...);
	}

	template <invocable_elem_init_func Fn, typename... Args>
	auto& create_head(Fn&& init, Args&&... args){
		return this->create(false, std::forward<Fn>(init), std::forward<Args>(args)...);
	}

	template <invocable_elem_init_func Fn, typename... Args>
	auto& create_body(Fn&& init, Args&&... args){
		return this->create(true, std::forward<Fn>(init), std::forward<Args>(args)...);
	}

	template <typename E = elem>
	auto& set_elem(bool as_body, elem_ptr&& item){
		items[as_body] = std::move(item);
		notify_isolated_layout_changed();
		return static_cast<E&>(*items[as_body]);
	}

	template <typename E = elem>
	auto& set_body_elem(elem_ptr&& item){
		return set_elem<E>(true, std::move(item));
	}

	template <typename E = elem>
	auto& set_head_elem(elem_ptr&& item){
		return set_elem<E>(false, std::move(item));
	}

	bool update_abs_src(math::vec2 parent_content_src) noexcept final{
		if(elem::update_abs_src(parent_content_src)){
			set_children_src();
			return true;
		}

		return false;
	}

	virtual void set_children_src() const{
		assert(items[0] != nullptr);
		assert(items[1] != nullptr);
		auto [_, minor] = layout::get_vec_ptr(get_layout_policy());

		auto sz = items[transpose_head_and_body_]->extent();
		math::vec2 relOff{};
		relOff.*minor += pad_ + sz.*minor;
		items[transpose_head_and_body_]->set_rel_pos({});
		items[!transpose_head_and_body_]->set_rel_pos(relOff);

		auto src = content_src_pos_abs();
		items[transpose_head_and_body_]->update_abs_src(src);
		items[!transpose_head_and_body_]->update_abs_src(src);
	}

	struct layout_minor_dim_conifg{
		float masterings;
		float passive;
		std::array<float, 2> size;
	};

	[[nodiscard]] layout_minor_dim_conifg get_layout_minor_dim_config(
		float layout_major_size
	) const{
		auto [majorTarget, minorTarget] = layout::get_vec_ptr(get_layout_policy());

		const float minorScaling = get_scaling().*minorTarget;
		layout_minor_dim_conifg result{};

		for(unsigned i = 0; i < items.size(); ++i){
			auto& item = items[i];
			const auto& size = item_size[i];

			switch(size.type){
			case layout::size_category::pending:{
				math::vec2 vec;
				vec.*majorTarget = layout_major_size;
				vec.*minorTarget = std::numeric_limits<float>::infinity();

				auto rst = item->pre_acquire_size(vec).value_or({}).*minorTarget;
				result.size[i] = rst;
				result.masterings += rst;
				break;
			}
			case layout::size_category::mastering:{
				float value = size.value * minorScaling;
				result.size[i] = value;
				result.masterings += value;
				break;
			}
			case layout::size_category::passive:{
				result.size[i] = size.value;
				result.passive += size.value;
				break;
			}
			case layout::size_category::scaling:{
				float value = size.value * layout_major_size;

				result.size[i] = value;
				result.masterings += value;
				break;
			}
			}
		}

		result.masterings += pad_;

		return result;
	}

	void layout_children(layout::expand_policy expand_policy_){
		auto [majorTarget, minorTarget] = layout::get_vec_ptr(get_layout_policy());

		bool majorPending{restriction_extent.get_pending().*layout::get_vec_ptr<bool>(get_layout_policy()).major};


		auto content_sz = content_extent();
		const auto minor_config = get_layout_minor_dim_config(content_sz.*majorTarget);

		if(expand_policy_ != layout::expand_policy::passive){
			math::vec2 size;
			size.*majorTarget = content_sz.*majorTarget;
			size.*minorTarget = minor_config.masterings;
			size += boarder().extent();

			if(expand_policy_ == layout::expand_policy::prefer){
				size.max(get_prefer_extent().value_or({}));
			}

			size.min(restriction_extent.potential_extent());
			resize(size, propagate_mask::force_upper);

			content_sz = content_extent();
		}

		const auto remains = std::fdim(content_sz.*minorTarget, minor_config.masterings);
		const auto passive_unit = remains / minor_config.passive;

		for (auto&& [idx, item] : items | std::views::enumerate){
			auto minor = minor_config.size[idx];
			if(item_size[idx].type == layout::size_category::passive)minor *= passive_unit;
			math::vec2 cell_sz;
			cell_sz.*majorTarget = content_sz.*majorTarget;
			cell_sz.*minorTarget = minor;
			item->resize(cell_sz, propagate_mask::lower);
			item->try_layout();

			if(item_size[idx].pending()){
				item->restriction_extent.set_minor_pending(get_layout_policy());
			}

			if(majorPending){
				item->restriction_extent.set_major_pending(get_layout_policy());
			}
		}

		set_children_src();
	}


	virtual void on_layout_policy_changed(const layout::layout_policy layout_policy){
		notify_isolated_layout_changed();
	}
};

export struct head_body : head_body_base{
	using head_body_base::head_body_base;

private:
	layout::expand_policy expand_policy_{};

public:
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


	std::optional<math::vec2> pre_acquire_size_impl(layout::optional_mastering_extent extent) override{

		if(get_layout_policy() == layout::layout_policy::none){
			if(extent.fully_mastering()) return extent.potential_extent();
			return std::nullopt;
		}

		auto [majorTargetDep, minorTargetDep] = layout::get_vec_ptr<bool>(get_layout_policy());
		const auto dep = extent.get_pending();
		auto [majorTarget, minorTarget] = layout::get_vec_ptr(get_layout_policy());


		if(dep.*majorTargetDep){

			if(dep.*minorTargetDep) return std::nullopt;


			assert(items[0] != nullptr);
			assert(items[1] != nullptr);

			const auto& sz0 = item_size[0];
			const auto& sz1 = item_size[1];


			if(sz0.type == layout::size_category::scaling || sz0.type == layout::size_category::pending ||
			   sz1.type == layout::size_category::scaling || sz1.type == layout::size_category::pending){
				return std::nullopt;
			}

			const float known_minor = extent.potential_extent().*minorTarget;
			const float available_minor = std::max(0.0f, known_minor - pad_);
			float minor_alloc[2]{};


			const float minorScaling = get_scaling().*minorTarget;
			const float m0 = sz0.value * minorScaling;
			const float m1 = sz1.value * minorScaling;


			if(sz0.type == layout::size_category::mastering && sz1.type == layout::size_category::passive){
				minor_alloc[0] = m0;
				minor_alloc[1] = math::fdim(available_minor, m0);
			} else if(sz1.type == layout::size_category::mastering && sz0.type == layout::size_category::passive){
				minor_alloc[1] = m1;
				minor_alloc[0] = math::fdim(available_minor, m1);
			} else if(sz0.type == layout::size_category::passive && sz1.type == layout::size_category::passive){

				const float sum = sz0.value + sz1.value;
				if(sum > 0){
					minor_alloc[0] = available_minor * (sz0.value / sum);
					minor_alloc[1] = available_minor * (sz1.value / sum);
				} else {
					minor_alloc[0] = available_minor * 0.5f;
					minor_alloc[1] = available_minor * 0.5f;
				}
			} else if(sz0.type == layout::size_category::mastering && sz1.type == layout::size_category::mastering){
				minor_alloc[0] = m0;
				minor_alloc[1] = m1;
			}

			float max_major = 0.0f;
			bool has_valid_child = false;


			for(unsigned i = 0; i < 2; ++i){
				math::vec2 child_ext_vec;
				child_ext_vec.*majorTarget = layout::pending_size;
				child_ext_vec.*minorTarget = minor_alloc[i];

				auto child_res = items[i]->pre_acquire_size(child_ext_vec);


				if(child_res){
					has_valid_child = true;
					max_major = std::max(max_major, child_res.value().*majorTarget);
				}
			}


			if(!has_valid_child) return std::nullopt;

			math::vec2 result;
			result.*majorTarget = max_major;
			result.*minorTarget = known_minor;


			if(auto pref = get_prefer_content_extent(); pref && get_expand_policy() == layout::expand_policy::prefer){
				result.max(pref.value());
			}

			return result;
		}


		auto potential = extent.potential_extent();

		if(dep.*minorTargetDep){
			if(get_expand_policy() == layout::expand_policy::passive){
				potential.*minorTarget = this->content_extent().*minorTarget;
			} else{
				potential.*minorTarget = get_layout_minor_dim_config(potential.*majorTarget).masterings;
			}
		}

		if(auto pref = get_prefer_content_extent(); pref && get_expand_policy() == layout::expand_policy::prefer){
			potential.max(pref.value());
		}

		return potential;
	}

	void layout_elem() override{
		elem::layout_elem();
		layout_children(expand_policy_);

		for(auto& item : items){
			item->try_layout();
		}
	}
};

export struct head_body_no_invariant : head_body{
	using head_body::head_body;

	using head_body::create_body;
	using head_body::create_head;

	using head_body::emplace_body;
	using head_body::emplace_head;

	using head_body::set_body_elem;
	using head_body::set_head_elem;

	using head_body::emplace;
	using head_body::create;
	using head_body::set_elem;

};
}
