#include <vulkan/vulkan.h>

import std;

import mo_yanxi.vk;
import mo_yanxi.vk.cmd;

import mo_yanxi.backend.vulkan.context;
import mo_yanxi.backend.glfw.window;
import mo_yanxi.backend.application_timer;
import mo_yanxi.backend.vulkan.renderer;

import mo_yanxi.math.rand;
import mo_yanxi.math.interpolation;

import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.graphic.image_atlas;
import mo_yanxi.graphic.compositor.manager;
import mo_yanxi.graphic.compositor.post_process_pass;
import mo_yanxi.graphic.compositor.post_process_pass_with_ubo;
import mo_yanxi.graphic.compositor.bloom;
import mo_yanxi.graphic.shaderc;
import mo_yanxi.graphic.trail;

import mo_yanxi.gui.infrastructure;
import mo_yanxi.gui.elem.group;
import mo_yanxi.gui.global;
import mo_yanxi.gui.assets.manager;
import mo_yanxi.gui.renderer.frontend;
import mo_yanxi.gui.examples;
import mo_yanxi.gui.fx.config;
import mo_yanxi.gui.fx.fringe;
import mo_yanxi.gui.fx.instruction_extension;

import mo_yanxi.gui.assets;
import mo_yanxi.gui.image_regions;

import mo_yanxi.font;
import mo_yanxi.font.plat;
import mo_yanxi.font.manager;
import mo_yanxi.typesetting;
import mo_yanxi.typesetting.util;
import mo_yanxi.typesetting.rich_text;

import mo_yanxi.react_flow;
import mo_yanxi.react_flow.common;

template <std::size_t Stride, typename Tup, typename Proj, std::size_t Offset = 0>
using tuple_stride_t = decltype([]{
	static_assert(Stride > 0);
	static constexpr auto total = std::tuple_size_v<Tup>;
	static constexpr auto count_raw = total / Stride;
	static constexpr auto count = Offset < (total - count_raw * Stride) ? count_raw + 1 : count_raw;
	return []<std::size_t ...Idx>(std::index_sequence<Idx...>){
		return std::type_identity<std::tuple<std::invoke_result_t<Proj, std::tuple_element_t<Idx * Stride + Offset, Tup>>...>>{};
	}(std::make_index_sequence<count>{});
}())::type;

template <typename FWIT, typename ...Args>
auto copy_classify(FWIT begin, FWIT end, Args&& ...args){
	static_assert(sizeof...(Args) & 1);
	using ParamTup = std::tuple<Args&&...>;
	using IteratorFwdTup = tuple_stride_t<2, ParamTup, std::identity, 1>;
	using PredFwdTup = tuple_stride_t<2, ParamTup, decltype([](auto&& decay) -> auto {return decay;})>;

	auto forward = std::forward_as_tuple(std::forward<Args>(args)...);
	auto pred_tup = [&]<std::size_t ...Idx>(std::index_sequence<Idx...>){
		return PredFwdTup{std::get<Idx * 2>(std::move(forward)) ...};
	}(std::make_index_sequence<std::tuple_size_v<PredFwdTup>>{});

	auto iter_tup = [&]<std::size_t ...Idx>(std::index_sequence<Idx...>){
		return IteratorFwdTup{std::get<Idx * 2 + 1>(std::move(forward)) ...};
	}(std::make_index_sequence<std::tuple_size_v<IteratorFwdTup>>{});

	auto write = []<typename OutputItr, typename Val>(OutputItr& itr, Val&& input){
		*itr = std::forward<Val>(input);
		++itr;
	};

	auto cur = begin;
	while(cur != end){
		auto&& value = *cur;
		bool any = [&]<std::size_t ...Idx>(std::index_sequence<Idx...>){
			return ([&]<std::size_t I>(){
				auto&& pred = std::get<I>(pred_tup);
				if(pred(value)){
					std::get<I>(pred_tup) = iter_tup;
					write(std::get<I>(pred_tup), std::forward<decltype(value)>(value));
					return true;
				}
				return false;
			}.template operator()<Idx>() || ...);
		}(std::make_index_sequence<std::tuple_size_v<PredFwdTup>>{});
		if(!any){
			write(std::get<std::tuple_size_v<PredFwdTup>>(pred_tup), std::forward<decltype(value)>(value));
		}
		++cur;
	}

	return iter_tup;
}

struct alignas(16) high_light_filter_args{
	float threshold{1.3f};
	float smoothness{.5f};
	float max_brightness{10}; // 新增：用于抑制超高亮像素，防止 Bloom 闪烁
};

