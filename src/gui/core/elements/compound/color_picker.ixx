module;

#include <cassert>

#ifndef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
#include <beman/inplace_vector.hpp>
#endif

export module mo_yanxi.gui.compound.color_picker;

import std;

export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.gui.elem.head_body_elem;
export import mo_yanxi.gui.elem.slider;
export import mo_yanxi.graphic.color;

import mo_yanxi.gui.util.observable_value;
import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.gui.fx.fringe;

#ifdef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
import <beman/inplace_vector.hpp>;
#endif

namespace mo_yanxi::gui{
export
struct slider_gradient_drawer : style::default_slider_drawer{
	graphic::color src;
	graphic::color dst;
	void draw_layer_impl(const slider& element, math::frect region, float opacityScl,
		fx::layer_param layer_param) const override{
		if(layer_param.is_top()){
			auto srca = src.copy().mul_a(opacityScl);
			auto dsta = dst.copy().mul_a(opacityScl);
			element.renderer() << graphic::draw::instruction::rect_aabb{
				.v00 = region.get_src(),
				.v11 = region.get_end(),
				.vert_color = {srca, dsta, srca, dsta}
			};
		}

		default_slider_drawer::draw_layer_impl(element, region, opacityScl, layer_param);
	}
};

struct hue_gradient_drawer final : gui::style::default_slider_drawer{
	void draw_layer_impl(const slider& element, math::frect region, float opacityScl,
		fx::layer_param layer_param) const override{
		if(layer_param.is_top()){
			static constexpr int segs_size = 128;
			fx::fringe::line_context context{std::in_place_type<beman::inplace_vector::inplace_vector<graphic::draw::instruction::line_node, segs_size + 4>>};

			float math::vec2::* major;
			float math::vec2::* minor;

			if(element.is_clamped_to_hori()){
				major = &math::vec2::x;
				minor = &math::vec2::y;
			}else if(element.is_clamped_to_vert()){
				major = &math::vec2::y;
				minor = &math::vec2::x;
			}else{
				throw std::invalid_argument{"invalid draw direction"};
			}

			math::vec2 start = region.get_src();
			const float stroke = region.extent().*minor;
			const float marching = region.extent().*major / (segs_size - 1);

			start.*minor += stroke * .5f;

			for(int i = 0; i < segs_size; ++i){
				auto p = start.copy();
				p.*major = math::fma(marching, i, p.*major);
				auto color = graphic::color{}.from_hsv({static_cast<float>(i) / static_cast<float>((segs_size - 1)), 1, 1}).set_a(opacityScl);
				context.push(p, stroke * .6f, color);
			}

			context.add_cap(0.1, 0.1);
			context.dump_mid(element.renderer(), graphic::draw::instruction::line_segments{});
		}

		default_slider_drawer::draw_layer_impl(element, region, opacityScl, layer_param);
	}
};

struct alpha_gradient_drawer : gui::style::default_slider_drawer{
	void draw_layer_impl(const slider& element, math::frect region, float opacityScl,
		fx::layer_param layer_param) const override{

		if(layer_param.is_top()){

			using namespace graphic::draw;

			element.renderer().push(instruction::rect_aabb{
				.v00 = region.vert_00(),
				.v11 = region.vert_11(),
				.vert_color = {graphic::colors::light_gray}
			});

			element.renderer().push(fx::slide_line_config{
				.angle = 45,
				.spacing = 8,
				.stroke = 8,
				.speed = 0,
				.phase = 8,
			});

			element.renderer().push(instruction::rect_aabb{
				.generic = {.mode = std::to_underlying(fx::primitive_draw_mode::draw_slide_line)},
				.v00 = region.vert_00(),
				.v11 = region.vert_11(),
				.vert_color = {graphic::colors::gray}
			});

			if(element.is_clamped_to_hori()){
				element.renderer().push(instruction::rect_aabb{
					.v00 = region.vert_00(),
					.v11 = region.vert_11(),
					.vert_color = {graphic::colors::light_gray, graphic::colors::light_gray.make_transparent(), graphic::colors::light_gray, graphic::colors::light_gray.make_transparent()}
				});
			}else if(element.is_clamped_to_vert()){
				element.renderer().push(instruction::rect_aabb{
					.v00 = region.vert_00(),
					.v11 = region.vert_11(),
					.vert_color = {graphic::colors::light_gray, graphic::colors::light_gray, graphic::colors::light_gray.make_transparent(), graphic::colors::light_gray.make_transparent()}
				});
			}else{
				throw std::invalid_argument{"invalid draw direction"};
			}
		}

		default_slider_drawer::draw_layer_impl(element, region, opacityScl, layer_param);
	}

};

constexpr hue_gradient_drawer hue_gradient_slider_drawer{};
constexpr alpha_gradient_drawer alpha_gradient_slider_drawer{};

namespace cpd{
template <typename ...Fns>
struct obv_slider : slider{
	util::observable_value<float, std::decay_t<Fns>...> target;

