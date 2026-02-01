#include <vulkan/vulkan.h>

import mo_yanxi.vk;
import mo_yanxi.vk.cmd;

import mo_yanxi.backend.vulkan.context;
import mo_yanxi.backend.glfw.window;
import mo_yanxi.backend.application_timer;

import mo_yanxi.gui.infrastructure;
import mo_yanxi.gui.elem.group;
import mo_yanxi.gui.global;
import mo_yanxi.gui.assets.manager;
import mo_yanxi.gui.renderer.frontend;

import mo_yanxi.graphic.image_atlas;

import mo_yanxi.font;
import mo_yanxi.font.manager;
import mo_yanxi.font.typesetting;

import mo_yanxi.graphic.compositor.manager;
import mo_yanxi.graphic.compositor.post_process_pass;
import mo_yanxi.graphic.compositor.bloom;


import mo_yanxi.gui.examples;
import mo_yanxi.backend.vulkan.renderer;
import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.gui.gfx_config;

import mo_yanxi.gui.fringe;

import mo_yanxi.math.rand;

import std;

import mo_yanxi.font.typesetting;
import mo_yanxi.hb.typesetting;
import mo_yanxi.typesetting.rich_text;


void app_run(
	mo_yanxi::backend::vulkan::context& ctx,
	mo_yanxi::backend::vulkan::renderer& renderer,
	mo_yanxi::graphic::compositor::manager& manager,
	mo_yanxi::vk::command_buffer& cmdBUf
	){



	using namespace mo_yanxi;

	backend::application_timer timer{backend::application_timer<double>::get_default()};


	const char* test_text =
R"(Basic{size:128} Token Test{/}
{u}AVasdfdjknfhvbawhboozx{/}cgiuTeWaVoT.P.àáâãäåx̂̃ñ
{color:#FF0000}Red Text{/} and {font:gui}Font Change{/}
{color:#FF0000}楼上的{/} 下来搞核算

Escapes Test:
1. Backslash: \\ (Should see single backslash)
2. Braces {size:128}with{/} slash: \{ and \} (Should see literal { and })
3. {off:16 16}Braces with double{/}: {{ and }} (Should see literal { and })

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

	type_setting::tokenized_text text{test_text};


	auto rst = font::hb::layout_text(*font::typesetting::default_font_manager, *font::typesetting::default_font_manager->get_default_family(),
		// "AVasdfdjk\nfhvbawhboozxcgiuTeWaVoT.P.àáâãäåx̂̃ñ\n\r楼上的下来搞核算\n\r咚鸡叮咚鸡\t大狗大狗叫叫叫\n带兴奋兴奋剂\n一段一段带一段",
		text,
		{
			// .direction = font::hb::layout_direction::ttb,
			.max_extent = {1200, 300},
			.font_size = {32, 32},
			.line_feed_type = font::hb::linefeed::CRLF,
			.line_spacing_scale = 1.6f
			// .align = font::hb::content_alignment::end
		});

	while(!ctx.window().should_close()){

		ctx.window().poll_events();
		timer.fetch_time();

		gui::global::manager.update(timer.global_delta_tick());
		gui::global::manager.layout();

		renderer.batch_host.begin_rendering();
		renderer.batch_host.get_data_group_non_vertex_info().push_default(gui::gfx_config::ui_state(
			timer.global_time()
		));
		renderer.batch_host.get_data_group_non_vertex_info().push_default(gui::gfx_config::slide_line_config{});

		auto& r = gui::global::manager.get_current_focus().renderer();
		r.init_projection();
		// gui::global::manager.draw();

		{
			using namespace graphic::draw::instruction;

			math::vec2 offset{50, 50};

			r.update_state(gui::draw_config{
					.mode = gui::draw_mode::msdf,
					.draw_targets = {0b1},
					.pipeline_index = 0
				});
			for (const auto & layout_result : rst.elems){
				if(!layout_result.texture->view)continue;
				r.push(rect_aabb{
					.generic = {layout_result.texture->view},
					.v00 = layout_result.aabb.get_src().add(offset),
					.v11 = layout_result.aabb.get_end().add(offset),
					.uv00 = layout_result.texture->uv.v00(),
					.uv11 = layout_result.texture->uv.v11(),
					.vert_color = {layout_result.color}
				});
			}
			for (const auto & layout_result : rst.underlines){
				r.push(line{
					.src = offset + layout_result.start,
					.dst = offset + layout_result.end,
					.color = {layout_result.color, layout_result.color},
					.stroke = layout_result.thickness,
				});
			}

			r.push(rect_aabb_outline{
				.v00 = math::vec2{}.add(offset),
				.v11 = rst.extent.copy().add(offset),
				.stroke = {2},
				.vert_color = {graphic::colors::white}
			});

			// r.push(rect_aabb_outline{
			// 	.v00 = math::vec2{}.add(offset),
			// 	.v11 = math::vec2{300, 300}.add(offset),
			// 	.stroke = {2},
			// 	.vert_color = {graphic::colors::GREEN}
			// });

			r.update_state(state_push_config{
				state_push_target::defer_pre
			}, gui::gfx_config::blit_config{
				{
					.src = {},
					.extent = math::vector2{ctx.get_extent().width, ctx.get_extent().height}.as_signed()
				},
				{.pipeline_index = 1}});
		}

		if(false){
			using namespace graphic::draw::instruction;

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



			float size = 80;
			auto X_count = math::ceil<int>(ctx.get_extent().width / size);
			auto Y_count = math::ceil<int>(ctx.get_extent().height / size);

			math::rand rand{54767963};
			for(int x = 0; x < X_count; ++x){
				for(int y = 0; y < Y_count; ++y){
					r.push(rect_aabb{
						// .generic = {.mode = std::to_underlying(gui::primitive_draw_mode::draw_slide_line)},
						.v00 = {x * size, y * size},
						.v11 = {x * size + size, y * size + size},
						.vert_color = {graphic::color{rand(.5f, 1.f), rand(.5f, 1.f), rand(.5f, 1.f), rand(.5f, 1.f)}}
					});
				}
			}

			// r.push(line{
			// 	.src = {100, 100},
			// 	.dst = {800, 400},
			// 	.color = {graphic::colors::white, graphic::colors::aqua},
			// 	.stroke = 2,
			// });
			//
			//
			// r.update_state(gui::gfx_config::blit_config{
			// 	{
			// 		.src = {},
			// 		.extent = math::vector2{ctx.get_extent().width, ctx.get_extent().height}.as_signed()
			// 	},
			// 	{.pipeline_index = 1}});
			//
			// r.update_state(gui::draw_config{
			// 	.pipeline_index = 1
			// });
			//
			r.update_state(state_push_config{
				state_push_target::defer_pre
			}, gui::gfx_config::blit_config{
				{
					.src = {},
					.extent = math::vector2{ctx.get_extent().width, ctx.get_extent().height}.as_signed()
				},
				{.pipeline_index = 1}});
			//
			// r.push(line{
			// 	.src = {200, 200},
			// 	.dst = {900, 500},
			// 	.color = {graphic::colors::white, graphic::colors::aqua},
			// 	.stroke = 2,
			// });

		}

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
	vk::load_ext(ctx.get_instance());

#pragma region LoadResource
	image_atlas image_atlas{
			ctx,
			ctx.graphic_family(),
			ctx.get_device().graphic_queue(1)
		};
	font::font_manager font_manager{};
	font_manager.set_page(image_atlas.create_image_page("font"));

	{
		const std::filesystem::path font_path = std::filesystem::current_path().append("assets/font").make_preferred();
		auto& SourceHanSansCN_regular = font_manager.register_meta("srchs", font_path / "SourceHanSansCN-Regular.otf");
		auto& telegrama = font_manager.register_meta("tele", font_path / "telegrama.otf");
		auto& seguisym = font_manager.register_meta("segui", font_path / "seguisym.ttf");

		auto& default_family = font_manager.register_family("def", {&telegrama, &SourceHanSansCN_regular, &seguisym});

		auto& default_family2 = font_manager.register_family("gui", {&SourceHanSansCN_regular, &seguisym});

		font_manager.set_default_family(&default_family2);

		font::typesetting::default_font_manager = &font_manager;
	}
#pragma endregion

#pragma region InitUI
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
								.attachment = {VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT}
							},
							draw_attachment_config{
								.attachment = {VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT}
							},
						},
						// VK_SAMPLE_COUNT_4_BIT
					},
					.attachment_blit_config = {
						{
							attachment_config{VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL},
							attachment_config{VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL},
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
								graphic_pipeline_option{false, {0b1}}
							},
							graphic_pipeline_create_config::config{
								{{VkPushConstantRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4}}},
								{
									shader_draw.get_create_info(VK_SHADER_STAGE_MESH_BIT_EXT, "main_mesh"),
									shader_draw.get_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, "main_frag")
								},
								graphic_pipeline_option{true, {0b1}}
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

							}
						}
					}
				}
			};
	}();

	auto& ui_root = gui::global::manager;
	const auto scene_add_rst = ui_root.add_scene<gui::loose_group>("main", true, renderer.create_frontend());
	scene_add_rst.scene.resize(math::rect_ortho{tags::from_extent, {}, ctx.get_extent().width, ctx.get_extent().height}.as<float>());
	gui::example::build_main_ui(ctx, scene_add_rst.scene, scene_add_rst.root_group);
