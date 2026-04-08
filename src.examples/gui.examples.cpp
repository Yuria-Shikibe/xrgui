//
// Created by Matrix on 2025/11/19.
//

module mo_yanxi.gui.examples;

import mo_yanxi.gui.elem.button;


import std;

import binary_trace;

import mo_yanxi.gui.infrastructure;
import mo_yanxi.gui.elem.group;
import mo_yanxi.gui.global;

import mo_yanxi.react_flow;
import mo_yanxi.react_flow.common;

import mo_yanxi.gui.elem.scaling_stack;
import mo_yanxi.gui.elem.sequence;
import mo_yanxi.gui.elem.overflow_sequence;
import mo_yanxi.gui.elem.scroll_pane;
import mo_yanxi.gui.elem.collapser;
import mo_yanxi.gui.elem.table;
import mo_yanxi.gui.elem.grid;
import mo_yanxi.gui.elem.menu;
import mo_yanxi.gui.elem.slider;
import mo_yanxi.gui.elem.progress_bar;
import mo_yanxi.gui.elem.image_frame;
import mo_yanxi.gui.elem.image_frame;
import mo_yanxi.gui.elem.drag_split;
import mo_yanxi.gui.elem.label;
import mo_yanxi.gui.elem.text_edit_v2;
import mo_yanxi.gui.elem.viewport;
import mo_yanxi.gui.elem.check_box;
import mo_yanxi.gui.elem.double_side;

import mo_yanxi.gui.infrastructure;
import mo_yanxi.font;

import mo_yanxi.typesetting.util;
import mo_yanxi.font;
import mo_yanxi.font.manager;
import mo_yanxi.graphic.color;
import mo_yanxi.graphic.msdf;
import mo_yanxi.heterogeneous.open_addr_hash;
import align;

import mo_yanxi.typesetting;
import mo_yanxi.graphic.draw.instruction.recorder;


import mo_yanxi.gui.compound.color_picker;
import mo_yanxi.gui.compound.named_slider;
import mo_yanxi.gui.compound.file_selector;
import mo_yanxi.gui.compound.data_table;

import mo_yanxi.gui.style.round_square;
import mo_yanxi.gui.style.progress_bars;
import mo_yanxi.gui.style.palette;

import mo_yanxi.gui.assets.manager;

import mo_yanxi.backend.communicator;
import mo_yanxi.backend.vulkan.context;

import celestial_display;
import mo_yanxi.graphic.trail;
import mo_yanxi.math.rand;


namespace mo_yanxi::gui::example{

struct test_entry{
	std::string name;
	std::function<elem_ptr(scene&, elem*)> creator;

	[[nodiscard]] test_entry(const std::string& name, const std::function<elem_ptr(scene&, elem*)>& creator)
		: name(name),
		  creator(creator){
	}

	template <invocable_elem_init_func Fn>
	[[nodiscard]] test_entry(const std::string& name, Fn&& fn)
		: name(name),
		  creator([f = std::forward<Fn>(fn)](scene& s, elem* p){
			  return elem_ptr{s, p, f};
		  }){
	}
};

struct image_cursor : style::cursor{

	gui::image_region_borrow icon_region;

	[[nodiscard]] explicit image_cursor(const gui::image_region_borrow& icon_region)
		: icon_region(icon_region){
	}

	rect draw(gui::renderer_frontend& renderer, math::raw_frect region,
		std::span<const elem* const> inbound_stack) const override{
		region.src -= region.extent * .5f;

		region.expand({mo_yanxi::graphic::msdf::sdf_image_boarder, mo_yanxi::graphic::msdf::sdf_image_boarder});
		state_guard g{renderer, gui::fx::batch_draw_mode::msdf};
		renderer << graphic::draw::instruction::rect_aabb{
				.generic = {icon_region->view},
				.v00 = region.vert_00(),
				.v11 = region.vert_11(),
				.uv00 = icon_region->uv.v00(),
				.uv11 = icon_region->uv.v11(),
				.vert_color = {graphic::colors::white}
			};

		return {tags::from_extent, region.src, region.extent};
	}
};


struct csv_file_reader : head_body{
	struct file_listener : react_flow::terminal<std::span<const std::filesystem::path>>{
		gui::overlay* overlay;
		csv_file_reader* carrier;

		[[nodiscard]] explicit file_listener(csv_file_reader* carrier)
			: carrier(carrier){
		}

	protected:
		void on_update(react_flow::data_carrier<std::span<const std::filesystem::path>>& data) override{
			auto sp = data.get();
			if(sp.empty())return;
			auto& path = sp.front();

			carrier->get_scene().close_overlay(std::exchange(overlay, nullptr)->element.get());

			util::post_elem_async_task(*carrier, [&](csv_file_reader& r){
				return elem_async_yield_task{r, [&](csv_file_reader& r){
					return elem_ptr{r.get_scene(), &r, [p = path](cpd::data_table& table){
						table.set_style();
						table.get_item() = cpd::data_table_desc::from_csv(p, '|');
						table.get_item().try_update_glyph_layouts();
						table.notify_isolated_layout_changed();
					}};
				}, [](csv_file_reader& r, elem_ptr&& ptr){
					util::sync_elem_tree(*ptr);
					r.set_body_elem(std::move(ptr));
				}};
			});

			carrier->create_body([&](progress_bar& prog){
				prog.set_style();
				prog.progress.set_state(progress_state::approach_smooth);
				prog.progress.set_speed(.0001f);
				referenced_ptr<style::ring_progress> drawer{std::in_place};
				drawer->thickness = 32;
				prog.draw_config.color = {graphic::colors::white, graphic::colors::white};

				prog.set_self_boarder(gui::boarder{}.set(32));
				prog.drawer = std::move(drawer);
				prog.set_progress_state(progress_state::rough);
			});

		}
	};

	react_flow::node_holder<file_listener> path_node_{this};

	[[nodiscard]] csv_file_reader(scene& scene, elem* parent)
		: head_body(scene, parent){
		set_style();
		set_expand_policy(layout::expand_policy::passive);

		create_head([this](button<direct_label>& b){
			b.set_tokenized_text({"Select File"});
			b.set_button_callback([this](direct_label& e){
				auto& p = e.parent_ref<csv_file_reader>();
				this->path_node_.node.overlay = &e.get_scene().create_overlay(
					{
						.extent = {
							{layout::size_category::passive, .95f},
							{layout::size_category::passive, .95f}
						},
						.align = align::pos::center,
					}, [this](cpd::file_selector& e){
						e.set_cared_suffix({".csv"});
						e.get_prov().connect_successor(this->path_node_.node);
					}).dialog;
			});
		});
		create_body([](cpd::data_table& data_table){

		});

		set_head_size(80);
		set_pad(8);
	}
};

struct vp : gui::viewport{
	std::vector<math::vec2> curve_points;