	template <typename ...FnTys>
	obv_slider(scene& scene, elem* parent, FnTys&&... fns)
		: slider(scene, parent), target(std::forward<FnTys>(fns)...){
	}

	void on_changed() override{
		target(*this, bar.get_progress().get_max());
	}
};

template <typename ...FnTys>
obv_slider(scene& scene, elem* parent, FnTys&&... fns) -> obv_slider<std::decay_t<FnTys>...>;

template <typename ...FnTys>
elem_ptr make_obv_slider(elem& parent, FnTys&&... fns){
	return elem_ptr{parent.get_scene(), &parent, std::in_place_type<obv_slider<std::decay_t<FnTys>>...>, std::forward<FnTys>(fns)...};
}

export
struct rgb_picker : head_body{
private:
	struct sv_selection : slider{
		rgb_picker& get_picker() const noexcept{
			return parent_ref<rgb_picker>();
		}

		sv_selection(scene& scene, elem* parent)
			: slider(scene, parent){
		}

		void on_changed() override{
			get_picker().set_color_sv(math::vec2{1, 1} - bar.get_progress());
		}

		void draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const override{
			elem::draw_layer(clipSpace, param);

			if(param.is_top()){
				auto hue = get_picker().hsv_.h;
				auto contentext = content_bound_abs();
				auto v00_full = contentext.vert_00() + bar_handle_extent / 2;
				auto v11_full = contentext.vert_11() - bar_handle_extent / 2;

				// 设置网格切分数量。16x16 通常能够在性能和视觉平滑度之间取得良好的平衡
				constexpr static float grid_size = 16;

				for (int iy = 0; iy < grid_size; ++iy) {

					const float ty0 = static_cast<float>(iy) / grid_size;
					const float ty1 = static_cast<float>(iy + 1) / grid_size;

					for (int ix = 0; ix < grid_size; ++ix) {
						const float tx0 = static_cast<float>(ix) / grid_size;
						const float tx1 = static_cast<float>(ix + 1) / grid_size;

						const auto sub_v00 = math::lerp(v00_full, v11_full, math::vec2{tx0, ty0});
						const auto sub_v11 = math::lerp(v00_full, v11_full, math::vec2{tx1, ty1});

						auto c_tl = graphic::color{0, 0, 0, 1}.from_hsv({hue, 1.0f - tx0, 1.0f - ty0});
						auto c_tr = graphic::color{0, 0, 0, 1}.from_hsv({hue, 1.0f - tx1, 1.0f - ty0});
						auto c_bl = graphic::color{0, 0, 0, 1}.from_hsv({hue, 1.0f - tx0, 1.0f - ty1});
						auto c_br = graphic::color{0, 0, 0, 1}.from_hsv({hue, 1.0f - tx1, 1.0f - ty1});

						renderer().push(graphic::draw::instruction::rect_aabb{
							.v00 = sub_v00,
							.v11 = sub_v11,
							.vert_color = {c_tl, c_tr, c_bl, c_br}
						});
					}
				}
			}

			if(param.is_top()){
				const auto region = content_bound_abs();
				const auto extent = get_bar_handle_extent();

				auto& r = renderer();
				using namespace graphic;
				using namespace graphic::draw::instruction;
				auto opacityScl = get_draw_opacity();



				auto draw_at = [&](math::vec2 progress, float opacity, bool expand){
					auto radius = extent.get_min() + (expand ? 6.f : 0.f);

					const auto pos = region.src + progress * region.extent().fdim(extent) + extent * .5f;
					progress.x = 1 - progress.x;
					progress.y = 1 - progress.y;
					auto color = get_picker().get_color_at_current_hue(progress);
					r.push(poly{
						.pos = pos,
						.segments = 12,
						.radius = {0, radius},
						.color = {color.copy_set_a(opacity), color.copy_set_a(opacity)}
					});

					const poly instr1{
						.pos = pos,
						.segments = 12u + (expand ? 4u : 0u),
						.radius = {radius, radius + 3},
						.color = {colors::white.copy_set_a(opacity), colors::white.copy_set_a(opacity)}
					};

					r << instr1;
					fx::fringe::poly_fringe_at_to(r, instr1, 1.f);
					fx::fringe::poly(r, {
						.pos = pos,
						.segments = 12u + (expand ? 4u : 0u),
						.radius = {radius - .25f, radius + .25f},
						.color = {colors::black.copy_set_a(opacity), colors::black.copy_set_a(opacity)}
					}, 1.f);
				};
				draw_at(bar.get_progress(), .5f, false);
				draw_at(bar.get_temp_progress(), 1.f, cursor_state().pressed);
			}
		}
	};

