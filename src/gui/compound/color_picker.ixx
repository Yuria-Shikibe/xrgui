module;

#include <cassert>

export module mo_yanxi.gui.compound.color_picker;

import std;

export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.graphic.color;

import mo_yanxi.gui.elem.head_body_elem;
import mo_yanxi.gui.elem.slider;
import mo_yanxi.gui.elem.grid;
import mo_yanxi.gui.elem.label;
import mo_yanxi.gui.elem.sequence;
import mo_yanxi.gui.compound.numeric_input_area;


import mo_yanxi.gui.util.observable_value;
import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.gui.fx.fringe;

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
			fx::fringe::inplace_line_context<segs_size + 4> context{};

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
				auto color = graphic::color{}.from_hsv({
						static_cast<float>(i) / static_cast<float>((segs_size - 1)), 1, 1
					}).set_a(opacityScl);
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
template <typename... Fns>
struct obv_slider : slider1d{
	util::observable_value<float, std::decay_t<Fns>...> target;

	template <typename... FnTys>
	obv_slider(scene& scene, elem* parent, FnTys&&... fns)
		: slider1d(scene, parent), target(std::forward<FnTys>(fns)...){
	}

protected:
	void on_changed() override{
		auto val = bar.get_progress()[0];
		target(val);
	}
};

template <typename... FnTys>
obv_slider(scene& scene, elem* parent, FnTys&&... fns) -> obv_slider<std::decay_t<FnTys>...>;

template <typename... FnTys>
elem_ptr make_obv_slider(elem& parent, FnTys&&... fns){
	return elem_ptr{
			parent.get_scene(), &parent, std::in_place_type<obv_slider<std::decay_t<FnTys>>...>,
			std::forward<FnTys>(fns)...
		};
}