	celestial::planetary_system system;
	std::vector<simulation_data::body_info> body_infos;

	graphic::draw::instruction::draw_record_chunked_storage<mr::unvs_allocator<std::byte>> text_render_cache;

	double update_speed = 1.;
	double update_time{};

	[[nodiscard]] vp(scene& scene, elem* parent)
		: viewport(scene, parent){
		camera.set_scale_range({.1f, 5.f});
		auto gen = [](float cx, float cy, float a, float b) -> std::vector<math::vec2>{
			// 使用 8 个控制点来近似
			// 为了让三次 B 样条闭合，通常需要将前 3 个点重复添加到末尾
			std::vector<math::vec2> points = {};
			for(int i = 0; i < 12; ++i){
				float angle = i * (360.0f / 12.0f) * (std::numbers::pi_v<float> / 180.0f);

				points.push_back({
						cx + a * math::cos(angle),
						cy + b * math::sin(angle)
					});
			}
			return points;
		};

		curve_points = gen(100, 200, 400, 300);
		body_infos = simulation_data::populate_solar_system(system);

		auto ctx = get_scene().resources().object_pool.acquire<typesetting::layout_context>();
		typesetting::tokenized_text txt;
		typesetting::glyph_layout layout;
		for (const auto & [tidx, body_info] : body_infos | std::views::enumerate){
			txt.reset(body_info.name);
			ctx->layout(txt, {}, layout);

			for(const auto& current_line : layout.lines){
				auto [line_src, spacing] = current_line.calculate_alignment(
					layout.extent, typesetting::line_alignment::start, typesetting::layout_direction::ltr);

				for(const auto& [idx, val] : std::span{
						layout.elems.begin() + current_line.glyph_range.pos, current_line.glyph_range.size
					} | std::views::enumerate){
					if(!val.texture->view) continue;
					auto start = math::fma(idx, spacing, line_src + val.aabb.src);
					text_render_cache.push(graphic::draw::instruction::rect_aabb{
							.generic = {val.texture->view},
							.v00 = start,
							.v11 = start + val.aabb.extent(),
							.uv00 = val.texture->uv.v00(),
							.uv11 = val.texture->uv.v11(),
							.vert_color = {val.color * system.get_constants()[tidx].get_hdr_color()},
							.slant_factor_asc = val.slant_factor_asc,
							.slant_factor_desc = val.slant_factor_desc,
							.sdf_expand = -val.weight_offset
						});
					}
			}

			text_render_cache.split(true);

			layout.clear();
		}
	}

	bool update(float delta_in_ticks) override{
		if(viewport::update(delta_in_ticks)){
			update_time += delta_in_ticks * update_speed;
			if(update_speed > 0)system.update(update_time);
			return true;
		}
		return false;
	}


	void draw_system() const{
		const auto& states = system.get_states();
		const auto& constants = system.get_constants();

		// 建议：先渲染所有轨迹，再渲染所有星体，这样星体会覆盖在轨迹上方

		// 绘制轨迹
		for (std::size_t i = 0; i < states.size(); ++i) {
			const auto& state = states[i];
			const auto& constant = constants[i];
			auto col = constant.get_hdr_color();

			struct trail_node_data : graphic::trail::node_type{
				float idx_scale;
				graphic::color color;

				[[nodiscard]] float get_width() const noexcept{
					return idx_scale * scale;
				}
			};

			if(state.path_trail.size() >= 2){
				state.path_trail.iterate(
					1.f,
					[&, last = state.path_trail.head_pos_or({})](
					const graphic::trail::node_type& node, const unsigned idx,
					const unsigned total) mutable{
						using namespace graphic;
						math::rand rand{std::bit_cast<std::uintptr_t>(&node)};

						float factor_global = math::idx_to_factor(idx, math::max(total, 8U));
						const auto fac = factor_global | math::interp::pow2Out;
						const auto off = rand.range(1.f) * fac * math::curve(
							factor_global, .05f, .5f);
						const auto tan = (node.pos - last).rotate_rt_counter_clockwise() * off;

						last = node.pos;
						auto n = node;
						n.pos += tan;
						const auto color = math::lerp(col, col.copy_set_a(.0f), factor_global);

						factor_global = math::curve(
							factor_global | math::interp::interp_func{
								math::interp::spec::concave_curve_fixed{.1f}
							} |
							math::interp::reverse, math::idx_to_factor(5U, math::max(total, 8U)),
							1.f);

						return trail_node_data{n, factor_global, color};
					}, [&](std::span<const trail_node_data, 4> sspn){
						using namespace graphic;
						using namespace graphic::draw;

						const auto appr = sspn[1].pos - sspn[2].pos;
						const auto apprLen = appr.length();
						const auto seg = math::clamp(
							static_cast<unsigned>(apprLen / 16.f), 4U, 12U);

						renderer().push(instruction::parametric_curve{
								.param = instruction::curve_trait_mat::b_spline * (sspn |
									std::views::transform(
										&trail_node_data::pos)),
								.stroke = math::range{
									sspn[1].get_width(), sspn[2].get_width()
								} * 8.f,
								.segments = seg,
								.color = {
									sspn[1].color, sspn[1].color, sspn[2].color, sspn[2].color
								},
							});
					});

			}
		}

		// 绘制星体球形
		for (std::size_t i = 0; i < states.size(); ++i) {
			const auto& state = states[i];
			const auto& constant = constants[i];
			float render_radius = body_infos[i].render_radius;
			auto col = constant.get_hdr_color();
			// 调用底层的画圆或画球函数，传入HDR发光颜色
			fx::circle c{
				.pos = state.global_position,
				.radius = {0, render_radius * .75f},
				.color = {col * 1.2f, col}
			};
			fx::fringe::poly(renderer(), c, (i == 0 ? 12 : fx::fringe::fringe_size) / camera.get_scale());
		}

		renderer().top_viewport().push_local_transform();
		for (std::size_t i = 0; i < states.size(); ++i) {
			const auto& state = states[i];
			const auto& constant = constants[i];
			float render_radius = body_infos[i].render_radius;
			auto col = constant.get_hdr_color();
			// 调用底层的画圆或画球函数，传入HDR发光颜色
			auto chunk = this->text_render_cache[i];

			auto mov = auto{math::mat3_idt}.set_translation(state.global_position + render_radius * 1.7f);
			auto scl = auto{math::mat3_idt}.from_scaling(body_infos[i].text_scale);
			renderer().top_viewport().set_local_transform(mov * scl);
			renderer().notify_viewport_changed();
			renderer().push(chunk.heads, chunk.data.data());
		}
		renderer().top_viewport().pop_local_transform();
		renderer().notify_viewport_changed();
	}

