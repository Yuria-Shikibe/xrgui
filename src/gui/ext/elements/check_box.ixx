//

//

export module mo_yanxi.gui.elem.check_box;

import std;
import mo_yanxi.gui.elem.value_selector;
import mo_yanxi.gui.region_drawable.derives;

namespace mo_yanxi::gui{

export
template <unsigned MaxSize>
struct select_box : dispersed_value_selector<elem, MaxSize>{
	static constexpr std::size_t max_size = MaxSize;
	static_assert(max_size >= 2);
	using pass_type = std::conditional_t<(max_size > 2), unsigned, bool>;




public:
	using icon_type = icon<component::vertex_color, component::batch_draw_mode, component::draw_switch>;
	std::array<icon_type, max_size> icons{};
	std::array<graphic::color, max_size> mul_color{[]{
		std::array<graphic::color, max_size> colors;
		colors.fill(graphic::colors::white);
		return colors;
	}()};

	[[nodiscard]] select_box(scene& scene, elem* parent)
		: dispersed_value_selector<elem, MaxSize>(scene, parent){
	}


	void record_draw_layer(draw_recorder& call_stack_builder) const override{
		elem::record_draw_layer(call_stack_builder);
		call_stack_builder.push_call_noop(*this, [](const select_box& s, const draw_call_param& param){
			if(!param.layer_param.is_top()) return;
			if(!util::is_draw_param_valid(s, param))return;
			const float opacityScl = util::get_final_draw_opacity(s, param);

			auto& elem_s = static_cast<const elem&>(s);

			auto idx = s.get_current_value();
			const icon_type& i = s.icons[idx];
			graphic::color color = s.mul_color[idx];

			auto bound = elem_s.content_bound_abs();
			auto drawext = bound.extent();
			auto off = bound.src;
			if(auto ext = i.get_preferred_extent()){
				drawext = align::embed_to(align::scale::fit, ext, drawext);
				off = align::get_offset_of(align::pos::center, drawext, bound);
			}

			i.draw(elem_s.renderer(), {off, drawext}, color.mul_a(opacityScl));
		});
	}
};

export
struct check_box : select_box<2>{
	private:
		react_flow::node_holder<react_flow::provider_cached<pass_type>> prov_;

protected:
	void on_selected_val_updated(unsigned value) override{
		prov_->update_value(value);
	}

public:
	react_flow::provider_cached<pass_type>& get_prov() noexcept{
		return prov_.node;
	}

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
