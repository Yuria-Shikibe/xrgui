module;

#include <cassert>

export module mo_yanxi.gui.compound.named_slider;

import std;

export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.gui.elem.head_body_elem;
export import mo_yanxi.gui.elem.slider;
export import mo_yanxi.gui.elem.label_v2;
export import mo_yanxi.graphic.color;

import mo_yanxi.gui.util.observable_value;
import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.gui.fx.fringe;

namespace mo_yanxi::gui::cpd{

export
struct named_slider : head_body{

	named_slider(scene& scene, elem* parent, layout::layout_policy layout_policy, std::string_view text, const float bar_size)
		: head_body(scene, parent, layout_policy){
		create_head([&](label_v2& l){
			l.set_style();
			l.set_text(text);
		});

		create_body([&](slider_with_output& s){
			s.set_style();
			s.set_clamp_from_layout_policy(layout_policy);
		});

		set_head_size({layout::size_category::pending, 1});
		set_body_size({layout::size_category::mastering, bar_size});
	}

	named_slider(scene& scene, elem* parent)
		: named_slider(scene, parent, layout::layout_policy::hori_major, "Slider", 60){
	}


	[[nodiscard]] label_v2& head() const noexcept{
		return elem_cast<label_v2>(head_body::head());
	}

	[[nodiscard]] slider_with_output& body() const noexcept{
		return elem_cast<slider_with_output>(head_body::body());
	}

	auto& get_slider_provider() const noexcept{
		return body().get_provider();
	}

	void set_progress(float value){
		body().set_progress(value);
	}
};

}