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
import mo_yanxi.gui.elem.text_edit;
import mo_yanxi.gui.compound.numeric_input_area;


import mo_yanxi.gui.util.observable_value;
import mo_yanxi.graphic.g2d;
import mo_yanxi.gui.layout.policies;
import mo_yanxi.graphic.g2d.fringe;

namespace mo_yanxi::gui{
export
struct slider_gradient_drawer{
	using target_type = slider1d;

	graphic::color src;
	graphic::color dst;

	void operator()(const style::typed_draw_param<target_type>& p) const{
		if(p.immut_args.layer.is_top()){
			auto srca = src.copy().mul_a(p->opacity_scl);
			auto dsta = dst.copy().mul_a(p->opacity_scl);
			p.subject().renderer() << graphic::g2d::rect_aabb{
					.v00 = p->draw_bound.get_src(),
					.v11 = p->draw_bound.get_end(),
					.vert_color = {srca, dsta, srca, dsta}
				};
		}

		style::draw_slider1d_default{}(p);
	}
};

struct hue_gradient_drawer final {
	using target_type = slider1d;

	void operator()(const style::typed_draw_param<target_type>& p) const{
		if(p.immut_args.layer.is_top()){
			const auto& element = p.subject();
			auto region = p->draw_bound;
			float opacityScl = p->opacity_scl;
			static constexpr int segs_size = 128;
			graphic::g2d::fringe::inplace_line_context<segs_size + 4> context{};

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
				auto pos = start.copy();
				pos.*major = math::fma(marching, i, pos.*major);
				auto color = graphic::color{}.from_hsv({
						static_cast<float>(i) / static_cast<float>(segs_size - 1), 1, 1
					}).set_a(opacityScl);
				context.push(pos, stroke * .6f, color);
			}

			context.add_cap(0.1f, 0.1f);
			element.renderer() << context.mid(graphic::g2d::line_segments{});
		}

		style::draw_slider1d_default{}(p);
	}
};

struct alpha_gradient_drawer {
	using target_type = slider1d;