	void draw_geom(){
		float fringe_s = fx::fringe::fringe_size / camera.get_scale();

		using namespace graphic::draw;

		instruction::poly my_circle{
				.pos = {100.f, 100.f},
				.segments = fx::get_smooth_circle_vertex_count(50.f, 1.0f),
				.radius = {0.f, 50.f}, // 从中心 0.f 到边缘 50.f
				.color = {graphic::colors::white * 2, graphic::colors::CORAL * 2}
			};

		// 提交给 fringe 进行边缘抗锯齿绘制
		// 假设 renderer() 返回 renderer_frontend 的引用
		fx::fringe::poly(renderer(), my_circle, fringe_s);

		instruction::poly_partial my_arc{
				.pos = {},
				.segments = 32U,
				.range = {0.2f, .5f}, // 0 到 Pi，表示半圆
				.radius = {120.f, 150.f}, // 内径 40，外径 50 的环形
				.color = {
					graphic::colors::pale_green, graphic::colors::pale_green,
					graphic::colors::red_dusted, graphic::colors::red_dusted,
				}
			};

		fx::fringe::poly_partial_with_cap(renderer(), my_arc, fringe_s, fringe_s, fringe_s);

		{
			state_guard _{renderer(), fx::blend::pma::additive};
			for(unsigned i = 0; i < curve_points.size(); ++i){
				fx::fringe::curve(renderer(), {
					                  .param = instruction::curve_trait_mat::b_spline.apply_to(
						                  curve_points[i], curve_points[(i + 1) % curve_points.size()],
						                  curve_points[(i + 2) % curve_points.size()],
						                  curve_points[(i + 3) % curve_points.size()]),
					                  .stroke = {12, 12},
					                  .segments = 6,
					                  .color = {graphic::colors::aqua.copy_set_a(.52f)}
				                  });
			}
		}

		{
			using namespace fx;
			fringe::inplace_line_context<32> ctx;

			// 压入折线的各个节点
			ctx.push({-2000.f, 100.f}, 25.f, graphic::colors::white);
			ctx.push({450.f, 600.f}, 50.f, graphic::colors::ENERGY);
			ctx.push({900.f, -200.f}, 75.f, graphic::colors::FOREST);

			// 为折线首尾添加透明渐变的端点，用于抗锯齿
			ctx.add_fringe_cap(fringe_s, fringe_s);

			// 准备非闭合折线的指令头
			instruction::line_segments line_head{};

			// 依次提交主体、内侧边缘、外侧边缘
			ctx.dump_mid(renderer(), line_head);
			ctx.dump_fringe_inner(renderer(), line_head, fringe_s);
			ctx.dump_fringe_outer(renderer(), line_head, fringe_s);
		}

		renderer() << instruction::rect_aabb{
				.v00 = {-50, -50},
				.v11 = {50, 50},
				.vert_color = {graphic::colors::gray}
			};
	}

