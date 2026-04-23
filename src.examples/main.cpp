#include <cstdio>
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
import mo_yanxi.gui.fx.config;
import mo_yanxi.gui.fx.fringe;
import mo_yanxi.gui.fx.instruction_extension;
import mo_yanxi.gui.elem.label;
import mo_yanxi.gui.elem.text_edit;

import mo_yanxi.gui.assets;
import mo_yanxi.gui.image_regions;

import mo_yanxi.font;
import mo_yanxi.font.plat;
import mo_yanxi.font.manager;
import mo_yanxi.typesetting;
import mo_yanxi.typesetting.segmented_layout;
import mo_yanxi.typesetting.util;
import mo_yanxi.typesetting.rich_text;

import mo_yanxi.react_flow.common;
import mo_yanxi.react_flow;

import mo_yanxi.gui.markdown;
import mo_yanxi.core.platform;

import mo_yanxi.gui.examples;
import mo_yanxi.gui.examples.main_loop;


struct alignas(16) high_light_filter_args{
	float threshold{1.3f};
	float smoothness{.5f};
	float max_brightness{10};
};

struct alignas(16) tonemap_args{
	float exposure{1};
	float contrast{1};
	float gamma{1};
	float saturation{1};
};

void app_run(
	mo_yanxi::gui::example::main_loop& main_loop,
	mo_yanxi::backend::vulkan::context& ctx,
	mo_yanxi::backend::vulkan::renderer& renderer,
	mo_yanxi::vk::command_buffer& cmd_buf
){
	using namespace mo_yanxi;

	backend::application_timer timer{backend::application_timer<double>::get_default()};

	auto& current_focus = main_loop.get_scene();;
	std::println("[App] Entering Main Loop");
	main_loop.wait_term_and_reset();
	while(!ctx.window().should_close()){
		ctx.window().poll_events();
		timer.fetch_time();
		//
		gui::global::event_queue.push_frame_split(timer.global_delta());
		main_loop.permit_burst();
		current_focus.get_output_communicate_async_task_queue(0).consume();

		//Clear unused events currently
		(void)main_loop.unhandled_events.fetch();

		main_loop.wait_term();
		std::array<VkCommandBuffer, 2> buffers{renderer.get_valid_cmd_buf(), cmd_buf};
		vk::cmd::submit_command(ctx.graphic_queue(), buffers, renderer.get_fence());
		ctx.flush();
		main_loop.reset_term();
	}
}