	slider_gradient_drawer hue_slider_drawer_;
	slider_gradient_drawer alpha_slider_drawer_;

	graphic::hsv hsv_{};
	slider* slider_HUE_{};
	slider* slider_alpha_{};

	graphic::color result_color_{1, 0, 0, 1};
private:
	graphic::color get_color_at_current_hue(math::vec2 sv) const noexcept{
		return graphic::color{{.a = 1}}.from_hsv({hsv_.h, sv.x, sv.y});
	}

public:
	slider& get_slider_HUE() const noexcept{
		return *slider_HUE_;
	}

	slider* get_slider_alpha() const noexcept{
		return slider_alpha_;
	}

	rgb_picker(scene& scene, elem* parent, layout::layout_policy layout_policy, bool has_alpha)
		: head_body(scene, parent, layout_policy){
		interactivity = interactivity_flag::children_only;
		set_expand_policy(layout::expand_policy::passive);

		create_head([](sv_selection& s){
			s.set_style();
			s.bar_handle_extent /= 1.3f;
		});

		if(has_alpha){
			create_body([&](head_body_no_invariant& sliders){
				sliders.interactivity = interactivity_flag::children_only;
				sliders.set_style();
				sliders.set_expand_policy(layout::expand_policy::passive);

				{
					auto& s = sliders.set_head_elem<slider>(make_obv_slider(sliders, [this](float hue){
						set_color_hue(hue);
					}));
					s.set_style();
					s.set_clamp_from_layout_policy(layout_policy);
					s.set_drawer(hue_gradient_slider_drawer);
					slider_HUE_ = &s;
				}

				{
					auto& s = sliders.set_body_elem<slider>(make_obv_slider(sliders, [this](float a){
						set_color_alpha(a);
					}));
					s.set_style();
					s.set_clamp_from_layout_policy(layout_policy);
					s.set_drawer(alpha_gradient_slider_drawer);
					slider_alpha_ = &s;
				}
			}, layout_policy);
			set_body_size({layout::size_category::mastering, 128});
		}else{
			auto& s = set_body_elem<slider>(make_obv_slider(*this, [this](float hue){
				set_color_hue(hue);
			}));
			s.set_style();
			s.set_clamp_from_layout_policy(layout_policy);
			s.set_drawer(hue_gradient_slider_drawer);
			slider_HUE_ = &s;
			set_body_size({layout::size_category::mastering, 60});
		}

	}

	rgb_picker(scene& scene, elem* parent)
		: rgb_picker(scene, parent, layout::layout_policy::vert_major, true){
	}

protected:
	virtual void on_color_changed(graphic::color color){
		std::println(std::cerr, "{:a}", color);
	}

private:
	void set_color_alpha(float a){
		if(util::try_modify(result_color_.a, a)){
			on_color_changed(result_color_);
		}
	}

	void set_color_hue(float hue){
		if(util::try_modify(hsv_.h, hue)){
			result_color_.set_hue(hue);
			on_color_changed(result_color_);
		}
	}

	void set_color_sv(math::vec2 sv){
		bool changed = false;
		if(util::try_modify(hsv_.s, sv.x)){
			result_color_.set_saturation(sv.x);
			changed = true;
		}
		if(util::try_modify(hsv_.v, sv.y)){
			result_color_.set_value(sv.y);
			changed = true;
		}
		if(changed){
			on_color_changed(result_color_);
		}
	}
};
}

}