	void draw_layer(const rect clipSpace, fx::layer_param_pass_t param) const override{
		viewport::draw_layer(clipSpace, param);

		if(param.is_top()){
			viewport_begin();

			{
				renderer().update_state(fx::pipeline_config{
					.draw_targets = {0b1},
					.pipeline_index = 2
				});

				auto region = camera.get_viewport();

				renderer() << graphic::draw::instruction::rect_aabb{
					.v00 = region.vert_00(),
					.v11 = region.vert_11(),
					.vert_color = {graphic::colors::white}
				};
				renderer().update_state(fx::pipeline_config{
									.draw_targets = {0b1},
									.pipeline_index = 0
								});
			}

			draw_system();


			viewport_end();
		}
	}
};

void set_cursors(scene& scene){
	auto& cm = scene.resources().cursor_collection_manager;

	cm.add_cursor<assets::builtin::cursor::default_cursor_regular>(style::cursor_type::regular);
	cm.add_cursor<assets::builtin::cursor::default_cursor_drag>(style::cursor_type::drag);
	cm.add_cursor<image_cursor>(style::cursor_type::textarea, assets::builtin::get_page()[assets::builtin::shape_id::textarea].value_or({}));

	cm.add_cursor<assets::builtin::cursor::default_cursor_arrow>(style::cursor_decoration_type::to_left,
		style::cursor_arrow_direction::left);
	cm.add_cursor<assets::builtin::cursor::default_cursor_arrow>(style::cursor_decoration_type::to_right,
		style::cursor_arrow_direction::right);
	cm.add_cursor<assets::builtin::cursor::default_cursor_arrow>(style::cursor_decoration_type::to_up,
		style::cursor_arrow_direction::up);
	cm.add_cursor<assets::builtin::cursor::default_cursor_arrow>(style::cursor_decoration_type::to_down,
		style::cursor_arrow_direction::down);
}

void make_styles(scene& scene){
	auto& sm = scene.resources().style_manager;


	{
		using namespace style;
		round_style templt{};

		static constexpr auto color_to_dark = [](graphic::color c){
			return c.set_value(.12f).shift_saturation(-.05f);
		};

		templt.edge.pal = math::lerp(pal::white, pal::dark, .155f);
		templt.edge = assets::builtin::default_round_square_boarder_thin;
		templt.back.pal = pal::dark;
		templt.back = assets::builtin::default_round_square_base;

		auto [itr, suc] = sm.register_style<elem_style_drawer>(referenced_ptr<round_style>{std::in_place, templt});
		auto& default_family = itr->second.get_default();

		{
			auto gst = templt;
			gst.edge.pal.set_cursor_ignored();
			gst.back.pal.set_cursor_ignored();
			default_family.set(family_variant::general_static, referenced_ptr<round_style>{std::in_place, gst});
		}

		{
			auto gst = templt;
			static constexpr auto baseColor = graphic::colors::aqua.create_lerp(graphic::colors::AQUA_SKY, .5f);
			gst.edge.pal = make_theme_palette(baseColor);
			gst.back.pal = make_theme_palette(color_to_dark(baseColor));
			default_family.set(family_variant::accent, referenced_ptr<round_style>{std::in_place, gst});
		}

		{
			auto gst = templt;
			static constexpr auto baseColor = graphic::colors::red_dusted.create_lerp(graphic::colors::white, .1f);
			gst.edge.pal = make_theme_palette(baseColor);
			gst.back.pal = make_theme_palette(color_to_dark(baseColor));
			default_family.set(family_variant::invalid, referenced_ptr<round_style>{std::in_place, gst});
		}

		{
			auto gst = templt;
			static constexpr auto baseColor = graphic::color::from_rgba8888(0XDBBB6AFF).create_lerp(graphic::colors::pale_yellow, .5f);
			gst.edge.pal = make_theme_palette(baseColor);
			gst.back.pal = make_theme_palette(color_to_dark(baseColor));
			default_family.set(family_variant::warning, referenced_ptr<round_style>{std::in_place, gst});
		}

		{
			auto gst = templt;
			static constexpr auto baseColor = graphic::colors::pale_green;
			gst.edge.pal = make_theme_palette(baseColor);
			gst.back.pal = make_theme_palette(color_to_dark(baseColor));
			default_family.set(family_variant::accepted, referenced_ptr<round_style>{std::in_place, gst});
		}

		{
			auto gst = templt;
			gst.base = gst.back;
			gst.base.pal.mul_alpha(.6f).mul_rgb(2.f);
			default_family.set(family_variant::solid, referenced_ptr<round_style>{std::in_place, gst});
		}

		{
			referenced_ptr<round_style_base_only> round_style{std::in_place};
			round_style->boarder.set(4);
			round_style->base = templt.base;
			round_style->back = templt.back;

			default_family.set(family_variant::base_only, std::move(round_style));
		}

		{
			referenced_ptr<round_style_edge_only> round_style{std::in_place};
			round_style->edge = templt.edge;

			default_family.set(family_variant::edge_only, std::move(round_style));
		}
	}

	auto elem_slice = sm.get_slice<style::elem_style_drawer>().value();

	{
		referenced_ptr<style::round_scroll_bar_style> round_scroll_bar_style{std::in_place};
		round_scroll_bar_style->bar_shape = assets::builtin::get_separator_row_patch();
		round_scroll_bar_style->bar_palette = style::pal::white.copy().mul_rgb(.8f);

		sm.register_style<style::scroll_pane_bar_drawer>(std::move(round_scroll_bar_style));
	}

	{
		referenced_ptr<style::thin_slider_drawer> round_scroll_bar_style{std::in_place};

		constexpr auto pal = style::make_theme_palette(graphic::colors::ROYAL.create_lerp(graphic::colors::aqua, .5f));
		round_scroll_bar_style->handle_palette = pal;
		round_scroll_bar_style->bar_shape = assets::builtin::get_separator_row_patch();
		round_scroll_bar_style->bar_palette = pal;

		sm.register_style<style::slider1d_drawer>(std::move(round_scroll_bar_style));
	}

	{
		constexpr auto pal = style::make_theme_palette(graphic::colors::ROYAL.create_lerp(graphic::colors::aqua, .5f));

		referenced_ptr<style::round_slider_drawer> drawer{std::in_place};
		drawer->handle_shape = assets::builtin::default_round_square_base;
		drawer->bar_shape = assets::builtin::default_round_square_base;

		drawer->handle_palette = pal;
		drawer->bar_palette = pal.copy().mul_rgb(.8f);
		drawer->bar_back_palette = pal.copy().mul_rgb(.5f);

		drawer->vert_margin = 5.f;

		sm.register_style<style::slider1d_drawer>(std::move(drawer));
	}

	{
		style::side_bar_style style{};
		const auto region = assets::builtin::get_page()[assets::builtin::shape_id::side_bar].value_or({});
		style.boarder.set(2);
		style.bar.pal = style::pal::white;
		style.bar = image_row_patch{region, region->uv.get_region(), 18, 18, 4};
		style.back.pal = style::pal::dark;
		style.back = assets::builtin::default_round_square_base;


		{
			auto s = style;
			s.boarder.left = 16;
			s.pos = style::side_bar_pos::left;
			elem_slice.insert_or_assign("side_bar_left", referenced_ptr<style::side_bar_style>{std::in_place, s});
		}
		{
			auto s = style;
			s.boarder.right = 16;
			s.pos = style::side_bar_pos::right;
			elem_slice.insert_or_assign("side_bar_right", referenced_ptr<style::side_bar_style>{std::in_place, s});
		}
		{
			auto s = style;
			s.boarder.top = 16;
			s.pos = style::side_bar_pos::top;
			elem_slice.insert_or_assign("side_bar_top", referenced_ptr<style::side_bar_style>{std::in_place, s});
		}
		{
			auto s = style;
			s.boarder.bottom = 16;
			s.pos = style::side_bar_pos::bottom;
			elem_slice.insert_or_assign("side_bar_bottom", referenced_ptr<style::side_bar_style>{std::in_place, s});
		}


		{
			referenced_ptr<style::round_scroll_bar_style> round_scroll_bar_style{std::in_place};
			round_scroll_bar_style->bar_shape = assets::builtin::get_separator_row_patch();
			round_scroll_bar_style->bar_palette = style::pal::white.copy().mul_rgb(.8f);
			sm.register_style<style::scroll_pane_bar_drawer>(std::move(round_scroll_bar_style));

		}

	}

	sm.register_style<style::slider2d_drawer>(referenced_ptr<style::default_slider2d_drawer>{std::in_place});

}


void example_scene::draw_at(const elem& elem){
	auto c = get_region().intersection_with(elem.bound_abs());
	const auto bound = c.round<int>();

	auto& cfg = pass_config;

	for(unsigned i = 0; i < cfg.size(); ++i){
		renderer().update_state(cfg[i].begin_config);

		renderer().update_state({},
		                        fx::batch_draw_mode::def,
		                        graphic::draw::instruction::make_state_tag(fx::state_type::push_constant, 0x00000010)
		);


		elem.draw_layer(c, {i});

		if(cfg[i].end_config)
			renderer().update_state(fx::blit_config{
					.blit_region = {bound.src, bound.extent()},
					.pipe_info = cfg[i].end_config.value()
				});
	}


	if(auto tail = cfg.get_tail_blit())
		renderer().update_state(fx::blit_config{
				.blit_region = {bound.src, bound.extent()},
				.pipe_info = tail.value()
			});
}

void example_scene::draw_impl(rect clip){
	renderer().init_projection();


	{
		viewport_guard _{renderer(), get_region()};

		for (auto&& elem : tooltip_manager_.get_draw_sequence()){
			if(elem.belowScene){
				draw_at(*elem.element);
			}
		}

		draw_at(root());

		for (const auto & draw_sequence : overlay_manager_.get_draw_sequence()){
			draw_at(*draw_sequence);

		}

		for (auto&& elem : tooltip_manager_.get_draw_sequence()){
			if(!elem.belowScene){
				draw_at(*elem.element);
			}
		}
	}

	if(input_handler_.inputs_.is_cursor_inbound()){
		renderer().update_state(fx::pipeline_config{
			.draw_targets = {0b1},
			.pipeline_index = 1
		});

		renderer().update_state(
			{}, 1.f,
			graphic::draw::instruction::make_state_tag(fx::state_type::push_constant, 0x00000010)
		);

		auto region = current_cursor_drawers_.draw(static_cast<scene&>(*this), resources_->cursor_collection_manager.get_cursor_size());

		renderer().update_state(gui::fx::blit_config{
			{
				.src = region.src.as<int>(),
				.extent = region.extent().as<int>()
			},
			{.pipeline_index = 1}});
	}
}

ui_outputs build_main_ui(backend::vulkan::context& ctx, renderer_frontend renderer){
	auto& ui_root = gui::global::manager;
	auto& res = ui_root.add_scene_resources("main");
	const auto scene_add_rst = ui_root.add_scene<example_scene, gui::loose_group>("main", res, true, std::move(renderer));
	scene_add_rst.root_group.on_context_sync_bind();

	// scene_add_rst.scene.resize(math::rect_ortho{tags::from_extent, {}, ctx.get_extent().width, ctx.get_extent().height}.as<float>());
	auto& scene = scene_add_rst.scene;
	auto& root = scene_add_rst.root_group;

	scene.drop_and_reset_communicate_async_task_queue_size(1);

	make_styles(scene);
	set_cursors(scene);

	scene.pass_config = {
			{
				fx::scene_render_pass_config::value_type{
					.begin_config = {
						.draw_targets = 0b1,
					},
					.end_config = std::nullopt
				},
				{
					.begin_config = {
						.draw_targets = 0b10,
					},
					.end_config = std::nullopt
				}
			},
			fx::blit_pipeline_config{}
		};

	scene.set_native_communicator<backend::glfw::communicator>(ctx.window().get_handle());
	scene.get_communicator()->set_native_cursor_visibility(false);

	auto e = scene.create<scaling_stack>();
	e->set_fill_parent({true, true});
	auto& mroot = static_cast<scaling_stack&>(root.insert(0, std::move(e)));

	ui_outputs result{&scene};


	auto make_create_table = [&] -> std::vector<test_entry> {
		std::vector<test_entry> tests{
				test_entry{
					"csv", [](cpd::data_table& table){
						table._debug_identity = 114;
						table.get_item() = cpd::data_table_desc::from_csv(LR"(D:\projects\untitled\shader info.csv)");
						table.notify_isolated_layout_changed();
					}
				},
				test_entry{
					"text input", [&](scroll_pane& pane){
						pane.create([&](sequence& sequence){
							sequence.template_cell.set_pad({4, 4});

							auto slider = sequence.emplace_back<gui::slider1d_with_output>();
							slider->set_smooth_scroll(true);
							slider->set_smooth_jump(false);
							slider->set_smooth_drag(true);
							slider->bar_handle_extent = {40};
							referenced_ptr<style::round_slider_drawer> drawer{std::in_place};
							drawer->handle_shape = assets::builtin::default_round_square_base;
							drawer->bar_shape = assets::builtin::default_round_square_base;
							drawer->handle_palette = style::pal::white;
							drawer->bar_palette = style::pal::pastel_gray.copy().mul_rgb(.7f);
							drawer->bar_back_palette = style::pal::pastel_gray.copy().mul_rgb(.2f);
							drawer->vert_margin = 5.f;


							slider->drawer_ = std::move(drawer);
							slider.cell().set_size(60);

							auto& progNode = slider->get_provider();

							sequence.create_back([&](progress_bar& prog){
								prog.progress.set_state(progress_state::approach_smooth);
								prog.progress.set_speed(.0001f);
								auto& t = prog.request_receiver();
								react_flow::connect_chain(progNode, t);
							}).cell().set_size(60);

							{
								auto label = sequence.create_back([&](direct_label& l){
								});
								label.cell().set_pending();

								auto& ln = label->request_react_node<direct_label_text_prov>();
								auto& trans = label->request_embedded_react_node(react_flow::make_transformer(
									[](std::u32string_view sv){
										return typesetting::tokenized_text{sv};
									}));

								sequence.create_back([&](text_edit_prov& area){
									area.set_on_changed_interval(30.f);
									react_flow::connect_chain(area.get_provider(), trans, ln);
								}).cell().set_pending();
							}
						});
					}
				},
				test_entry{
					"file reader", [](csv_file_reader& table){
					}
				},
				test_entry{
					"sliders", [&](scroll_adaptor<sequence>& pane){
						sequence& s = pane.get_elem();
						pane.set_style();

						util::post_elem_async_task(s, [](gui::sequence& seq){
							return elem_async_yield_task{
									seq,
									[](elem& e){
										std::println(std::cerr, "Task Begin: current thread: {}",
										             std::this_thread::get_id());
										std::this_thread::sleep_for(std::chrono::milliseconds(500));
										std::println(std::cerr, "Task End: current thread: {}",
										             std::this_thread::get_id());
										return 114;
									},
									[](elem& e, int val){
										std::println(std::cerr, "Task Done: current thread: {} - {}",
										             std::this_thread::get_id(), val);
									}
								};
						});

						s.set_expand_policy(layout::expand_policy::prefer);
						s.template_cell.set_pending();
						s.template_cell.pad = {16, 4};
						s.set_has_smooth_pos_animation(false);
						{
							auto hdl = s.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
							                                             "Bloom Sample Scale", 50.f);
							hdl->set_style();
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(.25f);

							auto& trans = hdl->add_relay_func([](float val){
								return math::lerp(0.25f, 4.f, val);
							});
							hdl->add_formatter_func([](float val){
								return std::format("{:.2f}", val);
							});
							result.shader_bloom_scale = &trans;
						}

						{
							auto hdl = s.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
							                                             "BloomSrcFactor", 50.f);
							hdl->set_style();
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(.5f);

							auto& trans = hdl->add_relay(react_flow::make_transformer([](float val){
								return math::lerp(0.f, 2.f, val);
							}));
							auto& formatter = hdl->request_embedded_react_node(react_flow::make_transformer(
								[](float val){
									return std::format("{:.2f}", val);
								}));
							react_flow::connect_chain(trans, formatter, hdl->get_display_text_receiver());

							result.shader_bloom_src_factor = &trans;
						}

						{
							auto hdl = s.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
							                                             "BloomDstFactor", 50.f);
							hdl->set_style();
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(.5f);

							auto& trans = hdl->add_relay_func([](float val){
								return math::lerp(0.f, 2.f, val);
							});
							hdl->add_formatter_func([](float val){
								return std::format("{:.2f}", val);
							});
							result.shader_bloom_dst_factor = &trans;
						}

						{
							auto hdl = s.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
							                                             "BloomMixFactor", 50.f);
							hdl->set_style();
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(.5f);
							result.shader_bloom_mix_factor = &hdl.elem().get_slider_provider();
						}

						{
							s.emplace_back<row_separator>().cell().set_size(8);
						}

						{
							auto hdl = s.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
							                                             "HighlightThres", 50.f);
							hdl->set_style();
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(.25f);
							auto& trans = hdl->add_relay_func([](float val){
								return math::lerp(0.5f, 2.5f, val);
							});
							hdl->add_formatter_func([](float val){
								return std::format("{:.2f}", val);
							});
							result.highlight_filter_threshold = &trans;
						}