	void operator()(const style::typed_draw_param<target_type>& p) const{
		if(p.immut_args.layer.is_top()){
			const auto& element = p.subject();
			auto region = p->draw_bound;

			using namespace graphic::g2d;

			constexpr auto color_base = graphic::colors::light_gray.create_lerp(graphic::colors::gray, .75f);
			constexpr auto color_front = graphic::colors::gray.create_lerp(graphic::colors::dark_gray, .75f);

			element.renderer().push(rect_aabb{
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
				element.renderer().push(rect_aabb{
						.generic = {.mode = std::to_underlying(fx::primitive_draw_mode::draw_slide_line)},
						.v00 = region.vert_00(),
						.v11 = region.vert_11(),
						.vert_color = {
							color_front.make_transparent(), color_front.make_transparent(),
							color_front, color_front,
						}
					});

				element.renderer().push(rect_aabb{
						.v00 = region.vert_00(),
						.v11 = region.vert_11(),
						.vert_color = {
							color_base, color_base,
							color_base.make_transparent(), color_base.make_transparent()
						}
					});
			} else{
				element.renderer().push(rect_aabb{
						.generic = {.mode = std::to_underlying(fx::primitive_draw_mode::draw_slide_line)},
						.v00 = region.vert_00(),
						.v11 = region.vert_11(),
						.vert_color = {
							graphic::colors::gray.make_transparent(), graphic::colors::gray,
							graphic::colors::gray.make_transparent(), graphic::colors::gray
						}
					});

				element.renderer().push(rect_aabb{
						.v00 = region.vert_00(),
						.v11 = region.vert_11(),
						.vert_color = {
							graphic::colors::light_gray, graphic::colors::light_gray.make_transparent(),
							graphic::colors::light_gray, graphic::colors::light_gray.make_transparent()
						}
					});
			}
		}

		style::draw_slider1d_default{}(p);
	}
};

inline auto make_hue_gradient_slider_style(){
	return style::make_tree_node_ptr(style::tree_leaf{hue_gradient_drawer{}});
}

inline auto make_alpha_gradient_slider_style(){
	return style::make_tree_node_ptr(style::tree_leaf{alpha_gradient_drawer{}});
}

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
		void record_draw_layer(draw_recorder& call_stack_builder) const override{
			call_stack_builder.push_call_noop(*this, [](const sv_selection& s, const draw_call_param& param,
			                                            const draw_immut_args& args){
				if(!args.layer.is_top()) return;
				if(!util::is_draw_param_valid(s, param)) return;
				const float opacityScl = util::get_final_draw_opacity(s, param);


				using namespace graphic;
				using namespace graphic::g2d;


				auto hue = s.get_picker().hsv_.h;
				auto contentext = s.content_bound_abs();
				auto v00_full = contentext.vert_00() + s.get_bar_handle_extent(s.content_extent()) / 2;
				auto v11_full = contentext.vert_11() - s.get_bar_handle_extent(s.content_extent()) / 2;
				auto& r = s.renderer();

				s.renderer().push(rect_aabb{
						.v00 = contentext.vert_00(),
						.v11 = contentext.vert_11(),
						.vert_color = {colors::dark_gray.copy().mul_a(opacityScl)}
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
								.vert_color = {
									c_tl.mul_a(opacityScl), c_tr.mul_a(opacityScl),
									c_bl.mul_a(opacityScl), c_br.mul_a(opacityScl)
								}
							});
					}

					const auto region = s.content_bound_abs();
					const auto extent = s.get_bar_handle_extent(s.content_extent());


					auto draw_at = [&](math::vec2 progress, float opacity, bool expand){
						opacity *= opacityScl;
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
					r << graphic::g2d::fringe::poly_fringe_at_to(instr1, 1.f);
					r << graphic::g2d::fringe::poly(poly{
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
					s.set_drawer(make_hue_gradient_slider_style());
					slider_HUE_ = &s;
				}

				{
					auto& s = sliders.set_body_elem<slider1d>(make_obv_slider(sliders, [this](float a){
						set_color_alpha(a);
					}));
					s.set_style();
					s.set_vertical(layout_policy);
					s.set_drawer(make_alpha_gradient_slider_style());
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
			s.set_drawer(make_hue_gradient_slider_style());
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
	enum struct hex_parse_status{
		valid,
		incomplete,
		invalid
	};

	enum struct hex_parse_mode{
		editing,
		commit
	};

	struct hex_parse_result{
		hex_parse_status status{};
		graphic::color color{};
	};

	static constexpr int hex_digit_value(char32_t ch) noexcept{
		if(ch >= U'0' && ch <= U'9')return static_cast<int>(ch - U'0');
		if(ch >= U'a' && ch <= U'f')return static_cast<int>(ch - U'a') + 10;
		if(ch >= U'A' && ch <= U'F')return static_cast<int>(ch - U'A') + 10;
		return -1;
	}

	static constexpr bool parse_hex_byte(std::u32string_view text, std::uint8_t& value) noexcept{
		assert(text.size() == 2);
		const int hi = hex_digit_value(text[0]);
		const int lo = hex_digit_value(text[1]);
		if(hi < 0 || lo < 0)return false;
		value = static_cast<std::uint8_t>((hi << 4) | lo);
		return true;
	}

	static constexpr graphic::color color_from_rgba8(std::array<std::uint8_t, 4> rgba) noexcept{
		return {
				static_cast<float>(rgba[0]) / graphic::color::max_val_f,
				static_cast<float>(rgba[1]) / graphic::color::max_val_f,
				static_cast<float>(rgba[2]) / graphic::color::max_val_f,
				static_cast<float>(rgba[3]) / graphic::color::max_val_f,
			};
	}

	static hex_parse_result parse_hex_color(std::u32string_view text, bool has_alpha, hex_parse_mode mode) noexcept{
		if(text.empty())return {.status = hex_parse_status::incomplete};

		if(text.front() == U'#'){
			text.remove_prefix(1);
		}

		if(text.empty())return {.status = hex_parse_status::incomplete};
		if(std::ranges::contains(text, U'#'))return {.status = hex_parse_status::invalid};

		if(has_alpha && mode == hex_parse_mode::editing && text.size() == 6){
			return {.status = hex_parse_status::incomplete};
		}

		const bool has_explicit_alpha = text.size() == 8;
		const bool valid_length = text.size() == 6 || (has_alpha && has_explicit_alpha);
		if(!valid_length){
			const auto target = has_alpha ? 8uz : 6uz;
			if(text.size() < target)return {.status = hex_parse_status::incomplete};
			return {.status = hex_parse_status::invalid};
		}

		std::array<std::uint8_t, 4> rgba{0, 0, 0, std::numeric_limits<std::uint8_t>::max()};
		for(std::size_t i = 0; i < (has_explicit_alpha ? 4uz : 3uz); ++i){
			if(!parse_hex_byte(text.substr(i * 2, 2), rgba[i])){
				return {.status = hex_parse_status::invalid};
			}
		}

		return {.status = hex_parse_status::valid, .color = color_from_rgba8(rgba)};
	}

	static std::array<std::uint8_t, 4> color_to_rgba8(graphic::color color) noexcept{
		auto rgba = color.to_rgba8888();
		if constexpr (std::endian::native == std::endian::little){
			rgba = std::byteswap(rgba);
		}
		return std::bit_cast<std::array<std::uint8_t, 4>>(rgba);
	}

	static std::u32string format_hex_color(graphic::color color, bool has_alpha){
		static constexpr std::u32string_view hex_digits{U"0123456789ABCDEF"};
		const auto rgba = color_to_rgba8(color);
		std::u32string result{};
		result.reserve(has_alpha ? 9 : 7);
		result.push_back(U'#');

		const int channel_count = has_alpha ? 4 : 3;
		for(int i = 0; i < channel_count; ++i){
			const auto byte = rgba[i];
			result.push_back(hex_digits[byte >> 4]);
			result.push_back(hex_digits[byte & 0x0f]);
		}

		return result;
	}

	struct rgba_input final : cpd::numeric_input_area<std::uint8_t>{
		precise_color_picker* picker_{};
		[[nodiscard]] rgba_input(scene& scene, elem* parent, precise_color_picker& picker)
			: numeric_input_area<unsigned char>(scene, parent), picker_{&picker} {
		}

		precise_color_picker& get_picker() const noexcept{
			return *picker_;
		}

		unsigned get_rgba_channel() const noexcept{
			auto& cs = get_picker().rgba_channel_inputs;
			return (unsigned)(std::ranges::find(cs, static_cast<const numeric_input_area*>(this)) - cs.begin());
		}

		void on_changed(value_type val) override{
			auto& picker = get_picker();
			auto& hsv_picker = picker.head();
			auto c = hsv_picker.get_current_color();
			switch(get_rgba_channel()){
			case 0 : c.r = float(val) / graphic::color::max_val_f; break;
			case 1 : c.g = float(val) / graphic::color::max_val_f; break;
			case 2 : c.b = float(val) / graphic::color::max_val_f; break;
			case 3 : c.a = float(val) / graphic::color::max_val_f; break;
			default : std::unreachable();
			}
			if(hsv_picker.set_current_color_no_propagate(c)){
				picker.sync_hex_input(c);
				picker.on_color_changed(c);
			}
		}
	};

	struct hex_text_edit final : text_edit{
		precise_color_picker* picker_{};
		bool syncing_{};

		[[nodiscard]] hex_text_edit(scene& scene, elem* parent, precise_color_picker& picker)
			: text_edit(scene, parent), picker_{&picker}{
			set_on_changed_interval(0);
			set_character_filter_mode(true);
			set_filter_characters(std::u32string_view{U"#0123456789abcdefABCDEF"});
			set_max_code_points(9);
			set_hint_text(std::u32string_view{picker.has_alpha_ ? U"#RRGGBBAA" : U"#RRGGBB"});
		}

		precise_color_picker& get_picker() const noexcept{
			return *picker_;
		}

		void sync_from_color(graphic::color color){
			const auto formatted = precise_color_picker::format_hex_color(color, get_picker().has_alpha_);
			syncing_ = true;
			apply_edit([&](std::u32string& text){
				if(text == formatted)return false;
				text = formatted;
				return true;
			});
			syncing_ = false;
		}

	protected:
		void on_changed() override{
			if(syncing_)return;

			auto parsed = precise_color_picker::parse_hex_color(
				get_text(), get_picker().has_alpha_, hex_parse_mode::editing);
			switch(parsed.status){
			case hex_parse_status::valid:
				get_picker().set_color_from_hex_input(parsed.color);
				break;
			case hex_parse_status::invalid:
				set_input_invalid();
				break;
			case hex_parse_status::incomplete:
				break;
			default:
				std::unreachable();
			}
		}

		void action_enter() override{
			if(commit_text()){
				action_select_all();
			}
		}

		void on_last_clicked_changed(bool isFocused) override{
			text_edit::on_last_clicked_changed(isFocused);
			if(isFocused){
				action_select_all();
			} else if(!syncing_ && !commit_text()){
				sync_from_color(get_picker().head().get_current_color());
			}
		}

	private:
		bool commit_text(){
			auto parsed = precise_color_picker::parse_hex_color(
				get_text(), get_picker().has_alpha_, hex_parse_mode::commit);
			if(parsed.status == hex_parse_status::valid){
				get_picker().set_color_from_hex_input(parsed.color);
				return true;
			}

			set_input_invalid();
			return false;
		}
	};

	std::array<rgba_input*, 4> rgba_channel_inputs{};
	hex_text_edit* hex_input_{};
	bool has_alpha_{true};

	void sync_rgba_inputs(graphic::color color){
		const auto rgba = color_to_rgba8(color);
		for(int i = 0; i < rgba.size(); ++i){
			if(auto c = rgba_channel_inputs[i])c->set_value_no_propagate(rgba[i]);
		}
	}

	void sync_hex_input(graphic::color color){
		if(hex_input_)hex_input_->sync_from_color(color);
	}

	void set_color_from_hex_input(graphic::color color){
		if(head().set_current_color_no_propagate(color)){
			sync_rgba_inputs(color);
			sync_hex_input(color);
			on_color_changed(color);
		} else{
			sync_hex_input(color);
		}
	}

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
			auto& p = parent_ref();
			p.sync_rgba_inputs(color);
			p.sync_hex_input(color);
			p.on_color_changed(color);
		}
	};

	precise_hsv_picker& head() const{
		return elem_cast<precise_hsv_picker, true>(head_body::head());
	}

	[[nodiscard]] precise_color_picker(scene& scene, elem* parent, const layout::directional_layout_specifier layout_policy, float input_area_height, bool has_alpha = true)
	: head_body(scene, parent, layout_policy), has_alpha_{has_alpha}{
		using namespace std::literals;
		static constexpr std::u32string_view titles[]{U"R: "sv, U"G: "sv, U"B: "sv, U"A: "sv, U"HEX: "sv};

		create_head([](precise_hsv_picker& s){
			s.set_style();
		});
		set_expand_policy(layout::expand_policy::passive);

		if(head_body::get_layout_policy() == layout::layout_policy::vert_major){
			create_body([&](sequence& s){
				s.set_expand_policy(layout::expand_policy::passive);
				s.template_cell.set_pad({4, 4});
				s.set_style();

				auto total = 3 + has_alpha;
				for(int i = 0; i < total; ++i){
					s.create_back([i](gui::direct_label& e){
						e.set_style();
						e.set_self_border({.left = 4});
						e.set_fit_type(label_fit_type::scl);
						e.set_tokenized_text({titles[i]});
					}).cell().set_passive(1);
					s.create_back([&](rgba_input& e){
						e.set_style();
						rgba_channel_inputs[i] = &e;
					}, *this).cell().set_passive(1.7f);
					if(i != total - 1){
						s.create_back([](elem& e){
							e.set_style();
						}).cell().set_passive(.3f);
					}
				}
				s.create_back([](elem& e){
					e.set_style();
				}).cell().set_passive(.3f);
				s.create_back([](gui::direct_label& e){
					e.set_style();
					e.set_self_border({.left = 4});
					e.set_fit_type(label_fit_type::scl);
					e.set_tokenized_text({titles[4]});
				}).cell().set_passive(1.4f);
				s.create_back([&](hex_text_edit& e){
					e.set_style();
					e.set_view_type(text_edit_view_type::align_y);
					e.set_expand_policy(layout::expand_policy::passive);
					e.text_entire_align = align::pos::center;
					hex_input_ = &e;
				}, *this).cell().set_passive(3.2f);
			}, layout::layout_policy::hori_major);

			set_body_size(input_area_height);
		} else {
			const float controls_height = input_area_height * (has_alpha ? 3.f : 4.f);

			// 提取一个通用的 Lambda，用于创建单个通道（Label + Input）的横向结构
			auto create_channel_view = [&](head_body_no_invariant& s, int i) {
				s.set_expand_policy(layout::expand_policy::passive);
				s.create_head([i](gui::direct_label& e) {
					e.set_style();
					e.set_self_border({.left = 4});
					e.set_fit_type(label_fit_type::scl);
					e.set_tokenized_text({titles[i]});
				});
				s.create_body([&, i](rgba_input& e) {
					e.set_style();
					rgba_channel_inputs[i] = &e;
				}, *this);
				s.set_head_size({layout::size_category::passive, 1.f});
				s.set_body_size({layout::size_category::passive, 1.7f});
				s.set_pad(4);
			};

			auto create_hex_view = [&](head_body_no_invariant& s) {
				s.set_expand_policy(layout::expand_policy::passive);
				s.create_head([](gui::direct_label& e) {
					e.set_style();
					e.set_self_border({.left = 4});
					e.set_fit_type(label_fit_type::scl);
					e.set_tokenized_text({titles[4]});
				});
				s.create_body([&](hex_text_edit& e) {
					e.set_style();
					e.set_view_type(text_edit_view_type::align_y);
					e.set_expand_policy(layout::expand_policy::passive);
					e.text_entire_align = align::pos::center;
					hex_input_ = &e;
				}, *this);
				s.set_head_size({layout::size_category::passive, 1.f});
				s.set_body_size({layout::size_category::passive, 4.f});
				s.set_pad(4);
			};

			if(has_alpha){
				create_body(
					[&](sequence& controls){
						controls.set_style();
						controls.set_expand_policy(layout::expand_policy::passive);
						controls.template_cell.set_pad({4, 4});

						controls.create_back([&](grid& table){
							table.set_expand_policy(layout::expand_policy::resize_to_fit);
							table.set_style();

							table.create_back([&](head_body_no_invariant& s){ create_channel_view(s, 0); }, layout::layout_policy::hori_major).cell().extent = {
									{.type = grid_extent_type::src_extent, .desc = {0, 1},},
									{.type = grid_extent_type::src_extent, .desc = {0, 1},},
								};
							table.create_back([&](head_body_no_invariant& s){ create_channel_view(s, 1); }, layout::layout_policy::hori_major).cell().extent = {
									{.type = grid_extent_type::src_extent, .desc = {1, 1},},
									{.type = grid_extent_type::src_extent, .desc = {0, 1},},
								};
							table.create_back([&](head_body_no_invariant& s){ create_channel_view(s, 2); }, layout::layout_policy::hori_major).cell().extent = {
									{.type = grid_extent_type::src_extent, .desc = {0, 1},},
									{.type = grid_extent_type::src_extent, .desc = {1, 1},},
								};
							table.create_back([&](head_body_no_invariant& s){ create_channel_view(s, 3); }, layout::layout_policy::hori_major).cell().extent = {
									{.type = grid_extent_type::src_extent, .desc = {1, 1},},
									{.type = grid_extent_type::src_extent, .desc = {1, 1},},
								};
						}, math::vector2<grid_dim_spec>{
							grid_uniformed_passive{2, {4, 4}},
							grid_uniformed_passive{2, {4, 4}}
						}).cell().set_passive(2.f);

						controls.create_back([&](head_body_no_invariant& row){
							create_hex_view(row);
						}, layout::layout_policy::hori_major).cell().set_passive(1.f);
					});
			} else{
				create_body([&](sequence& s){
					s.set_style();
					s.set_expand_policy(layout::expand_policy::passive);
					s.template_cell.set_pad({4, 4});
					for(int i = 0; i < 3; ++i){
						s.create_back([&, i](head_body_no_invariant& row){
							create_channel_view(row, i);
						}, layout::layout_policy::hori_major).cell().set_passive(1.f);
					}
					s.create_back([&](head_body_no_invariant& row){
						create_hex_view(row);
					}, layout::layout_policy::hori_major).cell().set_passive(1.f);
				}, layout::layout_policy::vert_major);
			}

			set_body_size(controls_height);
		}
		for(int i = 0; i < rgba_channel_inputs.size(); ++i){
			if(auto c = rgba_channel_inputs[i])c->set_value_no_propagate(std::numeric_limits<std::uint8_t>::max());
		}
		sync_hex_input(head().get_current_color());
	}

	[[nodiscard]] precise_color_picker(scene& scene, elem* parent)
		: precise_color_picker(scene, parent, layout::directional_layout_specifier::fixed(layout::layout_policy::vert_major), 240, true){
	}
};

}
}