#pragma endregion

#pragma region SetupRenderGraph
	compositor::manager manager{ctx.get_allocator()};
	vk::shader_module shader_filter_high_light = {ctx.get_device(), shader_spv_path / "post_process.highlight_extract.spv"};
	vk::shader_module shader_hdr_to_sdr = {ctx.get_device(), shader_spv_path / "post_process.hdr_to_sdr.spv"};

	vk::sampler sampler_blit{ctx.get_device(), vk::preset::default_blit_sampler};
	vk::shader_module shader_bloom{ctx.get_device(), shader_spv_path / "post_process.bloom.spv"};

	auto& ui_input = manager.add_external_resource(compositor::resource_entity_external{
			compositor::image_entity{.handle = renderer.get_base()}, compositor::resource_dependency{
				.src_access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				.dst_access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
			}
		});

	auto pass_filter_high_light = manager.add_pass<compositor::post_process_stage>(compositor::post_process_meta{
			shader_filter_high_light, {
				{{0}, compositor::no_slot, 0},
				{{1}, 0, compositor::no_slot},
			}
		});

	pass_filter_high_light.id()->add_input({{ui_input, 0}});


	// auto pass_bloom = manager.add_pass<compositor::bloom_pass>(compositor::get_bloom_default_meta(shader_bloom));
	// pass_bloom.meta.set_sampler_at_binding(0, sampler_blit);
	// pass_bloom.pass.add_dep({pass_filter_high_light.id(), 0, 0});
	// pass_bloom.pass.add_local({1, compositor::no_slot});


	compositor::post_process_meta meta{
			shader_hdr_to_sdr, {
				{{0}, compositor::no_slot, 0},
				{{1}, 0, compositor::no_slot},
			}
		};
	meta.sockets.at_out(0).get<compositor::image_requirement>().usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

	auto pass_h2s = manager.add_pass<compositor::post_process_stage>(meta);
	pass_h2s.id()->add_dep({pass_filter_high_light.id(), 0, 0});
	pass_h2s.id()->add_local({compositor::no_slot, 0});

	manager.sort();
