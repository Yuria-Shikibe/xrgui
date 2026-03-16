//
// Created by Matrix on 2026/3/16.
//

export module mo_yanxi.gui.elem.check_box;

import std;
import mo_yanxi.gui.elem.value_selector;
import mo_yanxi.gui.region_drawable.derives;

namespace mo_yanxi::gui{

export
template <unsigned MaxSize>
struct select_box : binary_value_selector<elem>{
	static constexpr std::size_t max_size = MaxSize;
	static_assert(max_size >= 2);
	using pass_type = std::conditional_t<(max_size > 2), unsigned, bool>;

private:
	react_flow::node_holder<react_flow::provider_cached<pass_type>> prov_;

public:
	std::array<icon<component::vertex_color, component::batch_draw_mode, component::draw_switch>, max_size> icons{};
	std::array<graphic::color, max_size> mul_color{graphic::colors::white, graphic::colors::white};

	[[nodiscard]] select_box(scene& scene, elem* parent)
		: value_selector(scene, parent){
	}

protected:
	void on_selected_val_updated(unsigned value) override{
		prov_->update_value(value);
	}

public:
	react_flow::provider_cached<pass_type>& get_prov() noexcept{
		return prov_.node;
	}

	void draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const override{
		elem::draw_layer(clipSpace, param);
		if(!param.is_top())return;

		auto idx = get_current_value();
		icons[idx].draw(renderer(), this->content_bound_abs(), mul_color[idx].copy().mul_a(get_draw_opacity()));
	}
};

export
struct check_box : select_box<2>{
	[[nodiscard]] check_box(scene& scene, elem* parent)
		: select_box<2>(scene, parent){
	}

	[[nodiscard]] check_box(scene& scene, elem* parent, std::in_place_t)
		: select_box<2>(scene, parent){
		set_default_style();
	}

	void set_default_style();
};



}
