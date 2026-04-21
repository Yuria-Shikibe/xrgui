//
// Created by Matrix on 2026/4/18.
//

export module mo_yanxi.gui.compound.click_collapser;

import std;
import mo_yanxi.gui.elem.collapser;
import mo_yanxi.gui.elem.arrow_elem;
import mo_yanxi.math;

namespace mo_yanxi::gui::cpd{
export
struct click_collapser : collapser{
private:
	math::range get_rot_angle() const noexcept{
		switch(get_layout_policy()){

		case layout::layout_policy::vert_major:
			return {math::pi, 0};
		default:
			return {-math::pi_half, math::pi_half};
		}
	}
public:

	head_body& head() const{
		return elem_cast<head_body, false>(collapser::head());
	}

	arrow_rotor& get_arrow() const{
		return elem_cast<arrow_rotor, false>(head().head());
	}

	[[nodiscard]] click_collapser(scene& scene, elem* parent, layout::layout_policy layout_policy)
		: collapser(scene, parent, layout_policy){
		interactivity = interactivity_flag::children_only;
		settings.expand_enter_spacing = 0;
		settings.expand_exit_spacing = 0;
		set_expand_cond(collapser_expand_cond::click);
	}

	[[nodiscard]] click_collapser(scene& scene, elem* parent)
		: click_collapser(scene, parent, layout::layout_policy::hori_major){
	}

	template <elem_create_pacakge HeadPackage, elem_create_pacakge BodyPackage>
	[[nodiscard]] click_collapser(scene& scene, elem* parent, layout::layout_policy layout_policy,
		HeadPackage&& hp, BodyPackage&& bp)
		: click_collapser(scene, parent, layout_policy){
		this->create_head([&](head_body_no_invariant& hb){
			hb.set_expand_policy(layout::expand_policy::passive);
			hb.interactivity = interactivity_flag::enabled;
			hb.sync_run([](elem& e){
				e.set_style(e.get_style_manager().get_default<style::elem_style_drawer>(style::family_variant::base_only));
			});

			hb.create_head([&](arrow_rotor& a){
				a.rotate_angle = get_rot_angle();
				a.set_style();
			});

			hb.set_head_size({layout::size_category::scaling});

			hb.create_body(std::forward<HeadPackage>(hp));

		}, layout::transpose_layout(layout_policy));

		this->create_body(std::forward<BodyPackage>(bp));
	}

	bool set_layout_policy(layout::layout_policy layout_policy){
		if(collapser::set_layout_policy(layout_policy)){
			head().set_layout_policy(layout::transpose_layout(layout_policy));
			return true;
		}else{
			return false;
		}
	}

	events::op_afterwards on_click(const events::click event, std::span<elem* const> aboves) override{
		auto rst = collapser::on_click(event, aboves);

		if(is_clicked())get_arrow().arrow_enter(std::identity{});
		else get_arrow().arrow_exit(std::identity{});

		return rst;
	}

};
}