export
struct hsv_picker : head_body{
private:
	struct sv_selection : slider2d{
		hsv_picker& get_picker() const noexcept{
			return parent_ref<hsv_picker>();
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
		void record_draw_layer(draw_call_stack_recorder& call_stack_builder) const override{
			call_stack_builder.push_call_noop(*this, [](const sv_selection& s, const draw_call_param& param){
				if(!param.layer_param.is_top()) return;
				if(!util::is_draw_param_valid(s, param)) return;


				using namespace graphic;
				using namespace graphic::draw::instruction;


				auto hue = s.get_picker().hsv_.h;
				auto contentext = s.content_bound_abs();
				auto v00_full = contentext.vert_00() + s.get_bar_handle_extent(s.content_extent()) / 2;
				auto v11_full = contentext.vert_11() - s.get_bar_handle_extent(s.content_extent()) / 2;
				auto& r = s.renderer();

				s.renderer().push(rect_aabb{
						.v00 = contentext.vert_00(),
						.v11 = contentext.vert_11(),
						.vert_color = {colors::dark_gray}
					});

				auto [grid_size_x, grid_size_y] = (contentext.extent() / 64.f).ceil().as<int>().max({1, 1});

				for(int iy = 0; iy < grid_size_y; ++iy){
					const float ty0 = static_cast<float>(iy) / grid_size_y;
					const float ty1 = static_cast<float>(iy + 1) / grid_size_y;

					for(int ix = 0; ix < grid_size_x; ++ix){
						const float tx0 = static_cast<float>(ix) / grid_size_x;
						const float tx1 = static_cast<float>(ix + 1) / grid_size_x;

						const auto sub_v00 = math::lerp(v00_full, v11_full, math::vec2{tx0, ty0});
						const auto sub_v11 = math::lerp(v00_full, v11_full, math::vec2{tx1, ty1});


						auto c_tl = color{0, 0, 0, 1}.from_hsv({hue, tx0, 1.0f - ty0});
						auto c_tr = color{0, 0, 0, 1}.from_hsv({hue, tx1, 1.0f - ty0});
						auto c_bl = color{0, 0, 0, 1}.from_hsv({hue, tx0, 1.0f - ty1});
						auto c_br = color{0, 0, 0, 1}.from_hsv({hue, tx1, 1.0f - ty1});

						s.renderer().push(rect_aabb{
								.v00 = sub_v00,
								.v11 = sub_v11,
								.vert_color = {c_tl, c_tr, c_bl, c_br}
							});
					}

					const auto region = s.content_bound_abs();
					const auto extent = s.get_bar_handle_extent(s.content_extent());


					auto draw_at = [&](math::vec2 progress, float opacity, bool expand){
						auto radius = extent.get_min() + (expand ? 6.f : 0.f);

						const auto pos = region.src + progress * region.extent().fdim(extent) + extent * .5f;
						progress.y = 1 - progress.y;
						auto color = s.get_picker().get_color_at_current_hue(progress);
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
							                 .color = {
								                 colors::black.copy_set_a(opacity), colors::black.copy_set_a(opacity)
							                 }
						                 }, 1.f);
					};
					draw_at(math::vec2{s.bar.get_progress()[0], s.bar.get_progress()[1]}, .5f, false);
					draw_at(math::vec2{s.bar.get_temp_progress()[0], s.bar.get_temp_progress()[1]}, 1.f,
					        s.cursor_state().pressed);
				}
			});
		}
	};

	graphic::hsv hsv_{};
	slider1d* slider_HUE_{};
	slider1d* slider_alpha_{};

	graphic::color result_color_{1, 1, 1, 1};

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

	hsv_picker(scene& scene, elem* parent, layout::layout_policy layout_policy, bool has_alpha)
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
					s.set_drawer(hue_gradient_slider_drawer);
					slider_HUE_ = &s;
				}

				{
					auto& s = sliders.set_body_elem<slider1d>(make_obv_slider(sliders, [this](float a){
						set_color_alpha(a);
					}));
					s.set_style();
					s.set_vertical(layout_policy);
					s.set_drawer(alpha_gradient_slider_drawer);
					slider_alpha_ = &s;
				}
			}, layout_policy);
			set_body_size({layout::size_category::mastering, 128});
		} else{
			auto& s = set_body_elem<slider1d>(make_obv_slider(*this, [this](float hue){
				set_color_hue(hue);
			}));
			s.set_style();
			s.set_vertical(layout_policy);
			s.set_drawer(hue_gradient_slider_drawer);
			slider_HUE_ = &s;
			set_body_size({layout::size_category::mastering, 60});
		}
	}

	hsv_picker(scene& scene, elem* parent)
		: hsv_picker(scene, parent, layout::layout_policy::vert_major, true){
	}

	graphic::color get_current_color() const noexcept{
		return result_color_;
	}

	bool set_current_color_no_propagate(graphic::color color) noexcept{
		if(!util::try_modify(result_color_, color))return false;

		hsv_ = result_color_.to_hsv();

		if(slider_HUE_){
			slider_HUE_->bar.set_progress(0, hsv_.h);
		}

		if(slider_alpha_){
			slider_alpha_->bar.set_progress(0, 1.0f - result_color_.a);
		}

		auto& sv_slider = elem_cast<sv_selection, true>(head());
		sv_slider.bar.set_progress({hsv_.s, 1.0f - hsv_.v});
		return true;
	}
	void set_current_color(graphic::color color) noexcept{
		if(!set_current_color_no_propagate(color))return;
		on_color_changed(result_color_);
	}

protected:
	virtual void on_color_changed(graphic::color color){

	}