struct alignas(16) tonemap_args{
	float exposure{1};
	float contrast{1};
	float gamma{1};
	float saturation{1};
};

void app_run(
	mo_yanxi::backend::vulkan::context& ctx,
	mo_yanxi::backend::vulkan::renderer& renderer,
	mo_yanxi::graphic::compositor::manager& manager,
	mo_yanxi::vk::command_buffer& cmdBUf
	){

	using namespace mo_yanxi;

	backend::application_timer timer{backend::application_timer<double>::get_default()};


	graphic::uniformed_trail trail{60, .75f};
	trail.shrink_interval *= 2.f;

	while(!ctx.window().should_close()){

		ctx.window().poll_events();
		timer.fetch_time();

		gui::global::manager.update(timer.global_delta_tick());
		gui::global::manager.layout();


		auto& current_focus = gui::global::manager.get_current_focus();
		auto& r = current_focus.renderer();

		trail.update(timer.global_delta_tick(), current_focus.get_cursor_pos(), 2);

		renderer.batch_host.begin_rendering();
		renderer.batch_host.get_data_group_non_vertex_info().push_default(gui::fx::ui_state(
			r.get_region().extent(),
			timer.global_time()
		));
		renderer.batch_host.get_data_group_non_vertex_info().push_default(gui::fx::slide_line_config{});

		r.init_projection();

		r.update_state(gui::fx::pipeline_config{});
		r.update_state(
			{},
			gui::fx::batch_draw_mode::def,
			gui::make_state_tag(gui::fx::state_type::push_constant, VK_SHADER_STAGE_FRAGMENT_BIT));
		r.update_state(gui::fx::blend::pma::standard);
		r.update_state(r.get_full_screen_scissor());
		r.update_state(r.get_full_screen_viewport());

		if(true){
			using namespace graphic::draw::instruction;

			{
				// gui::fringe::poly_partial_with_cap(r, poly_partial{
				// 		.pos = {600, 400},
				// 		.segments = 64,
				// 		.radius = {120, 130},
				// 		.range = {0, .45f},
				// 		.color = {
				// 			graphic::colors::white, graphic::colors::white, graphic::colors::aqua, graphic::colors::aqua
				// 		}
				// 	});
				//
				//
				// gui::fringe::poly_partial_with_cap(r, poly_partial{
				// 		.pos = {800, 400},
				// 		.segments = 64,
				// 		.radius = {120, 130},
				// 		.range = {0, -.45f},
				// 		.color = {
				// 			graphic::colors::white, graphic::colors::ACID, graphic::colors::aqua, graphic::colors::CRIMSON
				// 		}
				// 	});

				// gui::fringe::curve_with_cap(r, {
				// 		.param = curve_trait_mat::bezier.apply_to({300, 300}, {400, 500}, {800, 300}, {900, 500}),
				// 		.stroke = {12, 12},
				// 		.segments = 32,
				// 		.color = {graphic::colors::white}
				// 	});

				// auto gen = [](float cx, float cy, float a, float b) -> std::vector<math::vec2> {
				// 	// 使用 8 个控制点来近似
				// 	// 为了让三次 B 样条闭合，通常需要将前 3 个点重复添加到末尾
				// 	std::vector<math::vec2> points = {};
				// 	for (int i = 0; i < 12; ++i) {
				// 		float angle = i * (360.0f / 12.0f) * (std::numbers::pi_v<float> / 180.0f);
				//
				// 		points.push_back({
				// 			cx + a * std::cos(angle),
				// 			cy + b * std::sin(angle)
				// 		});
				// 	}
				// 	return points;
				// };
				//
				// {
				// 	auto verts = gen(800, 500, 400, 300);
				// 	for(unsigned i = 0; i < verts.size(); ++i){
				// 		gui::fringe::curve(r, {
				// 			.param = curve_trait_mat::b_spline.apply_to(verts[i], verts[(i + 1) % verts.size()], verts[(i + 2) % verts.size()], verts[(i + 3) % verts.size()]),
				// 			.stroke = {12, 12},
				// 			.segments = 6,
				// 			.color = {graphic::colors::white}
				// 		}, 2);
				// 	}
				// }
				// auto verts = gen(1200, 900, 300, 200);
				// for(unsigned i = 0; i < verts.size(); ++i){
				// 	gui::fringe::curve(r, {
				// 		.param = curve_trait_mat::b_spline.apply_to(verts[i], verts[(i + 1) % verts.size()], verts[(i + 2) % verts.size()], verts[(i + 3) % verts.size()]),
				// 		.stroke = {12, 12},
				// 		.segments = 6,
				// 		.color = {graphic::colors::white}
				// 	}, 2);
				// }
				//
				// gui::fringe::line_context line_ctx{r.get_mem_pool()};
				// line_ctx.push({math::vec2{450, 600}, 12, 0, {graphic::colors::white, graphic::colors::white}});
				// line_ctx.push({math::vec2{400, 560}, 12, 0, {graphic::colors::white, graphic::colors::white}});
				// line_ctx.push({math::vec2{300, 500}, 12, 0, {graphic::colors::white, graphic::colors::white}});
				//
				// line_ctx.push({math::vec2{250, 450}, 12, 0, {graphic::colors::white, graphic::colors::white}});
				// line_ctx.push({math::vec2{1200, 500}, 12, 0, {graphic::colors::white, graphic::colors::white}});
				//
				// line_ctx.add_cap();
				// line_ctx.add_fringe_cap(2, 2);
				// line_ctx.dump_mid(r, line_segments{});
				// line_ctx.dump_fringe_inner(r, line_segments{}, 1.75f);
				// line_ctx.dump_fringe_outer(r, line_segments{}, 1.75f);

				// line_ctx.dump_fringe_cap_src(r, line_segments{}, 3.f, 4.f);
				// line_ctx.dump_fringe_cap_dst(r, line_segments{}, 12.f, 4.f);
				// line_ctx.dump_fringe_inner(r, line_segments_closed{}, 2.f);
				// line_ctx.dump_fringe_outer(r, line_segments_closed{}, 2.f);

				// gui::fringe::curve(r, {
				// 		.param = curve_trait_mat::bezier.apply_to({300, 300}, {400, 400}, {800, 400}, {900, 500}),
				// 	.margin = {0, 1.01f},
				// 		.stroke = {4, 6},
				// 		.segments = 1,
				// 		.color = {graphic::colors::ACID.copy().mul_a(.5f)}
				// 	});
				// gui::fringe::curve(r, {
				// 		.param = curve_trait_mat::bezier.apply_to({300, 300}, {400, 400}, {800, 400}, {900, 500}),
				// 	.margin = {1.01f, 0},
				// 		.stroke = {4, 6},
				// 		.segments = 1,
				// 		.color = {graphic::colors::ACID.copy().mul_a(.5f)}
				// 	});


				// r.push(poly_partial{
				// 		// .generic = ,
				// 		.pos = {400, 400},
				// 		.segments = 64,
				//
				// 		.radius = {120, 130},
				// 		.range = {0, .45f},
				// 		.color = {
				// 			graphic::colors::white, graphic::colors::white, graphic::colors::aqua, graphic::colors::aqua
				// 		}
				// 	});
			}


			constexpr float size = 80;
			const auto X_count = math::ceil<int>(ctx.get_extent().width / size);
			const auto Y_count = math::ceil<int>(ctx.get_extent().height / size);
			math::rand rand{54767963};
			for(int x = 0; x < X_count; ++x){ for(int y = 0; y < Y_count; ++y){
				// r.push(rect_aabb{
				// 	.generic = {.mode = std::to_underlying(((x + y) % 3 == 0) ? gui::fx::primitive_draw_mode::draw_slide_line : gui::fx::primitive_draw_mode::none)},
				// 	.v00 = {x * size, y * size},
				// 	.v11 = {x * size + size, y * size + size},
				// 	.vert_color = {graphic::color{rand(.5f, 1.f), rand(.5f, 1.f), rand(.5f, 1.f), rand(.5f, 1.f)}}
				// });
			}}

			if(false){
				{
					gui::state_guard g{r, gui::fx::blend::multiply};
					r.push(poly{
						.pos = current_focus.get_cursor_pos().add_x(-150),
						.segments = 16,
						.radius = {0, 64},
						.color = {graphic::colors::gray, graphic::colors::white}
					});
				}

				{
					gui::state_guard g{r, gui::fx::blend::pma::screen};
					r.push(poly{
						.pos = current_focus.get_cursor_pos().add_x(150),
						.segments = 16,
						.radius = {0, 64},
						.color = {graphic::colors::gray, graphic::colors::white}
					});
				}

				{
					gui::state_guard g{r, gui::fx::blend::pma::additive};
					r.push(poly{
						.pos = current_focus.get_cursor_pos().add_y(150),
						.segments = 16,
						.radius = {0, 64},
						.color = {graphic::colors::gray, graphic::colors::white}
					});
				}


				{
					gui::state_guard g{r, gui::fx::blend::pma::subtractive};
					r.push(poly{
						.pos = current_focus.get_cursor_pos().add_y(-150),
						.segments = 16,
						.radius = {0, 64},
						.color = {graphic::colors::gray, graphic::colors::white}
					});
				}

				r.push(poly{
						.pos = current_focus.get_cursor_pos(),
						.segments = 16,
						.radius = {0, 64},
						.color = {graphic::colors::gray, graphic::colors::white}
					});
			}

			r.update_state(
			{},
			gui::fx::batch_draw_mode::msdf,
				gui::make_state_tag(gui::fx::state_type::push_constant, VK_SHADER_STAGE_FRAGMENT_BIT));

			r << gui::fx::nine_patch_draw_vert_color{
				.patch = &gui::assets::builtin::default_round_square_boarder,
				.region = {200, 200, 600, 600},
				.color = {graphic::colors::white, graphic::colors::CYAN, graphic::colors::ROYAL, graphic::colors::GREEN}
			};


			r.update_state(gui::fx::blit_config{
				{
					.src = {},
					.extent = math::vector2{ctx.get_extent().width, ctx.get_extent().height}.as_signed()
				},
				{.pipeline_index = 1, .inout_define_index = 0}
			});


			{
				struct trail_node_data : graphic::trail::node_type{
					float idx_scale;
					graphic::color color;

					[[nodiscard]] float get_width() const noexcept{
						return idx_scale * scale;
					}
				};

				trail.iterate(1.f,
					[last = trail.head_pos_or({})](
						const graphic::trail::node_type& node, const unsigned idx, const unsigned total) mutable {
						using namespace graphic;
						math::rand rand{std::bit_cast<std::uintptr_t>(&node)};

						const float factor_global = math::idx_to_factor(idx, total);
						const auto fac = factor_global | math::interp::pow2Out;
						const auto off = rand.range(1.f) * fac * math::curve(factor_global, .05f, .2f);
						const auto tan = (node.pos - last).rotate_rt_counter_clockwise() * off;

						last = node.pos;
						auto n = node;
						const auto color = math::lerp(colors::black, colors::aqua.to_light(2.5f),
							factor_global);
						return trail_node_data{
								n, factor_global | math::interp::pow2In | math::interp::reverse, color
							};
					}, [&](std::span<const trail_node_data, 4> sspn){
						using namespace graphic;
						using namespace graphic::draw;

						const auto appr = sspn[1].pos - sspn[2].pos;
						const auto apprLen = appr.length();
						const auto seg = math::clamp(static_cast<unsigned>(apprLen / 16.f), 2U, 8U);

						r.push(parametric_curve{
								.param = curve_trait_mat::b_spline * (sspn | std::views::transform(&trail_node_data::pos)),
								.stroke = math::range{sspn[1].get_width(), sspn[2].get_width()} * 10.f,
								.segments = seg,
								.color = {colors::aqua.to_light(2.5f)},
							});
						r.push(parametric_curve{
								.param = curve_trait_mat::b_spline * (sspn | std::views::transform(&trail_node_data::pos)),
								.stroke = math::range{sspn[1].get_width(), sspn[2].get_width()} * 5.f,
								.segments = seg,
								.color = {colors::black},
							});
					});

				trail.iterate(1.f,
					[last = trail.head_pos_or({})](
						const graphic::trail::node_type& node, const unsigned idx, const unsigned total) mutable {
						using namespace graphic;
						math::rand rand{std::bit_cast<std::uintptr_t>(&node)};

						float factor_global = math::idx_to_factor(idx, math::max(total, 8U));
						const auto fac = factor_global | math::interp::pow2Out;
						const auto off = rand.range(1.f) * fac * math::curve(factor_global, .05f, .5f);
						const auto tan = (node.pos - last).rotate_rt_counter_clockwise() * off;

						last = node.pos;
						auto n = node;
						n.pos += tan;
						const auto color = math::lerp(colors::aqua.to_light(2.5f), colors::pale_green.to_light(1.5f),
							factor_global);

						factor_global = math::curve(factor_global | math::interp::reverse, math::idx_to_factor(5U, math::max(total, 8U)), 1.f);

						return trail_node_data{n, factor_global, color};
					}, [&](std::span<const trail_node_data, 4> sspn){
						using namespace graphic;
						using namespace graphic::draw;

						const auto appr = sspn[1].pos - sspn[2].pos;
						const auto apprLen = appr.length();
						const auto seg = math::clamp(static_cast<unsigned>(apprLen / 16.f), 4U, 12U);

						r.push(parametric_curve{
								.param = curve_trait_mat::catmull_rom<> * (sspn | std::views::transform(&trail_node_data::pos)),
								.stroke = math::range{sspn[1].get_width(), sspn[2].get_width()} * 8.f,
								.segments = seg,
								.color = {sspn[1].color, sspn[1].color, sspn[2].color, sspn[2].color},
							});
					});

			}

			r.push(triangle{});
			r.update_state(gui::fx::blit_config{
				{
					.src = {},
					.extent = math::vector2{ctx.get_extent().width, ctx.get_extent().height}.as_signed()
				},
				{.pipeline_index = 1}
			});

		}

		gui::global::manager.draw();
		renderer.batch_host.end_rendering();
		renderer.upload();
		renderer.create_command();

		std::array<VkCommandBuffer, 2> buffers{renderer.get_valid_cmd_buf(), cmdBUf};
		vk::cmd::submit_command(ctx.graphic_queue(), buffers, renderer.get_fence());
		ctx.flush();
	}
}