void prepare(mo_yanxi::backend::vulkan::context& ctx){
	using namespace mo_yanxi;
	using namespace graphic;

	const auto shader_spv_path = std::filesystem::current_path().append("assets/shader/spv").make_preferred();



	std::println("[GUI] Core Initialize ");
	gui::global::initialize();
	gui::global::initialize_assets_manager(gui::global::manager.get_arena_id());
	std::println("[GUI] Core Initialize Done");

#pragma region InitRenderer
	std::println("[GUI] Renderer Initialize");
	vk::sampler sampler_ui{ctx.get_device(), vk::preset::ui_texture_sampler};
	//renderer should belong to main loop actually
	auto renderer = [&]() -> backend::vulkan::renderer{
		vk::shader_module draw_shader_vert{ctx.get_device(), shader_spv_path / "ui.draw.vert.spv"};
		vk::shader_module draw_shader_frag_basic{ctx.get_device(), shader_spv_path / "ui.draw.frag_basic.spv"};
		vk::shader_module draw_shader_frag_outlined{ctx.get_device(), shader_spv_path / "ui.draw.frag_outlined.spv"};
		vk::shader_module draw_shader_coord{ctx.get_device(), shader_spv_path / "ui.draw.coord_draw.spv"};
		vk::shader_module draw_shader_mask{ctx.get_device(), shader_spv_path / "ui.draw.frag_mask.spv"};
		vk::shader_module draw_shader_mask_apply{ctx.get_device(), shader_spv_path / "ui.draw.frag_mask_apply.spv"};

		vk::shader_module blit_shader_merge{ctx.get_device(), shader_spv_path / "ui.blit.basic.spv"};
		vk::shader_module blit_shader_blend{ctx.get_device(), shader_spv_path / "ui.blit.alpha_blend.spv"};
		vk::shader_module blit_shader_inverse{ctx.get_device(), shader_spv_path / "ui.blit.inverse.spv"};

		vk::shader_module shader_instr_resolve{ctx.get_device(), shader_spv_path / "ui.instruction_resolve_comp.spv"};

		using namespace backend::vulkan;
		return {
				renderer_create_info{
					.allocator_usage = ctx.get_allocator(),
					.command_pool = ctx.get_graphic_command_pool(),
					.sampler = sampler_ui,
					.attachment_draw_config = {
						{
							draw_attachment_config{
								.attachment = {VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT}
							},
							draw_attachment_config{
								.attachment = {VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT}
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
							//basic draw
							graphic_pipeline_create_config::config{
								{
									draw_shader_vert.get_stage_bundle(VK_SHADER_STAGE_VERTEX_BIT, "main_vert"),
									draw_shader_frag_basic.get_stage_bundle(VK_SHADER_STAGE_FRAGMENT_BIT, "main_frag")
								},
								graphic_pipeline_option{
									false, mask_usage::ignore, {0b1}, {},
									{
										{vk::blending::premultiplied_alpha_blend}, blend_dynamic_flags::equation | blend_dynamic_flags::write_flag
									}
								}
							},
							//outline sdf
							graphic_pipeline_create_config::config{
								{
									draw_shader_vert.get_stage_bundle(VK_SHADER_STAGE_VERTEX_BIT, "main_vert"),
									draw_shader_frag_outlined.get_stage_bundle(VK_SHADER_STAGE_FRAGMENT_BIT, "main_frag")
								},
								graphic_pipeline_option{
									false, mask_usage::ignore, {0b1}, {},
									{
										{vk::blending::premultiplied_alpha_blend}
									}
								}
							},
							//coordinate draw
							graphic_pipeline_create_config::config{
								{
									draw_shader_coord.get_stage_bundle(VK_SHADER_STAGE_VERTEX_BIT, "main_vert"),
									draw_shader_coord.get_stage_bundle(VK_SHADER_STAGE_FRAGMENT_BIT, "main_frag")
								},
								graphic_pipeline_option{
									false, mask_usage::ignore, {0b1}, {},
									{
										{vk::blending::premultiplied_alpha_blend}
									}
								}
							},
							//mask draw
							graphic_pipeline_create_config::config{
								{
									draw_shader_vert.get_stage_bundle(VK_SHADER_STAGE_VERTEX_BIT, "main_vert"),
									draw_shader_mask.get_stage_bundle(VK_SHADER_STAGE_FRAGMENT_BIT, "main_frag")
								},
								graphic_pipeline_option{
									false, mask_usage::write, {}, {},
									{
										{vk::blending::mask_draw}, blend_dynamic_flags::equation
									}
								}
							},
							//pipeline apply
							graphic_pipeline_create_config::config{
								{
									draw_shader_vert.get_stage_bundle(VK_SHADER_STAGE_VERTEX_BIT, "main_vert"),
									draw_shader_mask_apply.get_stage_bundle(VK_SHADER_STAGE_FRAGMENT_BIT, "main_frag")
								},
								graphic_pipeline_option{
									false, mask_usage::read, {0b1}, {},
									{
										{vk::blending::max_alpha_blend}
									}
								}
							},
						},
						{}
					},
					.blit_pipe_config = compute_pipeline_create_config{
						{
							compute_pipeline_create_config::config{
								.shader_bundle = blit_shader_merge.get_stage_bundle(VK_SHADER_STAGE_COMPUTE_BIT),
								.option = {
									.inout = compute_pipeline_blit_inout_config{
										{
											{0, 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
											{1, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
										},
										{
											{2, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
											{3, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
										}
									},
								}
							},
							compute_pipeline_create_config::config{
								.shader_bundle = blit_shader_blend.get_stage_bundle(VK_SHADER_STAGE_COMPUTE_BIT),
								.option = {
									.inout = compute_pipeline_blit_inout_config{
										{
											{0, 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
										},
										{
											{1, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
										}
									},
								}
							},
							compute_pipeline_create_config::config{
								.shader_bundle = blit_shader_inverse.get_stage_bundle(VK_SHADER_STAGE_COMPUTE_BIT),
								.option = {
									.inout = compute_pipeline_blit_inout_config{
										{
											{0, 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
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
									{0, 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
								},
								{
									{1, 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
								}
							}
						}
					},
					.resolver_shader_stage = shader_instr_resolve.get_create_info(VK_SHADER_STAGE_COMPUTE_BIT),
					.stride_config = {
						.vertex_stride = sizeof(gui_vertex_mock),
						.primitive_stride = sizeof(gui_primitive_mock),
					}
				}
			};
	}();
	std::println("[GUI] Renderer Initialize Done");

#pragma endregion

#pragma region LoadResource
	std::println("[GUI] Image Atlas Initialize ");
	image_atlas image_atlas{
			ctx,
			ctx.graphic_family(),
			ctx.get_device().graphic_queue(1),
			renderer.get_heap_dynamic_image_section()
		};
	std::println("[GUI] Image Atlas Initialize Done ");

	std::println("[GUI] Font Manager Initialize");
	font::font_manager font_manager{};
	font_manager.set_page(image_atlas.create_image_page("font"));

	{
		auto sys_font_path = font::get_system_fonts();
		auto consolas_family = font::find_family_of(sys_font_path, "Consolas");
		auto segoe_symbol = sys_font_path.find("Segoe UI Symbol");

		const std::filesystem::path font_path = std::filesystem::current_path().append("assets/font").make_preferred();
		auto& SourceHanSansCN_regular = font_manager.register_meta("srchs", font_path / "SourceHanSansCN-Regular.otf");
		const font::font_face_meta* segoe{};
		if(segoe_symbol != sys_font_path.end()){
			segoe = &font_manager.register_meta("segui", segoe_symbol->second);
		}


		std::vector<const font::font_face_meta*> code_faces_{};
		for(const auto& [name, path] : consolas_family){
			auto& meta = font_manager.register_meta(name, path);
			code_faces_.push_back(&meta);
		}

		font_manager.register_family("code", code_faces_, {&SourceHanSansCN_regular, segoe});

		auto& default_family = font_manager.register_family("gui", {&SourceHanSansCN_regular, segoe});

		font_manager.set_default_family(&default_family);

		font::default_font_manager = &font_manager;
	}
	std::println("[GUI] Font Manager Initialize Done");

	{
		std::println("[GUI] Load Logo Image");
		auto& icon_p = image_atlas.create_image_page("tex.logo", {
			.extent = {1920, 1080},
			.format = VK_FORMAT_R8G8B8A8_SRGB,
			.margin = 0
		});
		const auto image_path = std::filesystem::current_path().append("assets/images").make_preferred();

		auto rst = icon_p.register_named_region("logo", image_load_description{
			bitmap_path_load{(image_path / "logo.png").string()}
		}, true);

		gui::assets::builtin::get_page().insert(gui::assets::builtin::shape_id::logo, gui::constant_image_region_borrow{rst.region});
	}

	{
		std::println("[GUI] Generate Icons and Shapes");

		gui::example::generate_default_shapes(image_atlas);
		gui::example::load_default_icons(image_atlas);
	}
#pragma endregion

#pragma region SetupRenderGraph
	std::println("[Compositor] Initialize");

	compositor::manager manager{ctx.get_allocator()};
	vk::shader_module shader_filter_high_light = {
			ctx.get_device(), shader_spv_path / "post_process.highlight_extract.spv"
		};
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

	auto pass_filter_high_light = manager.add_pass<compositor::post_process_pass_with_ubo<high_light_filter_args>>(
		compositor::post_process_meta{
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
	std::println("[Compositor] Initialize Done");
#pragma endregion

#pragma region GuiBindingFn
	auto init_fn = [&](const gui::example::ui_outputs& ui_providers){
		auto& scene = *ui_providers.scene_ptr;

		static constexpr auto post_task = []<typename F>(gui::scene& scene, F&& fn){
			scene.get_output_communicate_async_task_queue(0).post(std::forward<F>(fn));
		};

		auto& bloom_scale = scene.request_independent_react_node(react_flow::make_listener(
			[=, &p = pass_bloom.data, &scene](float val){
				post_task(scene, [&, val]{
					p.set_scale(val);
				});
			}));
		auto& bloom_src_recv = scene.request_independent_react_node(react_flow::make_listener(
			[=, &p = pass_bloom.data, &scene](float val){
				post_task(scene, [&, val]{
					p.set_strength_src(val);
				});
			}));
		auto& bloom_dst_recv = scene.request_independent_react_node(react_flow::make_listener(
			[=, &p = pass_bloom.data, &scene](float val){
				post_task(scene, [&, val]{
					p.set_strength_dst(val);
				});

			}));
		auto& bloom_mix_recv = scene.request_independent_react_node(react_flow::make_listener(
			[=, &p = pass_bloom.data, &scene](float val){
				post_task(scene, [&, val]{
					p.set_mix_factor(val);
				});
			}));

		auto& highlight_thres_recv = scene.request_independent_react_node(react_flow::make_listener(
			[=, &p = pass_filter_high_light.data, &scene](float val){
				post_task(scene, [&, val]{
					p.set_ubo_value(&high_light_filter_args::threshold, val);
				});
			}));
		auto& highlight_smooth_recv = scene.request_independent_react_node(react_flow::make_listener(
			[=, &p = pass_filter_high_light.data, &scene](float val){
				post_task(scene, [&, val]{
					p.set_ubo_value(&high_light_filter_args::smoothness, val);
				});
			}));

		auto& tonemap_contrast = scene.request_independent_react_node(react_flow::make_listener(
			[=, &p = pass_h2s.data, &scene](float val){
				post_task(scene, [&, val]{
					p.set_ubo_value(&tonemap_args::contrast, val);
				});
			}));
		auto& tonemap_exposure = scene.request_independent_react_node(react_flow::make_listener(
			[=, &p = pass_h2s.data, &scene](float val){
				post_task(scene, [&, val]{
					p.set_ubo_value(&tonemap_args::exposure, val);
				});
			}));
		auto& tonemap_saturation = scene.request_independent_react_node(react_flow::make_listener(
			[=, &p = pass_h2s.data, &scene](float val){
				post_task(scene, [&, val]{
					p.set_ubo_value(&tonemap_args::saturation, val);
				});
			}));
		auto& tonemap_gamma = scene.request_independent_react_node(react_flow::make_listener(
			[=, &p = pass_h2s.data, &scene](float val){
				post_task(scene, [&, val]{
					p.set_ubo_value(&tonemap_args::gamma, val);
				});
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

		ui_providers.apply(scene);
	};
#pragma endregion

	std::println("[GUI] Async Scene Setup");
	gui::example::main_loop main_loop{renderer, ctx, init_fn};
	main_loop.permit_burst();
	main_loop.wait_term();
	std::println("[GUI] Async Scene Setup Done");

	auto post_process_cmd = ctx.get_compute_command_pool().obtain();
	ctx.register_post_resize("test", [&](backend::vulkan::context& context, window_instance::resize_event event){

		{
			main_loop.wait_until_idle();

			renderer.resize({event.size.width, event.size.height});

			auto& focus = main_loop.get_scene();
			auto exec_thread = std::exchange(focus.ui_main_thread_id, std::this_thread::get_id());
			focus.resize(math::rect_ortho{tags::from_extent, {}, event.size.width, event.size.height}.as<float>());
			focus.ui_main_thread_id = exec_thread;
		}

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

	app_run(main_loop, ctx, renderer, post_process_cmd);

	std::println("[GUI] Exiting...");

	main_loop.join();

	gui::example::dispose_generated_shapes();
	gui::global::terminate_assets_manager();
	gui::global::terminate();

	image_atlas.request_stop();
	image_atlas.wait_load();
	ctx.wait_on_device();
}

class FastColorErrorBuffer : public std::streambuf {
private:
	std::streambuf* source;
	std::vector<char> buffer;
	static constexpr std::size_t BUF_SIZE = 1024; // 1KB 缓冲区

	const char* RED_START = "\033[31m";
	const char* COLOR_RESET = "\033[0m";

	// 内部刷新逻辑
	bool flush_to_source() {
		std::ptrdiff_t n = pptr() - pbase();
		if (n <= 0) return true;

		// 批量注入颜色：[开始颜色][数据][结束颜色]
		if (source->sputn(RED_START, 5) != 5) return false;
		if (source->sputn(pbase(), n) != n) return false;
		if (source->sputn(COLOR_RESET, 4) != 4) return false;

		pbump(static_cast<int>(-n)); // 重置指针
		return source->pubsync() == 0;
	}

protected:
	// 当缓冲区满时调用
	virtual int_type overflow(int_type c) override {
		if (!flush_to_source()) return EOF;
		if (c != EOF) {
			*pptr() = static_cast<char>(c);
			pbump(1);
		}
		return c;
	}

	// 当调用 std::endl 或 flush 时调用
	virtual int sync() override {
		return flush_to_source() ? 0 : -1;
	}

public:
	explicit FastColorErrorBuffer(std::streambuf* s) : source(s), buffer(BUF_SIZE) {
		// 设置缓冲区区域：开始、当前、结束
		setp(buffer.data(), buffer.data() + buffer.size());
	}

	~FastColorErrorBuffer() override {
		sync();
	}
};

// 自动初始化器
struct GlobalCerrOptimizer {
	std::streambuf* original_buf;
	FastColorErrorBuffer* optimized_buf;

	GlobalCerrOptimizer() {
		original_buf = std::cerr.rdbuf();
		optimized_buf = new FastColorErrorBuffer(original_buf);
		std::cerr.rdbuf(optimized_buf);

		// 关键性能优化：cerr 默认设置了 unitbuf（每次写入都 flush）
		// 如果追求极致性能，可以关闭它，但在错误处理中通常建议保留
		// std::cerr.unsetf(std::ios::unitbuf);
	}

	~GlobalCerrOptimizer() {
		std::cerr.rdbuf(original_buf);
		delete optimized_buf;
	}
};
int main(){
	std::optional<GlobalCerrOptimizer> _;

	using namespace mo_yanxi;
	using namespace graphic;

	if(auto ptr = std::getenv("COLORED"); ptr != nullptr && std::strcmp(ptr, "0") == 0){

	} else{
		_.emplace();
	}

#ifndef NDEBUG
	if(auto ptr = std::getenv("NSIGHT"); ptr != nullptr && std::strcmp(ptr, "1") == 0){
		vk::enable_validation_layers = false;
	} else{
		vk::enable_validation_layers = true;
	}
#endif

	platform::initialize();
	font::initialize();
	backend::glfw::initialize();


	typesetting::rich_text_look_up_table table;
	typesetting::look_up_table = &table;



	VkApplicationInfo appInfo{
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "Hello Xrgui",
		.applicationVersion = VK_MAKE_API_VERSION(1, 0, 0, 0),
		.pEngineName = "No Engine",
		.engineVersion = VK_MAKE_API_VERSION(1, 0, 0, 0),
		.apiVersion = VK_API_VERSION_1_4,
	};


	if (std::uint32_t supportedVersion = 0; vkEnumerateInstanceVersion(&supportedVersion) == VK_SUCCESS) {
		if (supportedVersion >= VK_API_VERSION_1_4) {
			// appInfo.apiVersion = VK_API_VERSION_1_3;
			//currently using 1.4 cause the code dead, I really have no idea why
			appInfo.apiVersion = VK_API_VERSION_1_3;
		} else {
			appInfo.apiVersion = VK_API_VERSION_1_3;
		}
	} else {
		appInfo.apiVersion = VK_API_VERSION_1_0;
	}

	std::println("[Vulkan] API Version: {}.{}.{}.{}", VK_API_VERSION_VARIANT(appInfo.apiVersion), VK_API_VERSION_MAJOR(appInfo.apiVersion), VK_API_VERSION_MINOR(appInfo.apiVersion), VK_API_VERSION_PATCH(appInfo.apiVersion));
	{
		backend::vulkan::context ctx{appInfo};
		vk::load_ext(ctx.get_instance());
		vk::register_default_requirements(ctx.get_device(), ctx.get_physical_device());

		prepare(ctx);
	}

	backend::glfw::terminate();
	font::terminate();
	platform::terminate();
}