private:
	void set_color_alpha(float a){
		if(util::try_modify(result_color_.a, 1 - a)){
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

export
struct precise_color_picker : head_body{
private:
	struct rgba_input final : cpd::numeric_input_area<std::uint8_t>{
		using numeric_input_area::numeric_input_area;
		precise_color_picker& get_picker() const noexcept{
			return parent_ref<elem>().parent_ref<precise_color_picker, true>();
		}

		unsigned get_rgba_channel() const noexcept{
			auto& cs = get_picker().rgba_channel_inputs;
			return std::ranges::find(cs, static_cast<const numeric_input_area*>(this)) - cs.begin();
		}

		void on_changed(value_type val) override{
			auto& picker = get_picker();
			auto& hsv_picker = picker.head();
			auto c = hsv_picker.get_current_color();
			switch(get_rgba_channel()){
			case 0 : c.r = float(val) / 255.5f; break;
			case 1 : c.g = float(val) / 255.5f; break;
			case 2 : c.b = float(val) / 255.5f; break;
			case 3 : c.a = float(val) / 255.5f; break;
			default : std::unreachable();
			}
			if(hsv_picker.set_current_color_no_propagate(c)){
				picker.on_color_changed(c);
			}
		}
	};

	std::array<rgba_input*, 4> rgba_channel_inputs{};

protected:
	virtual void on_color_changed(graphic::color color){

	}
public:

	struct precise_hsv_picker final : hsv_picker{
		precise_color_picker& parent_ref() const noexcept{
			return elem::parent_ref<precise_color_picker, true>();
		}

		using hsv_picker::hsv_picker;

	protected:
		void on_color_changed(graphic::color color) override{
			auto rgba = color.to_rgba8888();

			if constexpr (std::endian::native == std::endian::little){
				rgba = std::byteswap(rgba);
			}

			auto arr = std::bit_cast<std::array<std::uint8_t, 4>>(rgba);

			auto& p = parent_ref();
			for(int i = 0; i < arr.size(); ++i){
				if(auto c = p.rgba_channel_inputs[i])c->set_value_no_propagate(arr[i]);
			}
			p.on_color_changed(color);
		}
	};

	precise_hsv_picker& head() const{
		return elem_cast<precise_hsv_picker, true>(head_body::head());
	}

	[[nodiscard]] precise_color_picker(scene& scene, elem* parent, layout::layout_policy layout_policy, float input_area_height, bool has_alpha)
	: head_body(scene, parent, layout_policy){
		using namespace std::literals;
		static constexpr std::u32string_view titles[]{U"R: "sv, U"G: "sv, U"B: "sv, U"A: "sv};

		create_head([](precise_hsv_picker& s){
			s.set_style();
		});
		set_expand_policy(layout::expand_policy::passive);

		if(layout_policy == layout::layout_policy::vert_major){
			create_body([&](sequence& s){
				s.set_expand_policy(layout::expand_policy::passive);
				s.template_cell.set_pad({4, 4});
				s.set_style();

				auto total = 3 + has_alpha;
				for(int i = 0; i < total; ++i){
					s.create_back([i](gui::direct_label& e){
						e.set_style();
						e.set_self_boarder({.left = 4});
						e.set_fit_type(label_fit_type::scl);
						e.set_tokenized_text({titles[i]});
					}).cell().set_passive(1);
					s.create_back([&](rgba_input& e){
						e.set_style();
						rgba_channel_inputs[i] = &e;
					}).cell().set_passive(1.7f);
					if(i != total - 1){
						s.create_back([](elem& e){
							e.set_style();
						}).cell().set_passive(.3f);
					}
				}
			}, layout::layout_policy::hori_major);

			set_body_size(input_area_height);
		}else{
			if(has_alpha){
				create_body(
					[](grid& table){
						table.set_style();
						table.emplace_back<elem>().cell().extent = {
								{.type = grid_extent_type::src_extent, .desc = {0, 1},},
								{.type = grid_extent_type::src_extent, .desc = {0, 1},},
							};
						table.emplace_back<elem>().cell().extent = {
								{.type = grid_extent_type::src_extent, .desc = {1, 1},},
								{.type = grid_extent_type::src_extent, .desc = {0, 1},},
							};
						table.emplace_back<elem>().cell().extent = {
								{.type = grid_extent_type::src_extent, .desc = {0, 1},},
								{.type = grid_extent_type::src_extent, .desc = {1, 1},},
							};
						table.emplace_back<elem>().cell().extent = {
								{.type = grid_extent_type::src_extent, .desc = {1, 1},},
								{.type = grid_extent_type::src_extent, .desc = {1, 1},},
							};
					}, math::vector2<grid_dim_spec>{
						grid_uniformed_passive{2, {4, 4}},
						grid_uniformed_mastering{2, input_area_height, {4, 4}}
					});
			} else{
				create_body([](sequence& s){
					s.set_style();
				});
			}

			set_body_size({layout::size_category::pending});
		}
		for(int i = 0; i < rgba_channel_inputs.size(); ++i){
			if(auto c = rgba_channel_inputs[i])c->set_value_no_propagate(std::numeric_limits<std::uint8_t>::max());
		}
	}

	[[nodiscard]] precise_color_picker(scene& scene, elem* parent)
		: precise_color_picker(scene, parent, layout::layout_policy::vert_major, 240, true){
	}
};

}
}
