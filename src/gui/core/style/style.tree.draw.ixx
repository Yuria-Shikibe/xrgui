module;

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.style.tree.draw;

import std;
import mo_yanxi.math;
export import mo_yanxi.gui.style.tree;
export import mo_yanxi.gui.style.palette;
export import mo_yanxi.gui.fx.instruction_extension;
import mo_yanxi.react_flow.flexible_value;
import mo_yanxi.gui.infrastructure;

namespace mo_yanxi::gui::style::primitives{

export
struct nine_patch_draw_entry{
	react_flow::flexible_value_holder<image_nine_region> patch{};
	react_flow::flexible_value_holder<palette> pal{};
};

export
struct draw_nine_patch{
	nine_patch_draw_entry entry;

	void operator()(const typed_draw_param<elem>& p) const{
		if(!entry.patch->image_view) return;
		const elem& e = p.subject();
		auto& r = e.renderer();
		r.update_state(fx::push_constant{fx::batch_draw_mode::msdf});
		auto color = entry.pal->on_instance(e).mul_a(p->opacity_scl);
		r << fx::nine_patch_draw<>{
			.patch = std::to_address(entry.patch),
			.region = p->draw_bound,
			.color = color,
		};
	}
};

export
struct draw_nine_patch_hollow{
	nine_patch_draw_entry entry;

	void operator()(const typed_draw_param<elem>& p) const{
		if(!entry.patch->image_view) return;
		const elem& e = p.subject();
		auto& r = e.renderer();

		r.update_state(fx::push_constant{fx::batch_draw_mode::msdf});
		auto color = entry.pal->on_instance(e).mul_a(p->opacity_scl);
		r << fx::nine_patch_hollow_draw<>{
			.patch = std::to_address(entry.patch),
			.region = p->draw_bound,
			.color = color,
		};
	}
};

export
struct draw_row_patch{
	react_flow::flexible_value_holder<image_row_patch> patch{};
	react_flow::flexible_value_holder<palette> pal{};
	graphic::draw::instruction::row_patch_flags flags{};

	void operator()(const typed_draw_param<elem>& p) const{
		if(!patch->get_image_view()) return;
		const elem& e = p.subject();
		auto& r = e.renderer();
		e.renderer().update_state(fx::push_constant{fx::batch_draw_mode::msdf});
		auto color = pal->on_instance(e).mul_a(p->opacity_scl);
		e.renderer() << fx::row_patch_draw{
			.patch = std::to_address(patch),
			.region = p->draw_bound,
			.color = color,
			.flags = flags,
		};
	}
};

}
