//
// Created by Matrix on 2025/11/14.
//

export module mo_yanxi.gui.elem.two_segment_elem;

export import mo_yanxi.gui.infrastructure;
import std;

namespace mo_yanxi::gui{

export
struct two_segment_elem : elem{
protected:
	std::array<elem_ptr, 2> items{};
	std::array<layout::stated_size, 2> item_size{};
	layout::layout_policy layout_policy_{};

	float pad_ = 8;
	bool transpose_head_and_body_{};

	virtual void set_item_size(bool isContent, layout::stated_size size){
		if(util::try_modify(item_size[isContent], size)){
			notify_isolated_layout_changed();
		}
	}

public:
	[[nodiscard]] two_segment_elem(scene& scene, elem* parent)
	: elem(scene, parent){
	}

	[[nodiscard]] two_segment_elem(scene& scene, elem* parent, layout::layout_policy layout_policy)
	: elem(scene, parent),
	layout_policy_(layout_policy){
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

	[[nodiscard]] layout::layout_policy get_layout_policy() const noexcept{
		return layout_policy_;
	}

	void set_layout_policy(const layout::layout_policy layout_policy){
		//TODO ban none
		if(util::try_modify(layout_policy_, layout_policy)){
			notify_isolated_layout_changed();
		}
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

	[[nodiscard]] std::span<const elem_ptr> children() const noexcept final{
		return items;
	}

protected:
	void draw_content_impl(const rect clipSpace) const override{
		draw_style();
		const auto space = content_bound_abs().intersection_with(clipSpace);

		items[0]->try_draw(space);
		items[1]->try_draw(space);
	}

	void draw_background_impl(const rect clipSpace) const override{
		draw_style_background();
		const auto space = content_bound_abs().intersection_with(clipSpace);

		items[0]->try_draw_background(space);
		items[1]->try_draw_background(space);
	}
public:

	bool update(float delta_in_ticks) override{
		if(!elem::update(delta_in_ticks))return false;

		for(auto& item : items){
			item->update(delta_in_ticks);
		}
		return true;
	}

protected:
	template <std::derived_from<elem> E, typename... Args>
		requires (std::constructible_from<E, scene&, elem*, Args...>)
	E& emplace(bool as_content, Args&&... args){
		items[as_content] = elem_ptr{get_scene(), this, std::in_place_type<E>, std::forward<Args>(args)...};
		notify_isolated_layout_changed();
		return static_cast<E&>(*items[as_content]);
	}

	template <invocable_elem_init_func Fn, typename... Args>
	auto& create(
		bool as_content,
		Fn&& init,
		Args&&... args
	){
		items[as_content] = elem_ptr{get_scene(), this, std::forward<Fn>(init), std::forward<Args>(args)...};
		notify_isolated_layout_changed();
		return static_cast<elem_init_func_create_t<Fn>&>(*items[as_content]);
	}

	template <std::derived_from<elem> E, typename... Args>
		requires (std::constructible_from<E, scene&, elem*, Args...>)
	E& emplace_head(Args&&... args){
		return this->emplace<E>(false, std::forward<Args>(args)...);
	}

	template <std::derived_from<elem> E, typename... Args>
		requires (std::constructible_from<E, scene&, elem*, Args...>)
	E& emplace_content(Args&&... args){
		return this->emplace<E>(true, std::forward<Args>(args)...);
	}

	template <invocable_elem_init_func Fn, typename... Args>
	auto& create_head(Fn&& init, Args&&... args){
		return this->create(false, std::forward<Fn>(init), std::forward<Args>(args)...);
	}

	template <invocable_elem_init_func Fn, typename... Args>
	auto& create_content(Fn&& init, Args&&... args){
		return this->create(true, std::forward<Fn>(init), std::forward<Args>(args)...);
	}


	[[nodiscard]] std::optional<layout::layout_policy> search_layout_policy_getter_impl() const noexcept final{
		return get_layout_policy();
	}

	bool update_abs_src(math::vec2 parent_content_src) noexcept final{
		if(elem::update_abs_src(parent_content_src)){
			set_children_src();
			return true;
		}

		return false;
	}

	virtual void set_children_src() const{
		auto [_, minor] = layout::get_vec_ptr(layout_policy_);

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
		auto [majorTarget, minorTarget] = layout::get_vec_ptr(layout_policy_);

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
		auto [majorTarget, minorTarget] = layout::get_vec_ptr(layout_policy_);

		bool majorPending{restriction_extent.get_pending().*layout::get_vec_ptr<bool>(layout_policy_).major};


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
				item->restriction_extent.set_minor_pending(layout_policy_);
			}

			if(majorPending){
				item->restriction_extent.set_major_pending(layout_policy_);
			}
		}

		set_children_src();
	}
};
}
