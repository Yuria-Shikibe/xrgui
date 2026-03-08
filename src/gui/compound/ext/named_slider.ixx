module;

#include <cassert>

#include "../../../../external/mo_yanxi_vulkan_wrapper/external/VulkanMemoryAllocator/src/Common.h"

export module mo_yanxi.gui.compound.named_slider;

import std;

export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.gui.elem.head_body_elem;
export import mo_yanxi.gui.elem.slider;
export import mo_yanxi.gui.elem.label_v2;
export import mo_yanxi.gui.elem.scaling_stack;
export import mo_yanxi.graphic.color;

import mo_yanxi.gui.util.observable_value;
import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.gui.fx.fringe;

namespace mo_yanxi::gui::cpd{

export
struct named_slider : head_body{
private:
	react_flow::node_pointer progress_transformer_{};
	react_flow::node_holder<label_v2_text_prov> display_text_recv_{};

public:

	named_slider(scene& scene, elem* parent, layout::layout_policy layout_policy, std::string_view text, const float bar_size)
		: head_body(scene, parent, layout_policy){

		create_head([&](label_v2& l){
			l.set_style();
			l.set_text(text);
		});

		create_body([&](scaling_stack& s){
			s.set_style();
			s.template_cell.region_scale = {0, 0, 1, 1};

			s.create_back([&](slider_with_output& slider){
				slider.bar_handle_extent.set(bar_size);
				slider.set_style();
				slider.set_clamp_from_layout_policy(layout_policy);
			});

			s.create_back([&](label_v2& value){
				value.set_style();
				value.set_fit();
				value._debug_identity = 1;
				value.text_entire_align = align::pos::center_left;
				value.set_opacity(.5f);
				value.set_text("Test");
				display_text_recv_ = {value};
			});

		});

		// create_body([&](slider_with_output& s){
		// 	s.set_style();
		// 	s.set_clamp_from_layout_policy(layout_policy);
		// });

		set_head_size({layout::size_category::pending, 1});
		set_body_size({layout::size_category::mastering, bar_size});
	}

	named_slider(scene& scene, elem* parent)
		: named_slider(scene, parent, layout::layout_policy::hori_major, "Slider", 60){
	}


	[[nodiscard]] label_v2& head() const noexcept{
		return elem_cast<label_v2>(head_body::head());
	}

	[[nodiscard]] scaling_stack& body() const noexcept{
		return elem_cast<scaling_stack>(head_body::body());
	}

	[[nodiscard]] slider_with_output& get_slider() const noexcept{
		return elem_cast<slider_with_output>(*body().children()[0]);
	}

	[[nodiscard]] label_v2& get_slider_display_label() const noexcept{
		return elem_cast<label_v2>(*body().children()[1]);
	}

	auto& get_slider_provider() const noexcept{
		return get_slider().get_provider();
	}

	auto& get_display_text_receiver() noexcept{
		return display_text_recv_.node;
	}

	react_flow::node& add_relay(react_flow::node_pointer&& node){
		progress_transformer_ = (std::move(node));
		//TODO check the node is update on pulse?
		if(progress_transformer_->get_propagate_type() == react_flow::propagate_type::pulse){
			throw std::invalid_argument{"Not implemented"};
		}

		try{
			progress_transformer_->connect_predecessor(get_slider_provider());
		}catch(...){
			progress_transformer_ = {};
			throw;
		}

		return *progress_transformer_;
	}

	template <typename T>
		requires (std::derived_from<std::remove_cvref_t<T>, react_flow::node>)
	T& add_relay(T&& node){
		return static_cast<T&>(add_relay(react_flow::node_pointer{std::in_place_type<std::remove_cvref_t<T>>, std::forward<T>(node)}));
	}

	template <std::invocable<float> Fn>
	auto& add_relay_func(Fn&& fn){
		return this->add_relay(react_flow::make_transformer(std::forward<Fn>(fn)));
	}

	template <typename T>
		requires (std::derived_from<std::remove_cvref_t<T>, react_flow::type_aware_node<std::string>>)
	T& add_formatter(T&& node){
		auto& formatter = this->request_embedded_react_node(std::forward<T>(node));
		react_flow::connect_chain(progress_transformer_ ? *progress_transformer_ : get_slider().get_provider(), formatter, get_display_text_receiver());
		return formatter;
	}

	template <typename Fn>
		requires (std::is_invocable_r_v<std::string, Fn, float>)
	auto& add_formatter_func(Fn&& function){
		return this->add_formatter(react_flow::make_transformer(std::forward<Fn>(function)));
	}

	void set_progress(float value){
		get_slider().set_progress(value);
	}
};

}