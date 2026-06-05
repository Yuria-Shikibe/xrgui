#include <cstdio>
#include <vulkan/vulkan.h>

import std;

import mo_yanxi.vk;
import mo_yanxi.vk.cmd;

import mo_yanxi.backend.vulkan.context;
import mo_yanxi.backend.glfw.window;
import mo_yanxi.backend.application_timer;
import mo_yanxi.backend.vulkan.renderer;

import mo_yanxi.graphic.g2d;
import mo_yanxi.graphic.image_atlas;
import mo_yanxi.graphic.compositor.manager;
import mo_yanxi.graphic.compositor.post_process_pass;
import mo_yanxi.graphic.compositor.post_process_pass_with_ubo;
import mo_yanxi.graphic.compositor.bloom;
import mo_yanxi.graphic.compositor.fullscreen_present_pass;

import mo_yanxi.gui.infrastructure;
import mo_yanxi.gui.elem.group;
import mo_yanxi.gui.global;
import mo_yanxi.gui.assets.manager;
import mo_yanxi.gui.renderer.frontend;
import mo_yanxi.gui.fx.instruction_extension;

import mo_yanxi.gui.cfg.builtin.assets;
import mo_yanxi.gui.image_regions;

import mo_yanxi.font;
import mo_yanxi.font.plat;
import mo_yanxi.font.manager;
import mo_yanxi.typesetting;
import mo_yanxi.typesetting.segmented_layout;
import mo_yanxi.typesetting.util;

import mo_yanxi.react_flow.common;
import mo_yanxi.react_flow;

import mo_yanxi.gui.markdown;
import mo_yanxi.core.platform;

import mo_yanxi.gui.examples;
import mo_yanxi.gui.cfg.builtin.main_loop;
import mo_yanxi.gui.examples.loop_exec;
import mo_yanxi.gui.cfg.builtin.colored_cerr;
import mo_yanxi.gui.cfg.builtin.font_styles;
import mo_yanxi.gui.cfg.builtin.log_channels;
import mo_yanxi.log;


struct alignas(16) high_light_filter_args{
	float threshold{1.3f};
	float smoothness{.5f};
	float max_brightness{10};
};

void configure_example_runtime_working_directory(const char* executable_path){
	if(executable_path == nullptr || executable_path[0] == '\0'){
		throw std::runtime_error{"xrgui.example executable path is unavailable"};
	}

	const auto executable_dir = std::filesystem::absolute(std::filesystem::path{executable_path}).parent_path();
	if(executable_dir.empty()){
		throw std::runtime_error{"xrgui.example executable directory is unavailable"};
	}

	std::filesystem::current_path(executable_dir);
}