void prepare(){
	const auto shader_spv_path = std::filesystem::current_path().append("assets/shader/spv").make_preferred();
	constexpr VkApplicationInfo ApplicationInfo{
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "Hello Xrgui",
		.applicationVersion = VK_MAKE_API_VERSION(1, 0, 0, 0),
		.pEngineName = "No Engine",
		.engineVersion = VK_MAKE_API_VERSION(1, 0, 0, 0),
		.apiVersion = VK_API_VERSION_1_3,
	};

	using namespace mo_yanxi;
	using namespace graphic;

	gui::global::initialize();
	gui::global::initialize_assets_manager(gui::global::manager.get_arena_id());

	backend::vulkan::context ctx{ApplicationInfo};
	vk::register_default_requirements(ctx.get_device(), ctx.get_physical_device());

	vk::load_ext(ctx.get_instance());

	vk::sampler sampler_ui{ctx.get_device(), vk::preset::ui_texture_sampler};
	auto renderer = [&]() -> backend::vulkan::renderer{
		vk::shader_module shader_draw{ctx.get_device(), shader_spv_path / "ui.draw_v2.spv"};
		vk::shader_module shader_blit{ctx.get_device(), shader_spv_path / "ui.blit.basic.spv"};
		vk::shader_module shader_blend{ctx.get_device(), shader_spv_path / "ui.blend.spv"};

		using namespace backend::vulkan;
		return {
				renderer_create_info{
					.allocator_usage = ctx.get_allocator(),
					.command_pool = ctx.get_graphic_command_pool(),
					.sampler = sampler_ui,
					.attachment_draw_config = {
						{
							draw_attachment_config{
								.attachment = {VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT}
							},
							draw_attachment_config{
								.attachment = {VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT}
							},
						},
						// VK_SAMPLE_COUNT_4_BIT
					},
					.attachment_blit_config = {
						{
							attachment_config{VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL},
							attachment_config{VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL},
							attachment_config{VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL},
						}
					},
					.draw_pipe_config = graphic_pipeline_create_config{
						{
							graphic_pipeline_create_config::config{
								{{VkPushConstantRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4}}},
								{
									shader_draw.get_create_info(VK_SHADER_STAGE_MESH_BIT_EXT, "main_mesh"),
									shader_draw.get_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, "main_frag")
								},
								graphic_pipeline_option{
									false, {0b1},
									{
										{vk::blending::scaled_alpha_blend}, false, true, false
									}}
							},
							graphic_pipeline_create_config::config{
								{{VkPushConstantRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4}}},
								{
									shader_draw.get_create_info(VK_SHADER_STAGE_MESH_BIT_EXT, "main_mesh"),
									shader_draw.get_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, "main_frag")
								},
								graphic_pipeline_option{
									true, {0b1}, {
											{vk::blending::scaled_alpha_blend}
										}}
							},
						},
						{}
					},
					.blit_pipe_config = compute_pipeline_create_config{
						{
							compute_pipeline_create_config::config{
								.general = {make_push_constants(VK_SHADER_STAGE_COMPUTE_BIT, {8})},
								.shader_module = shader_blit.get_create_info_compute(),
								.option = {
									.inout = compute_pipeline_blit_inout_config{
										{
											{0, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
											{1, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
										},
										{
											{2, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
											{3, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
										}
									},
								}
							},
							compute_pipeline_create_config::config{
								.general = {make_push_constants(VK_SHADER_STAGE_COMPUTE_BIT, {8})},
								.shader_module = shader_blend.get_create_info_compute(),
								.option = {
									.inout = compute_pipeline_blit_inout_config{
										{
											{0, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
										},
										{
											{1, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
										}
									},
								}
							},
						},
						{
							compute_pipeline_blit_inout_config{
									{
										{0, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
									},
									{
										{1, 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
									}
							}
						}
					}
				}
			};
	}();


	{
		//
		// vk::shader_module bindless_shader_mesh{ctx.get_device(), shader_spv_path / "ui.draw_bindless.glsl.spv"};
		// vk::shader_module bindless_shader_frag{ctx.get_device(), shader_spv_path / "ui.frag.spv"};

		// vk::graphic_pipeline_template template_{};
		// template_.set_shaders({bindless_shader_mesh.get_create_info(VK_SHADER_STAGE_MESH_BIT_EXT), bindless_shader_frag.get_create_info(VK_SHADER_STAGE_FRAGMENT_BIT)});
		// template_.push_color_attachment_format(VK_FORMAT_R8G8B8A8_UNORM);
		// template_.push_color_attachment_blend_state(vk::blending::scaled_alpha_blend);
		// vk::pipeline pipeline{ctx.get_device(), 0, VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT, template_};
		//
		// renderer._temp_pipeline = std::move(pipeline);
	}


#pragma region LoadResource
	image_atlas image_atlas{
			ctx,
			ctx.graphic_family(),
			ctx.get_device().graphic_queue(1),
			renderer.resource_descriptor_heap,
			renderer.get_heap_dynamic_image_section()
		};
	font::font_manager font_manager{};
	font_manager.set_page(image_atlas.create_image_page("font"));

	{
		auto sys_font_path = font::get_system_fonts();


		const std::filesystem::path font_path = std::filesystem::current_path().append("assets/font").make_preferred();
		auto& SourceHanSansCN_regular = font_manager.register_meta("srchs", font_path / "SourceHanSansCN-Regular.otf");
		auto& telegrama = font_manager.register_meta("tele", font_path / "telegrama.otf");
		auto& seguisym = font_manager.register_meta("segui", font_path / "seguisym.ttf");

		auto& default_family = font_manager.register_family("def", {&telegrama, &SourceHanSansCN_regular, &seguisym});

		auto& default_family2 = font_manager.register_family("gui", {&SourceHanSansCN_regular, &seguisym});

		font_manager.set_default_family(&default_family2);

		font::default_font_manager = &font_manager;
	}

	{
		gui::assets::generate_default_shapes(image_atlas);
	}
#pragma endregion

#pragma region InitUI

	auto& ui_root = gui::global::manager;
	const auto scene_add_rst = ui_root.add_scene<gui::loose_group>("main", true, renderer.create_frontend());
	scene_add_rst.scene.resize(math::rect_ortho{tags::from_extent, {}, ctx.get_extent().width, ctx.get_extent().height}.as<float>());
	auto ui_providers = gui::example::build_main_ui(ctx, scene_add_rst.scene, scene_add_rst.root_group);

#pragma endregion

#pragma region SetupRenderGraph
	compositor::manager manager{ctx.get_allocator()};
	vk::shader_module shader_filter_high_light = {ctx.get_device(), shader_spv_path / "post_process.highlight_extract.spv"};
	vk::shader_module shader_merge = {ctx.get_device(), shader_spv_path / "ui.merge.spv"};
	vk::shader_module shader_hdr_to_sdr = {ctx.get_device(), shader_spv_path / "post_process.hdr_to_sdr.spv"};

	vk::sampler sampler_blit{ctx.get_device(), vk::preset::default_blit_sampler};
	vk::shader_module shader_bloom{ctx.get_device(), shader_spv_path / "post_process.bloom.spv"};

	auto& ui_input_base = manager.add_external_resource(compositor::resource_entity_external{
			compositor::image_entity{}, compositor::resource_dependency{
				.src_access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				.dst_access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
			}
		});

	auto& ui_input_back = manager.add_external_resource(compositor::resource_entity_external{
			compositor::image_entity{}, compositor::resource_dependency{
				.src_access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				.dst_access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
			}
		});

	auto& input_background = manager.add_external_resource(compositor::resource_entity_external{
			compositor::image_entity{}, compositor::resource_dependency{
				.src_access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				.dst_access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
			}
		});

	auto pass_filter_high_light = manager.add_pass<compositor::post_process_pass_with_ubo<high_light_filter_args>>(compositor::post_process_meta{
			shader_filter_high_light, {
				{{0}, compositor::no_slot, 0},
				{{1}, 0, compositor::no_slot},
			}
		});

	pass_filter_high_light.id()->add_input({{ui_input_base, 0}});


	auto pass_bloom = manager.add_pass<compositor::bloom_pass>(compositor::get_bloom_default_meta(shader_bloom, {}));
	pass_bloom.data.set_sampler_at_binding(0, sampler_blit);
	pass_bloom.pass.add_dep({pass_filter_high_light.id(), 0, 0});
	pass_bloom.pass.add_local({1, compositor::no_slot});


	static constexpr VkSpecializationMapEntry SpecEntry{0, 0, 4};
	static constexpr VkBool32 SpecData{true};
	auto pass_blur = manager.add_pass<compositor::bloom_pass>(compositor::get_bloom_default_meta(shader_bloom, {
			.target_scale = 1,
			.specializationInfo = VkSpecializationInfo{
				.mapEntryCount = 1,
				.pMapEntries = &SpecEntry,
				.dataSize = sizeof(SpecData),
				.pData = &SpecData
			},
			.format = VK_FORMAT_R8G8B8A8_UNORM

		}));
	pass_blur.data.set_max_mip_level(5);
	pass_blur.data.set_sampler_at_binding(0, sampler_blit);
	pass_blur.id()->add_input({{input_background, 0}});
	pass_blur.id()->add_local({1, compositor::no_slot});


	auto pass_merge = manager.add_pass<compositor::post_process_stage>(compositor::post_process_meta{
			shader_merge, {
				{{0}, compositor::no_slot, 0},
				{{1}, 0, compositor::no_slot},
				{{2}, 1, compositor::no_slot},
				{{3}, 2, compositor::no_slot},
				{{4}, 3, compositor::no_slot},
				{{5}, 4, compositor::no_slot},
			}
		});
	pass_merge.data.set_sampler_at_binding(5, sampler_blit);

	pass_merge.id()->add_input({{ui_input_base, 0}});
	pass_merge.id()->add_input({{ui_input_back, 1}});
	pass_merge.id()->add_dep({pass_bloom.id(), 0, 2});
	pass_merge.id()->add_input({{input_background, 3}});
	pass_merge.id()->add_dep({pass_blur.id(), 0, 4});


	compositor::post_process_meta meta{
			shader_hdr_to_sdr, {
				{{0}, compositor::no_slot, 0},
				{{1}, 0, compositor::no_slot},
			}
		};
	meta.sockets.at_out(0).get<compositor::image_requirement>().usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	auto pass_h2s = manager.add_pass<compositor::post_process_pass_with_ubo<tonemap_args>>(meta);
	pass_h2s.id()->add_dep({pass_merge.id(), 0, 0});
	pass_h2s.id()->add_local({compositor::no_slot, 0});

	manager.sort();

	renderer.resize({64, 64});
	ui_input_base.resource = compositor::image_entity{.handle = renderer.get_blit_attachments()[0]};
	ui_input_back.resource = compositor::image_entity{.handle = renderer.get_blit_attachments()[1]};
	input_background.resource = compositor::image_entity{.handle = renderer.get_blit_attachments()[2]};
	manager.resize({64, 64}, true);

	pass_blur.data.set_scale(1.25f);
	pass_blur.data.set_mix_factor(.025f);
	pass_blur.data.set_strength(1.f, 1.f);

#pragma endregion

#pragma region GuiBinding
	{
		auto& m = gui::global::manager.get_current_focus();
		auto& bloom_scale = m.request_independent_react_node(react_flow::make_listener([&p = pass_bloom.data](float val){
			p.set_scale(val);
		}));
		auto& bloom_src_recv = m.request_independent_react_node(react_flow::make_listener([&p = pass_bloom.data](float val){
			p.set_strength_src(val);
		}));
		auto& bloom_dst_recv = m.request_independent_react_node(react_flow::make_listener([&p = pass_bloom.data](float val){
			p.set_strength_dst(val);
		}));
		auto& bloom_mix_recv = m.request_independent_react_node(react_flow::make_listener([&p = pass_bloom.data](float val){
			p.set_mix_factor(val);
		}));

		auto& highlight_thres_recv = m.request_independent_react_node(react_flow::make_listener([&p = pass_filter_high_light.data](float val){
			p.set_ubo_value(&high_light_filter_args::threshold, val);
		}));
		auto& highlight_smooth_recv = m.request_independent_react_node(react_flow::make_listener([&p = pass_filter_high_light.data](float val){
			p.set_ubo_value(&high_light_filter_args::smoothness, val);
		}));

		auto& tonemap_contrast = m.request_independent_react_node(react_flow::make_listener([&p = pass_h2s.data](float val){
			p.set_ubo_value(&tonemap_args::contrast, val);
		}));
		auto& tonemap_exposure = m.request_independent_react_node(react_flow::make_listener([&p = pass_h2s.data](float val){
			p.set_ubo_value(&tonemap_args::exposure, val);
		}));
		auto& tonemap_saturation = m.request_independent_react_node(react_flow::make_listener([&p = pass_h2s.data](float val){
			p.set_ubo_value(&tonemap_args::saturation, val);
		}));
		auto& tonemap_gamma = m.request_independent_react_node(react_flow::make_listener([&p = pass_h2s.data](float val){
			p.set_ubo_value(&tonemap_args::gamma, val);
		}));

		bloom_scale.connect_predecessor(*ui_providers.shader_bloom_scale);
		bloom_src_recv.connect_predecessor(*ui_providers.shader_bloom_src_factor);
		bloom_dst_recv.connect_predecessor(*ui_providers.shader_bloom_dst_factor);
		bloom_mix_recv.connect_predecessor(*ui_providers.shader_bloom_mix_factor);

		highlight_thres_recv.connect_predecessor(*ui_providers.highlight_filter_threshold);
		highlight_smooth_recv.connect_predecessor(*ui_providers.highlight_filter_smooth);

		tonemap_contrast.connect_predecessor(*ui_providers.tonemap_contrast);
		tonemap_exposure.connect_predecessor(*ui_providers.tonemap_exposure);
		tonemap_saturation.connect_predecessor(*ui_providers.tonemap_saturation);
		tonemap_gamma.connect_predecessor(*ui_providers.tonemap_gamma);

		ui_providers.apply();
	}

#pragma endregion


	auto post_process_cmd = ctx.get_compute_command_pool().obtain();
	ctx.register_post_resize("test", [&](backend::vulkan::context& context, window_instance::resize_event event){
		renderer.resize({event.size.width, event.size.height});
		gui::global::manager.resize(math::rect_ortho{tags::from_extent, {}, event.size.width, event.size.height}.as<float>());

		ui_input_base.resource = compositor::image_entity{.handle = renderer.get_blit_attachments()[0]};
		ui_input_back.resource = compositor::image_entity{.handle = renderer.get_blit_attachments()[1]};
		input_background.resource = compositor::image_entity{.handle = renderer.get_blit_attachments()[2]};

		manager.resize(event.size, true);

		{
			vk::scoped_recorder r{post_process_cmd, VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT};
			manager.create_command(post_process_cmd);
		}

		context.set_staging_image(
			{
				.image = pass_h2s.pass.get_used_resources().get_out(0).as_image().handle.image,
				// .image = renderer.get_base().image,
				.extent = event.size,
				.clear = false,
				.owner_queue_family = context.graphic_family(),
				.src_stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.src_access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				.dst_stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.dst_access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				.src_layout = VK_IMAGE_LAYOUT_GENERAL,
				.dst_layout = VK_IMAGE_LAYOUT_GENERAL
			}, false);
	});

	ctx.record_post_command(true);

	app_run(ctx, renderer, manager, post_process_cmd);

	gui::assets::dispose_generated_shapes();
	gui::global::terminate_assets_manager();
	gui::global::terminate();

	image_atlas.wait_load();
	ctx.wait_on_device();
}


int main(){
	using namespace mo_yanxi;
	using namespace graphic;

	//shader_runtime_compiler shader_runtime_compiler{};
	//shader_wrapper wrapper{shader_runtime_compiler, (std::filesystem::current_path() / "assets/shader/spv").make_preferred()};
	//wrapper.compile(R"(D:\projects\xrgui\properties\assets\shader\glsl\ui.frag)");
	//wrapper.compile(R"(D:\projects\xrgui\properties\assets\shader\glsl\ui.draw_bindless.glsl)");

#ifndef NDEBUG
	if(auto ptr = std::getenv("NSIGHT"); ptr != nullptr && std::strcmp(ptr, "1") == 0){
		vk::enable_validation_layers = false;
	}else{
		vk::enable_validation_layers = true;
	}
#endif

	font::initialize();
	backend::glfw::initialize();
	typesetting::rich_text_look_up_table table;
	typesetting::look_up_table = &table;

	prepare();

	backend::glfw::terminate();
	font::terminate();
}