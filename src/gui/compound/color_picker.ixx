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
struct slider_gradient_drawer : style::default_slider1d_drawer{
	graphic::color src;
	graphic::color dst;

protected:
	void draw_layer_impl(const target_type& element, math::frect region, float opacityScl,
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

		default_slider1d_drawer::draw_layer_impl(element, region, opacityScl, layer_param);
	}
};

struct hue_gradient_drawer final : gui::style::default_slider1d_drawer{
	void draw_layer_impl(const target_type& element, math::frect region, float opacityScl,
		fx::layer_param layer_param) const override{
		if(layer_param.is_top()){
			static constexpr int segs_size = 128;
			fx::fringe::line_context context{std::in_place_type<beman::inplace_vector::inplace_vector<graphic::draw::instruction::line_node, segs_size + 4>>};

			float math::vec2::* major;
			float math::vec2::* minor;

			if(element.is_vertical()){
				major = &math::vec2::y;
				minor = &math::vec2::x;
			} else{
				major = &math::vec2::x;
				minor = &math::vec2::y;
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

		default_slider1d_drawer::draw_layer_impl(element, region, opacityScl, layer_param);
	}
};

struct alpha_gradient_drawer : gui::style::default_slider1d_drawer{
protected:
	void draw_layer_impl(const target_type& element, math::frect region, float opacityScl,
		fx::layer_param layer_param) const override{

		if(layer_param.is_top()){

			using namespace graphic::draw;

			constexpr auto color_base = graphic::colors::light_gray.create_lerp(graphic::colors::gray, .75f);
			constexpr auto color_front = graphic::colors::gray.create_lerp(graphic::colors::dark_gray, .75f);

			element.renderer().push(instruction::rect_aabb{
				.v00 = region.vert_00(),
				.v11 = region.vert_11(),
				.vert_color = {color_base}
			});

			element.renderer().push(fx::slide_line_config{
				.angle = 45,
				.spacing = 8,
				.stroke = 8,
				.speed = 0,
				.phase = 8,
			});

			if(element.is_vertical()){
				element.renderer().push(instruction::rect_aabb{
						.generic = {.mode = std::to_underlying(fx::primitive_draw_mode::draw_slide_line)},
						.v00 = region.vert_00(),
						.v11 = region.vert_11(),
						.vert_color = {
							color_front.make_transparent(), color_front.make_transparent(),
							color_front, color_front,
						}
					});

				element.renderer().push(instruction::rect_aabb{
						.v00 = region.vert_00(),
						.v11 = region.vert_11(),
						.vert_color = {
							color_base, color_base,
							color_base.make_transparent(), color_base.make_transparent()
						}
					});

			} else{
				element.renderer().push(instruction::rect_aabb{
						.generic = {.mode = std::to_underlying(fx::primitive_draw_mode::draw_slide_line)},
						.v00 = region.vert_00(),
						.v11 = region.vert_11(),
						.vert_color = {
							graphic::colors::gray.make_transparent(), graphic::colors::gray,
							graphic::colors::gray.make_transparent(), graphic::colors::gray
						}
					});

				element.renderer().push(instruction::rect_aabb{
						.v00 = region.vert_00(),
						.v11 = region.vert_11(),
						.vert_color = {
							graphic::colors::light_gray, graphic::colors::light_gray.make_transparent(),
							graphic::colors::light_gray, graphic::colors::light_gray.make_transparent()
						}
					});
			}
		}

		default_slider1d_drawer::draw_layer_impl(element, region, opacityScl, layer_param);
	}

};

constexpr hue_gradient_drawer hue_gradient_slider_drawer{};
constexpr alpha_gradient_drawer alpha_gradient_slider_drawer{};

namespace cpd{
template <typename ...Fns>
struct obv_slider : slider1d{
	util::observable_value<float, std::decay_t<Fns>...> target;

	template <typename ...FnTys>
	obv_slider(scene& scene, elem* parent, FnTys&&... fns)
		: slider1d(scene, parent), target(std::forward<FnTys>(fns)...){
	}

protected:
	void on_changed() override{
		auto val = bar.get_progress()[0];
		target(val);
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
	struct sv_selection : slider2d{
		rgb_picker& get_picker() const noexcept{
			return parent_ref<rgb_picker>();
		}

		sv_selection(scene& scene, elem* parent)
			: slider2d(scene, parent){
		}

	protected:
		void on_changed() override{
			auto [s, v] = bar.get_progress();
			get_picker().set_color_sv(math::vec2{s, 1 - v});
		}
	public:

		void draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const override{
			elem::draw_layer(clipSpace, param);

			if(param.is_top()){

				auto hue = get_picker().hsv_.h;
				auto contentext = content_bound_abs();
				auto v00_full = contentext.vert_00() + get_bar_handle_extent(content_extent()) / 2;
				auto v11_full = contentext.vert_11() - get_bar_handle_extent(content_extent()) / 2;

				renderer().push(graphic::draw::instruction::rect_aabb{
				   .v00 = contentext.vert_00(),
				   .v11 = contentext.vert_11(),
				   .vert_color = {graphic::colors::dark_gray}
				});

				auto [grid_size_x, grid_size_y] = (contentext.extent() / 64.f).ceil().as<int>().max({1, 1});

				for (int iy = 0; iy < grid_size_y; ++iy) {
					const float ty0 = static_cast<float>(iy) / grid_size_y;
					const float ty1 = static_cast<float>(iy + 1) / grid_size_y;

					for (int ix = 0; ix < grid_size_x; ++ix) {
						const float tx0 = static_cast<float>(ix) / grid_size_x;
						const float tx1 = static_cast<float>(ix + 1) / grid_size_x;

						const auto sub_v00 = math::lerp(v00_full, v11_full, math::vec2{tx0, ty0});
						const auto sub_v11 = math::lerp(v00_full, v11_full, math::vec2{tx1, ty1});

						// 2. 水平翻转顶点色：将 1.0f - tx 替换为 tx
						auto c_tl = graphic::color{0, 0, 0, 1}.from_hsv({hue, tx0, 1.0f - ty0});
						auto c_tr = graphic::color{0, 0, 0, 1}.from_hsv({hue, tx1, 1.0f - ty0});
						auto c_bl = graphic::color{0, 0, 0, 1}.from_hsv({hue, tx0, 1.0f - ty1});
						auto c_br = graphic::color{0, 0, 0, 1}.from_hsv({hue, tx1, 1.0f - ty1});

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
				const auto extent = get_bar_handle_extent(content_extent());

				auto& r = renderer();
				using namespace graphic;
				using namespace graphic::draw::instruction;
				auto opacityScl = get_draw_opacity();



				auto draw_at = [&](math::vec2 progress, float opacity, bool expand){
					auto radius = extent.get_min() + (expand ? 6.f : 0.f);

					const auto pos = region.src + progress * region.extent().fdim(extent) + extent * .5f;
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
				draw_at(math::vec2{bar.get_progress()[0], bar.get_progress()[1]}, .5f, false);
				draw_at(math::vec2{bar.get_temp_progress()[0], bar.get_temp_progress()[1]}, 1.f, cursor_state().pressed);
			}
		}
	};

	graphic::hsv hsv_{};
	slider1d* slider_HUE_{};
	slider1d* slider_alpha_{};

	graphic::color result_color_{1, 0, 0, 1};
private:
	graphic::color get_color_at_current_hue(math::vec2 sv) const noexcept{
		return graphic::color{{.a = 1}}.from_hsv({hsv_.h, sv.x, sv.y});
	}

public:
	auto& get_slider_HUE() const noexcept{
		return *slider_HUE_;
	}

	auto* get_slider_alpha() const noexcept{
		return slider_alpha_;
	}

	rgb_picker(scene& scene, elem* parent, layout::layout_policy layout_policy, bool has_alpha)
		: head_body(scene, parent, layout_policy){
		interactivity = interactivity_flag::children_only;
		set_expand_policy(layout::expand_policy::passive);

		create_head([](sv_selection& s){
			s.set_style();
			s.bar_handle_extent[0] /= 1.3f;
			s.bar_handle_extent[1] /= 1.3f;
		});

		if(has_alpha){
			create_body([&](head_body_no_invariant& sliders){
				sliders.interactivity = interactivity_flag::children_only;
				sliders.set_style();
				sliders.set_expand_policy(layout::expand_policy::passive);

				{
					auto& s = sliders.set_head_elem<slider1d>(make_obv_slider(sliders, [this](float hue){
						set_color_hue(hue);
					}));
					s.set_style();
					s.set_vertical(layout_policy);
					s.drawer_ = hue_gradient_slider_drawer;
					slider_HUE_ = &s;
				}

				{
					auto& s = sliders.set_body_elem<slider1d>(make_obv_slider(sliders, [this](float a){
						set_color_alpha(a);
					}));
					s.set_style();
					s.set_vertical(layout_policy);
					s.drawer_ = alpha_gradient_slider_drawer;
					slider_alpha_ = &s;
				}
			}, layout_policy);
			set_body_size({layout::size_category::mastering, 128});
		}else{
			auto& s = set_body_elem<slider1d>(make_obv_slider(*this, [this](float hue){
				set_color_hue(hue);
			}));
			s.set_style();
			s.set_vertical(layout_policy);
			s.drawer_ = hue_gradient_slider_drawer;
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
			result_color_.from_hsv(hsv_);
			on_color_changed(result_color_);
		}
	}

	void set_color_sv(math::vec2 sv){
		bool changed = false;
		if(util::try_modify(hsv_.s, sv.x)){
			changed = true;
		}
		if(util::try_modify(hsv_.v, sv.y)){
			changed = true;
		}
		if(changed){
			result_color_.from_hsv(hsv_);
			on_color_changed(result_color_);
		}
	}
};
}

}