void app_run(
	mo_yanxi::gui::cfg::builtin::main_loop_type& main_loop,
	std::vector<mo_yanxi::vk::command_buffer>& post_process_cmds
){
	using namespace mo_yanxi;

	backend::application_timer timer{backend::application_timer<double>::get_default()};

	auto& current_focus = main_loop.get_scene();
	log::info({"App"}, "entering main loop");
	main_loop.wait_term_and_reset();

	auto& ctx = main_loop.get_ctx();
	while(!ctx.window().should_close()){
		ctx.window().poll_events();
		main_loop.get_window_dispatcher().drain();
		timer.fetch_time();
		//
		gui::global::event_queue.push_frame_split(timer.global_delta());

		auto output_token = ctx.acquire_output_frame();
		if(!output_token.acquired){
			continue;
		}

		main_loop.get_window_dispatcher().drain();
		main_loop.permit_burst();
		current_focus.get_output_communicate_async_task_queue(0).consume();

		//Clear unused events currently
		(void)main_loop.unhandled_events.fetch();

		main_loop.wait_term();
		main_loop.get_window_dispatcher().drain();
		std::array<VkCommandBuffer, 2> buffers{
			main_loop.get_renderer().get_valid_cmd_buf(),
			post_process_cmds.at(output_token.image.index)
		};
		vk::cmd::submit_command(
			ctx.graphic_queue(),
			buffers,
			output_token.frame_fence,
			output_token.acquire_semaphore,
			VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
			output_token.render_finished_semaphore,
			VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
		main_loop.get_renderer().set_current_external_submit_fence(output_token.frame_fence);
		ctx.present_output_frame(output_token);
		main_loop.reset_term();
	}
	main_loop.get_window_dispatcher().drain();
}

void prepare(mo_yanxi::backend::vulkan::context& ctx){
	using namespace mo_yanxi;
	using namespace graphic;

	const auto shader_spv_path = std::filesystem::current_path().append("assets/shader/spv").make_preferred();

	log::info({"GUI"}, "core initialize");
	gui::global::initialize();
	gui::global::initialize_assets_manager(gui::global::manager.get_arena_id());
	log::info({"GUI"}, "core initialize done");

#pragma region InitRenderer
	log::info({"GUI"}, "renderer initialize");
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
	log::info({"GUI"}, "renderer initialize done");

#pragma endregion

#pragma region LoadResource
	log::info({"GUI"}, "image atlas initialize");
	image_atlas image_atlas{
			ctx,
			ctx.graphic_family(),
			ctx.get_device().graphic_queue(1),
			renderer.get_image_view_registry(),
			renderer.get_default_sampler_index()
		};
	log::info({"GUI"}, "image atlas initialize done");

	log::info({"GUI"}, "font manager initialize");
	font::font_manager font_manager{};
	gui::cfg::builtin::init_font_manager(font_manager, image_atlas);
	log::info({"GUI"}, "font manager initialize done");

	{
		log::info({"GUI"}, "load logo image");
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
		log::info({"GUI"}, "generate icons and shapes");

		gui::cfg::builtin::generate_default_shapes(image_atlas);
		gui::cfg::builtin::load_default_icons(image_atlas);
	}
#pragma endregion

#pragma region SetupRenderGraph
	log::info({"Compositor"}, "initialize");

	compositor::manager manager{ctx.get_allocator()};
	compositor::compute_shader_info shader_filter_high_light{
		vk::shader_module{ctx.get_device(), shader_spv_path / "post_process.highlight_extract.spv"}
	};
	compositor::compute_shader_info shader_merge{
		vk::shader_module{ctx.get_device(), shader_spv_path / "ui.merge.spv"}
	};
	vk::shader_module shader_present_vert{
		ctx.get_device(), shader_spv_path / "post_process.fullscreen_present.vert.spv"
	};
	vk::shader_module shader_present_frag{
		ctx.get_device(), shader_spv_path / "post_process.fullscreen_present.frag.spv"
	};

	vk::sampler sampler_blit{ctx.get_device(), vk::preset::default_blit_sampler};
	compositor::compute_shader_info shader_bloom{
		vk::shader_module{ctx.get_device(), shader_spv_path / "post_process.bloom.spv"}
	};
	auto make_bloom_shader = [&]{
		return vk::shader_module{ctx.get_device(), shader_spv_path / "post_process.bloom.spv"};
	};

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

	auto& present_output = manager.add_external_resource(compositor::resource_entity_external{
			compositor::image_entity{}, compositor::resource_dependency{
				.src_stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
				.src_access = VK_ACCESS_2_NONE,
				.dst_stage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
				.dst_access = VK_ACCESS_2_NONE,
				.src_layout = VK_IMAGE_LAYOUT_UNDEFINED,
				.dst_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			}
		});

	auto pass_filter_high_light = manager.add_pass<compositor::post_process_pass_with_ubo<high_light_filter_args>>(
		compositor::post_process_meta{
			std::move(shader_filter_high_light), {
				{{0}, compositor::no_slot, 0},
				{{1}, 0, compositor::no_slot},
			}
		});

	pass_filter_high_light.id()->add_input({{ui_input_base, 0}});


	auto pass_bloom = manager.add_pass<compositor::bloom_pass>(compositor::get_bloom_default_meta(std::move(shader_bloom), {}));
	pass_bloom.data.set_sampler_at_binding(0, sampler_blit);
	pass_bloom.pass.add_dep({pass_filter_high_light.id(), 0, 0});
	pass_bloom.pass.add_local({1, compositor::no_slot});


	static constexpr VkSpecializationMapEntry SpecEntry{0, 0, 4};
	static constexpr VkBool32 SpecData{true};
	auto pass_blur = manager.add_pass<compositor::bloom_pass>(compositor::get_bloom_default_meta(
		compositor::compute_shader_info{
			make_bloom_shader(), "main",
			VkSpecializationInfo{
				.mapEntryCount = 1,
				.pMapEntries = &SpecEntry,
				.dataSize = sizeof(SpecData),
				.pData = &SpecData
			}
		}, {
			.target_scale = 1,
			.format = VK_FORMAT_R8G8B8A8_UNORM

		}));
	pass_blur.data.set_max_mip_level(5);
	pass_blur.data.set_sampler_at_binding(0, sampler_blit);
	pass_blur.id()->add_input({{input_background, 0}});
	pass_blur.id()->add_local({1, compositor::no_slot});


	auto pass_merge = manager.add_pass<compositor::post_process_stage>(compositor::post_process_meta{
			std::move(shader_merge), {
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


	auto pass_present = manager.add_pass<compositor::fullscreen_present_stage>(
		compositor::fullscreen_present_shader_info{
			.vertex_shader = std::move(shader_present_vert),
			.fragment_shader = std::move(shader_present_frag),
		},
		ctx.output_image(0).format);
	pass_present.id()->add_dep({pass_merge.id(), 0, 0});
	pass_present.id()->add_output({{present_output, 0}});

	manager.sort();

	renderer.resize({64, 64});
	ui_input_base.resource = compositor::image_entity{.handle = renderer.get_blit_attachments()[0]};
	ui_input_back.resource = compositor::image_entity{.handle = renderer.get_blit_attachments()[1]};
	input_background.resource = compositor::image_entity{.handle = renderer.get_blit_attachments()[2]};
	manager.set_frame_count(ctx.output_image_count());
	pass_present.data.set_output_format(ctx.output_image(0).format);
	for(std::uint32_t frame_slot = 0; frame_slot < ctx.output_image_count(); ++frame_slot){
		present_output.set_frame_resource(
			frame_slot,
			compositor::image_entity{.handle = ctx.output_image(frame_slot).handle});
	}
	manager.resize({64, 64}, true);

	pass_blur.data.set_scale(1.25f);
	pass_blur.data.set_mix_factor(.025f);
	pass_blur.data.set_strength(1.f, 1.f);
	log::info({"Compositor"}, "initialize done");
#pragma endregion

#pragma region GuiBindingFn
	auto init_fn = [&](gui::cfg::builtin::main_loop_type& loop) -> gui::cfg::builtin::main_loop_init_return_t {
		gui::cfg::builtin::main_loop_init_return_t ret{};

		auto ui_providers = gui::cfg::builtin::build_main_ui(
			loop.get_ctx(),
			loop.get_renderer().create_frontend(),
			image_atlas,
			loop.get_window_dispatcher());
		auto& scene = *ui_providers.scene_ptr;
		ret.main_scene = ui_providers.scene_ptr;

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
			[=, &p = pass_present.data, &scene](float val){
				post_task(scene, [&, val]{
					p.set_ubo_value(&compositor::fullscreen_present_params::contrast, val);
				});
			}));
		auto& tonemap_exposure = scene.request_independent_react_node(react_flow::make_listener(
			[=, &p = pass_present.data, &scene](float val){
				post_task(scene, [&, val]{
					p.set_ubo_value(&compositor::fullscreen_present_params::exposure, val);
				});
			}));
		auto& tonemap_saturation = scene.request_independent_react_node(react_flow::make_listener(
			[=, &p = pass_present.data, &scene](float val){
				post_task(scene, [&, val]{
					p.set_ubo_value(&compositor::fullscreen_present_params::saturation, val);
				});
			}));
		auto& tonemap_gamma = scene.request_independent_react_node(react_flow::make_listener(
			[=, &p = pass_present.data, &scene](float val){
				post_task(scene, [&, val]{
					p.set_ubo_value(&compositor::fullscreen_present_params::gamma, val);
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

		return ret;
	};

#pragma endregion

	log::info({"GUI"}, "async scene setup");
	gui::cfg::builtin::main_loop_type main_loop{std::move(renderer), ctx, {
			.init_fn = init_fn,
			.main_loop_fn = gui::cfg::builtin::main_loop_fn,
			.exit_fn = [](const gui::cfg::builtin::main_loop_type&){
				gui::cfg::builtin::clear_main_ui();
			}
		}};

	log::info({"GUI"}, "async scene setup done");

	std::vector<vk::command_buffer> post_process_cmds{};
	auto rebuild_post_process_commands = [&](
		backend::vulkan::context& context,
		const VkExtent2D extent){
		const auto frame_count = context.output_image_count();
		manager.set_frame_count(frame_count);
		pass_present.data.set_output_format(context.output_image(0).format);
		for(std::uint32_t frame_slot = 0; frame_slot < frame_count; ++frame_slot){
			present_output.set_frame_resource(
				frame_slot,
				compositor::image_entity{.handle = context.output_image(frame_slot).handle});
		}

		manager.resize(extent, true);

		if(post_process_cmds.size() != frame_count){
			post_process_cmds.clear();
			post_process_cmds.reserve(frame_count);
			for(std::uint32_t frame_slot = 0; frame_slot < frame_count; ++frame_slot){
				post_process_cmds.push_back(context.get_graphic_command_pool().obtain());
			}
		}

		for(std::uint32_t frame_slot = 0; frame_slot < frame_count; ++frame_slot){
			vk::scoped_recorder recorder{
				post_process_cmds[frame_slot],
				VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT
			};
			manager.create_command(post_process_cmds[frame_slot], frame_slot);
		}
	};

	ctx.register_post_resize("test", [&](backend::vulkan::context& context, window_instance::resize_event event){

		main_loop.resize({event.size.width, event.size.height});
		{
			auto& r = main_loop.get_renderer();

			ui_input_base.resource = compositor::image_entity{.handle = r.get_blit_attachments()[0]};
			ui_input_back.resource = compositor::image_entity{.handle = r.get_blit_attachments()[1]};
			input_background.resource = compositor::image_entity{.handle = r.get_blit_attachments()[2]};
		}

		rebuild_post_process_commands(context, event.size);
	});

	app_run(main_loop, post_process_cmds);

	log::info({"GUI"}, "exiting");

	main_loop.join();

	gui::cfg::builtin::dispose_generated_shapes();
	gui::global::terminate_assets_manager();
	gui::global::terminate();

	image_atlas.request_stop();
	image_atlas.wait_load();
	ctx.wait_on_device();
}

int main(int argc, char** argv){
	using namespace mo_yanxi;
	using namespace graphic;

	//auto _cerr = make_colored_errc();
	gui::cfg::builtin::configure_gui_log_channels();
	configure_example_runtime_working_directory(argc > 0 ? argv[0] : nullptr);
	log::info({"Assets"}, "using runtime working directory {}", std::filesystem::current_path().string());

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
			appInfo.apiVersion = VK_API_VERSION_1_4;
		} else {
			appInfo.apiVersion = VK_API_VERSION_1_3;
		}
	} else {
		appInfo.apiVersion = VK_API_VERSION_1_0;
	}

	log::info({"Vulkan"}, "API version: {}.{}.{}.{}", VK_API_VERSION_VARIANT(appInfo.apiVersion), VK_API_VERSION_MAJOR(appInfo.apiVersion), VK_API_VERSION_MINOR(appInfo.apiVersion), VK_API_VERSION_PATCH(appInfo.apiVersion));
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