#pragma endregion

	auto post_process_cmd = ctx.get_compute_command_pool().obtain();
	ctx.register_post_resize("test", [&](backend::vulkan::context& context, window_instance::resize_event event){
		renderer.resize({event.size.width, event.size.height});
		gui::global::manager.resize(math::rect_ortho{tags::from_extent, {}, event.size.width, event.size.height}.as<float>());

		ui_input.resource = compositor::image_entity{.handle = renderer.get_base()};
		manager.resize(event.size, true);
		//
		// pass_bloom.meta.set_scale(.5f);
		// pass_bloom.meta.set_mix_factor(0.f);
		// pass_bloom.meta.set_strength(.8f, .8f);

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

	gui::global::terminate_assets_manager();
	gui::global::terminate();

	image_atlas.wait_load();
	ctx.wait_on_device();
}

std::string to_utf8(std::u32string_view s) {
	std::string res;
	res.reserve(s.size() * 4); // 预留空间，避免频繁重分配

	for (char32_t c : s) {
		if (c <= 0x7F) {
			res += static_cast<char>(c);
		} else if (c <= 0x7FF) {
			res += static_cast<char>(0xC0 | ((c >> 6) & 0x1F));
			res += static_cast<char>(0x80 | (c & 0x3F));
		} else if (c <= 0xFFFF) {
			res += static_cast<char>(0xE0 | ((c >> 12) & 0x0F));
			res += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
			res += static_cast<char>(0x80 | (c & 0x3F));
		} else if (c <= 0x10FFFF) {
			res += static_cast<char>(0xF0 | ((c >> 18) & 0x07));
			res += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
			res += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
			res += static_cast<char>(0x80 | (c & 0x3F));
		}
	}
	return res;
}

int main(){
	using namespace mo_yanxi;
	using namespace graphic;

const char* test_text =
R"({size:24}Basic Token Test{/}
{color:#FF0000}Red Text{/} and {font:Arial}Font Change{/}

Escapes Test:
1. Backslash: \\ (Should see single backslash)
2. Braces with slash: \{ and \} (Should see literal { and })
3. Braces with double: {{ and }} (Should see literal { and })

Line Continuation Test:
This is a very long line that \
should be joined together \
without newlines.

Edge Cases:
1. Token without arg: {bold}Bold Text{/bold}
2. Unclosed brace: { This is just text because no closing bracket
3. Unknown escape: \z (Should show 'z')
4. Colon in arg: {log:Time:12:00} (Name="log", Arg="Time:12:00")
)";

	type_setting::tokenized_text text{test_text};

	// std::println("{}", to_utf8(text.get_text()));

	// type_setting::tokenized_text text2{test_text};

#ifndef NDEBUG
	if(auto ptr = std::getenv("NSIGHT"); ptr != nullptr && std::strcmp(ptr, "1") == 0){
		vk::enable_validation_layers = false;
	}else{
		vk::enable_validation_layers = true;
	}
#endif

	font::initialize();
	backend::glfw::initialize();

	prepare();

	backend::glfw::terminate();
	font::terminate();
}