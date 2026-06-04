import std;

import mo_yanxi.gui.cfg.default_application;
import mo_yanxi.gui.elem.button;
import mo_yanxi.gui.elem.label;
import mo_yanxi.gui.elem.sequence;
import mo_yanxi.gui.elem.slider;
import mo_yanxi.gui.elem.text_edit;

namespace{

struct hello_app : mo_yanxi::gui::cfg::default_application{
	using default_application::default_application;

private:
	int click_count_{};
	mo_yanxi::gui::label* counter_label_{};

	void build_gui(mo_yanxi::gui::scene& scene, mo_yanxi::gui::loose_group& root) override{
		namespace gui = mo_yanxi::gui;

		auto content = scene.create<gui::sequence>();
		auto& column = static_cast<gui::sequence&>(root.insert(0, std::move(content)));

		column.set_fill_parent({true, true});
		column.set_layout_spec(gui::layout::layout_policy::hori_major);
		column.set_self_border(gui::border_t{}.set(24));
		column.set_style();
		column.template_cell.set_size(56);
		column.template_cell.set_pad({8.f, 8.f});

		auto title = column.create_back([](gui::label& label){
			label.set_style();
			label.set_fit_type(gui::label_fit_type::scl);
			label.text_entire_align = gui::align::pos::center_left;
			label.set_text("XRGUI Hello");
		});
		title.cell().set_size(72);

		auto counter = column.create_back([this](gui::label& label){
			counter_label_ = &label;
			label.set_style(gui::style::family_variant::base_only);
			label.set_fit_type(gui::label_fit_type::scl);
			label.text_entire_align = gui::align::pos::center_left;
			label.set_text("Clicks: 0");
		});
		counter.cell().set_size(56);

		auto button = column.create_back([this](gui::button<gui::label>& button){
			button.set_style(gui::style::family_variant::accent);
			button.set_fit_type(gui::label_fit_type::scl);
			button.text_entire_align = gui::align::pos::center;
			button.set_text("Click");
			button.set_button_callback([this]{
				++click_count_;
				if(counter_label_ != nullptr){
					counter_label_->set_text(std::format("Clicks: {}", click_count_));
				}
			});
		});
		button.cell().set_size(60);

		auto slider_label = column.create_back([](gui::label& label){
			label.set_style();
			label.set_fit_type(gui::label_fit_type::scl);
			label.text_entire_align = gui::align::pos::center_left;
			label.set_text("Slider");
		});
		slider_label.cell().set_size(44);

		auto slider = column.emplace_back<gui::slider1d_with_output>();
		slider->set_style();
		slider->set_smooth_drag(true);
		slider->set_smooth_scroll(true);
		slider->bar_handle_extent = {40.f};
		slider.cell().set_size(52);

		auto input_label = column.create_back([](gui::label& label){
			label.set_style();
			label.set_fit_type(gui::label_fit_type::scl);
			label.text_entire_align = gui::align::pos::center_left;
			label.set_text("Text input");
		});
		input_label.cell().set_size(44);

		auto input = column.emplace_back<gui::text_edit_prov>();
		input->set_style(gui::style::family_variant::base_only);
		input->set_hint_text(U"Type here");
		input->set_on_changed_interval(30.f);
		input.cell().set_size(96);
	}
};

}

int main(int argc, char** argv){
	return mo_yanxi::gui::cfg::run_default_application<hello_app>(
		argc,
		argv,
		{.app_name = "XRGUI Hello"});
}