						{
							auto hdl = s.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
							                                             "HighlightSmooth", 50.f);
							hdl->set_style();
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(.5f);
							hdl->add_formatter_func([](float val){
								return std::format("{:.2f}", val);
							});
							result.highlight_filter_smooth = &hdl.elem().get_slider_provider();
						}

						{
							s.emplace_back<row_separator>().cell().set_size(8);
						}

						{
							auto hdl = s.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
							                                             "Contrast", 50.f);
							hdl->set_style();
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(1.f);
							hdl->add_formatter_func([](float val){
								return std::format("{:.2f}", val);
							});
							result.tonemap_contrast = &hdl->get_slider_provider();
						}
						{
							auto hdl = s.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
							                                             "Exposure", 50.f);
							hdl->set_style();
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(.5f);
							auto& trans = hdl->add_relay_func([](float val){
								return math::lerp(0.f, 2.f, val);
							});
							hdl->add_formatter_func([](float val){
								return std::format("{:.2f}", val);
							});
							result.tonemap_exposure = &hdl->get_slider_provider();
						}
						{
							auto hdl = s.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
							                                             "Saturation", 50.f);
							hdl->set_style();
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(1.f);
							hdl->add_formatter_func([](float val){
								return std::format("{:.2f}", val);
							});
							result.tonemap_saturation = &hdl->get_slider_provider();
						}
						{
							auto hdl = s.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major,
							                                             "Gamma", 50.f);
							hdl->set_style();
							hdl->get_slider().set_smooth_drag(true);
							hdl->get_slider().set_progress(math::map(1.2f, 0.5f, 3.f, 0.f, 1.f));
							auto& trans = hdl->add_relay_func([](float val){
								return math::lerp(0.5f, 3.f, val);
							});
							hdl->add_formatter_func([](float val){
								return std::format("{:.2f}", val);
							});
							result.tonemap_gamma = &trans;
						}
					}
				},
				test_entry{
					"collapsers", [&](scroll_adaptor<sequence>& pane){
						pane.set_layout_policy(layout::layout_policy::vert_major);

						sequence& s = pane.get_elem();
						s.set_layout_policy(layout::layout_policy::vert_major);

						s.set_style();
						s.set_expand_policy(layout::expand_policy::prefer);
						s.template_cell.set_pending();
						s.template_cell.set_pad({6.f});

						for(int i = 0; i < 14; ++i){
							s.create_back([&](collapser& c){
								c.set_update_opacity_during_expand(true);
								c.set_expand_cond(collapser_expand_cond::inbound);

								c.emplace_head<elem>();
								c.set_head_size(50);
								c.create_body([](table& e){
									e.interactivity = interactivity_flag::enabled;
									e.set_tooltip_state(
										{
											.layout_info = tooltip::align_meta{
												.follow = tooltip::anchor_type::owner,
												.attach_point_spawner = align::pos::top_left,
												.attach_point_tooltip = align::pos::top_right,
											},
										}, [](table& tooltip){
											using namespace gui;
											struct dialog_creator : elem{
												[[nodiscard]] dialog_creator(
													gui::scene& scene, elem* parent)
													: elem(scene, parent){
													interactivity = interactivity_flag::enabled;
												}

												events::op_afterwards on_click(
													const events::click event,
													std::span<elem* const> aboves) override{
													if(event.key.on_release()){
														get_scene().create_overlay({
																.extent = {
																	{
																		layout::size_category::passive,
																		.4f
																	},
																	{
																		layout::size_category::scaling,
																		1.f
																	}
																},
																.align = align::pos::center,
															}, [](table& e){
																e.end_line().emplace_back<elem>();
																e.end_line().emplace_back<elem>();
																e.end_line().emplace_back<elem>();
																e.end_line().emplace_back<elem>();
																e.end_line().emplace_back<elem>();
															});
													}
													return events::op_afterwards::intercepted;
												}
											};
											tooltip.emplace_back<dialog_creator>().cell().set_size({
													160, 60
												});
										});
									e.set_entire_align(align::pos::top_left);
									for(int k = 0; k < 5; ++k){
										e.emplace_back<elem>().cell().set_size({250, 60});
									}
									// e.interactivity = gui::interactivity_flag::enabled;
								});

								c.set_head_body_transpose(i & 1);
							});
						}
					}
				},
				test_entry{
					"menu", [](menu& menu){
						menu.set_expand_policy(layout::expand_policy::passive);
						menu.set_head_size(90);
						menu.get_head_template_cell().set_size(240).set_pad({4, 4});

						for(int i = 0; i < 4; ++i){
							auto hdl = menu.create_back(
								[](elem& e){
									e.interactivity = interactivity_flag::enabled;
								}, [&](sequence& e){
									e.set_has_smooth_pos_animation(true);
									e.set_expand_policy(layout::expand_policy::passive);
									e.template_cell.set_pad({4, 4});
									for(int j = 0; j < i + 1; ++j){
										e.emplace_back<elem>();
									}
								});
						}
					}
				},
				test_entry{
					"table/check box", [](scroll_pane& pane){
						pane.create([](table& table){
							table.set_expand_policy(layout::expand_policy::prefer);
							table.set_entire_align(align::pos::center);
							table.template_cell.pad.set(4);

							style::family_variant family_variants[]{
									style::family_variant::general,
									style::family_variant::general_static,
									style::family_variant::solid,
									style::family_variant::base_only,
									style::family_variant::edge_only,
									style::family_variant::accent,
									style::family_variant::accepted,
									style::family_variant::warning,
									style::family_variant::invalid,
								};

							for (auto family_variant : family_variants){
								auto check_box = table.emplace_back<gui::check_box>(std::in_place);
								check_box->icons[1].components.color = {graphic::colors::pale_green};
								check_box.cell().set_size({60, 60});
								check_box.cell().unsaturate_cell_align = align::pos::none;

								auto receiver = table.emplace_back<label>();
								receiver->set_fit();
								receiver->set_style(receiver->get_style_manager().get_default<style::elem_style_drawer>(family_variant));
								receiver->interactivity = interactivity_flag::enabled;

								auto& listener = receiver->request_embedded_react_node(react_flow::make_listener(
									[&e = receiver.elem()](bool i){
										e.set_toggled(i);
										if(i){
											e.set_text("Toggled");
										} else{
											e.set_text("");
										}
									}));
								listener.connect_predecessor(check_box->get_prov());

								receiver.cell().set_end_line();
							}

							{
								auto sep = table.emplace_back<row_separator>();
								sep.cell().set_height(20).set_width_passive(.85f).saturate = true;
								sep.cell().margin.set_vert(4);
								sep.cell().set_end_line();
							}

							{
								auto sep = table.create_back([](overflow_sequence& seq){
									seq.set_layout_policy(layout::layout_policy::vert_major);
									seq.template_cell.set_size(120).set_pad({2, 2});
									auto [_, cell] = seq.create_overflow_elem([](icon_frame& i){
										i.set_style(i.get_style_manager().get_default<style::elem_style_drawer>(style::family_variant::base_only));
										i.interactivity = interactivity_flag::enabled;
									}, gui::assets::builtin::shape_id::more);
									cell.set_size({layout::size_category::scaling});

									for(unsigned i = 0; i < 12; ++i){
										seq.create_back([&](label& l){
											l.set_text(std::format("{}", i));
											l.set_fit();
											l.set_style(l.get_style_manager().get_default<style::elem_style_drawer>(style::family_variant::base_only));
											l.interactivity = interactivity_flag::enabled;
										});
									}
									seq.set_split_index(2);
								});
								sep.cell().set_height(80).saturate = true;
								sep.cell().margin.set_vert(4);
								sep.cell().set_end_line();
							}

							for(int i = 0; i < 4; ++i){
								table.emplace_back<elem>().cell().set_size({120, 120});
								table.emplace_back<elem>();
								table.end_line();
							}
						});
					}
				},
				test_entry{
					"grid", [](scroll_pane& pane){
						pane.create(
							[](grid& table){
								table.set_has_smooth_pos_animation(true);
								table.set_expand_policy(layout::expand_policy::prefer);
								table.emplace_back<elem>().cell().extent = {
										{.type = grid_extent_type::src_extent, .desc = {0, 2},},
										{.type = grid_extent_type::src_extent, .desc = {0, 1},},
									};
								table.emplace_back<elem>().cell().extent = {
										{.type = grid_extent_type::src_extent, .desc = {1, 2},},
										{.type = grid_extent_type::src_extent, .desc = {1, 1},},
									};
								table.emplace_back<elem>().cell().extent = {
										{.type = grid_extent_type::src_extent, .desc = {2, 2},},
										{.type = grid_extent_type::src_extent, .desc = {2, 1},},
									};
								table.emplace_back<elem>().cell().extent = {
										{.type = grid_extent_type::src_extent, .desc = {0, 4},},
										{.type = grid_extent_type::src_extent, .desc = {2, 2},},
									};
								table.emplace_back<elem>().cell().extent = {
										{.type = grid_extent_type::margin, .desc = {1, 1},},
										{.type = grid_extent_type::src_extent, .desc = {5, 1},},
									};
								table.emplace_back<elem>().cell().extent = {
										{.type = grid_extent_type::margin, .desc = {4, 1},},
										{.type = grid_extent_type::src_extent, .desc = {7, 1},},
									};
								table.emplace_back<elem>().cell().extent = {
										{.type = grid_extent_type::src_extent, .desc = {5, 6},},
										{.type = grid_extent_type::margin, .desc = {0, 0},},
									};
							}, math::vector2<grid_dim_spec>{
								grid_uniformed_mastering{6, 300.f, {4, 4}},
								grid_uniformed_passive{8, {4, 4}}
							});
						pane.set_layout_policy(layout::layout_policy::vert_major);
					}
				},
				test_entry{
					"drag/label", [](scroll_pane& pane){
						pane.create(
							[](split_pane& table){
								constexpr static auto test_text =
									R"({s:*.5}Basic{size:64} Token {size:128}Test{//}
{u}AVasdfdjknfhvbawhboozx{/}cgiuTeWaVoT.P.àáâã ä åx̂̃ñ
{color:#FF0000}Red Text{/} and {font:gui}Font Change{/}

Escapes Test:
1. Backslash: \\ {_}(Should see single backslash){/}
2. Braces {size:128}with{/} slash: \{ and \} (Should see literal { and })
3. Braces with double: {{ and }} (Should see literal { and })

Line Continuation Test:
This is a very long line that \
{font:gui}should be joined together{/} \
without newlines.

{feature:liga}0 ff {feature:-liga}1 ff {feature:liga} 2 ff{feature} 3 ff{feature} 4 ff

O{ftr:liga}off file flaff{/} ff

Edge Cases:
1. Token without arg: {bold}Bold Text{/bold}
2. {u}Unclosed brace{/}: { This is just text because no closing bracket
3. Unknown escape: \z (Should show 'z')
4. Colon in arg: {log:Time:12:00} (Name="log", Arg="Time:12:00")
)";

								table.set_expand_policy(layout::expand_policy::prefer);
								using namespace std::literals;
								table.create_head([](split_pane& inner){
									inner.set_expand_policy(layout::expand_policy::passive);
									inner.set_layout_policy(layout::layout_policy::hori_major);
									inner.create_head([](scroll_adaptor<label>& p){
										p.set_style();
										auto& l = p.get_elem();
										// l.set_tokenizer_tag(typesetting::tokenize_tag::raw);
										l.set_expand_policy(layout::expand_policy::prefer);
										l.set_fit(false);
										l.set_text(test_text);
									});
									inner.create_body([](scroll_adaptor<label>& p){
										p.set_overlay_bar(true);
										// label.set_style();
										auto& l = p.get_elem();
										l.set_tokenizer_tag(typesetting::tokenize_tag::raw);
										l.set_expand_policy(layout::expand_policy::prefer);
										l.set_fit(false);
										l.set_text(test_text);
										p.set_layout_policy(layout::layout_policy::vert_major);
									});
								});


								table.create_body([](split_pane& inner){
									inner.set_expand_policy(layout::expand_policy::passive);
									inner.set_layout_policy(layout::layout_policy::hori_major);
									inner.create_head([](scroll_pane& label){
										label.set_style();
										label.create([](gui::label& l){
											l.set_expand_policy(layout::expand_policy::prefer);
											l.set_fit(false);
											l.set_typesetting_config(typesetting::layout_config{
													.direction = typesetting::layout_direction::rtl,
												});
											l.set_text(test_text);
										});
									});
									inner.create_body([](scroll_pane& label){
										label.set_overlay_bar(true);
										label.set_style();
										label.create([&](gui::label& l){
											l.set_expand_policy(layout::expand_policy::prefer);
											l.set_fit(false);
											l.set_typesetting_config(typesetting::layout_config{
													.direction = typesetting::layout_direction::btt
												});
											l.set_text(test_text);
										});
										label.set_layout_policy(layout::layout_policy::vert_major);
									});
								});
							});
						pane.set_layout_policy(layout::layout_policy::vert_major);
					}
				},
				test_entry{
					"color picker", [](scroll_pane& pane){
						pane.create(
							[](table& table){
								table.set_expand_policy(layout::expand_policy::prefer);
								table.create_back([](cpd::rgb_picker& picker){
								}).cell().set_size({600, 600});
							});
						pane.set_layout_policy(layout::layout_policy::none);
					}
				},
				test_entry{
					"view port", [](scaling_stack& stack){
						auto vphld = stack.emplace_back<vp>();
						vphld.cell().region_scale = {.0f, .0f, 1.f, 1.f};
						auto hdl = stack.emplace_back<cpd::named_slider>(layout::layout_policy::hori_major, "Speed", 50.f);
						hdl->get_slider().set_smooth_drag(true);
						hdl->get_slider().set_progress(math::map(1.f, 0.f, 3.f, 0.f, 1.f));
						hdl->set_expand_policy(layout::expand_policy::passive);
						hdl->set_max_extent({std::numeric_limits<float>::infinity(), 140});
						hdl->set_style(hdl->get_style_manager().get_default<style::elem_style_drawer>(style::family_variant::solid));

						hdl.cell().region_scale = {.0f, .0f, .5f, .5f};
						hdl.cell().region_align = align::pos::bottom_left;
						hdl.cell().unsaturate_cell_elem_align = align::pos::bottom_left;

						auto& trans = hdl->add_relay_func([](float val){
							return math::lerp(0.0f, 3.f, val);
						});
						hdl->add_formatter_func([](float val){
							return std::format("{:.2f}", val);
						});
						auto& n = hdl->request_embedded_react_node(react_flow::make_listener([&vp = vphld.elem()](float val){
							vp.update_speed = val;
						}));
						trans.connect_successor(n);
						hdl->get_slider_provider().pull_and_push(false);
					}
				}
			};

		return tests;
	};


	action::push_runnable_action(root, [](elem& e){
		e.set_style();
	});
	mroot.set_style();

	const auto menu_hdl = mroot.emplace_back<menu>(layout::layout_policy::vert_major, [](gui::direct_label& l){
		l.set_fit_type(label_fit_type::scl);
		l.set_style();
		l.text_entire_align = align::pos::center;
		l.set_tokenized_text(typesetting::tokenized_text{
			U"{i}{f:code}"
			U"{#8999F9}{+#223344}X{//}"
			U"{#F0969D}{b}r{//}"
			U"{#9DE6D1aa}g{/}"
			U"{u}u{/}"
			U"i{/i}"
			U"{s:*.4} {/}{w:r}{s:*.9}Test"
		});
	});
	menu_hdl->set_expand_policy(layout::expand_policy::passive);
	menu_hdl->set_head_size({layout::size_category::mastering, 100});
	menu_hdl.cell().region_scale = {.0f, .0f, .8f, 1.f};
	menu_hdl.cell().region_align = align::pos::left;

	menu_hdl->get_head_template_cell().set_pending();
	menu_hdl->get_head_template_cell().set_pad({4, 4});

	for(const auto& [idx, creator] : make_create_table() | std::views::enumerate){
		menu_hdl->push_back(
			elem_ptr{
				menu_hdl->get_scene(), &menu_hdl.elem(), [&](label& label){
					label.sync_run([](elem& el){
						el.set_style(el.get_style_manager().get_default<style::elem_style_drawer>(style::family_variant::base_only));
					});
					label.set_fit_type(label_fit_type::scl);
					label.set_text(std::format("[{}]-{}", idx, creator.name));
					label.text_entire_align = align::pos::center;
					label.interactivity = interactivity_flag::enabled;
					label.set_transform_config({
							.rotation = text_rotation::deg_270
						});
				}
			}, creator.creator(menu_hdl->get_scene(), &menu_hdl.elem()));
	}

	return result;
}

void clear_main_ui(){
	auto& ui_root = gui::global::manager;
	ui_root.erase_scene("main");
	ui_root.erase_resource("main");
}
}

