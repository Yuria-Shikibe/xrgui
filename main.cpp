#include <vulkan/vulkan.h>
import mo_yanxi.vk;
import mo_yanxi.backend.vulkan.context;
import mo_yanxi.backend.vulkan.renderer;
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

import mo_yanxi.vk.cmd;
import std;

import mo_yanxi.gui.examples;
import mo_yanxi.pipeline_configure;

import instruction_draw.batch.v2;
import renderer_v2;
import mo_yanxi.gui.draw_config;

void app_run(
	mo_yanxi::backend::vulkan::context& ctx,
	mo_yanxi::backend::vulkan::renderer_v2& renderer,
	mo_yanxi::graphic::compositor::manager& manager,
	mo_yanxi::vk::command_buffer& cmdBUf
	){
	using namespace mo_yanxi;


	backend::application_timer timer{backend::application_timer<double>::get_default()};
	while(!ctx.window().should_close()){

		ctx.window().poll_events();
		timer.fetch_time();

		gui::global::manager.update(timer.global_delta_tick());
		gui::global::manager.layout();

		renderer.batch.begin_rendering();
		renderer.batch.get_data_group_non_vertex_info().push_default(gui::draw_config::ui_state(
			timer.global_time()
		));
		renderer.batch.get_data_group_non_vertex_info().push_default(gui::draw_config::slide_line_config{});

		auto& r = gui::global::manager.get_current_focus().renderer();
		r.init_projection();
		{
			using namespace graphic::draw::instruction;

			for(int x = 0; x < 5; ++x){
				for(int y = 0; y < 5; ++y){
					r.push(rectangle_ortho{
						.generic = {.mode = std::to_underlying(gui::primitive_draw_mode::draw_slide_line)},
						.v00 = {x * 60.f, y * 60.f},
						.v11 = {x * 60.f + 40, y * 60.f + 40},
						.vert_color = {graphic::colors::white.copy().mul_a(.4f)}
					});
					if((x + y) & 1){

						r.push(gui::draw_config::slide_line_config{
							.angle = -45,
							.spacing = 10,
							.stroke = 15,
						});
					}else{
						r.push(gui::draw_config::slide_line_config{});
					}
				}
			}
		}

		gui::global::manager.draw();
		renderer.batch.end_rendering();
		renderer.batch.load_to_gpu();
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
		auto& SourceHanSansCN_regular = font_manager.register_face("srchs", (font_path / "SourceHanSansCN-Regular.otf").string());
		font::typesetting::default_font_manager = &font_manager;
		font::typesetting::default_font = &SourceHanSansCN_regular;
	}
#pragma endregion

#pragma region InitUI
	vk::sampler sampler_ui{ctx.get_device(), vk::preset::ui_texture_sampler};

	backend::vulkan::renderer_v2 renderer{ctx.get_allocator(), ctx.get_graphic_command_pool(), sampler_ui};
	// auto renderer = gui::make_renderer(ctx);
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


	auto pass_bloom = manager.add_pass<compositor::bloom_pass>(compositor::get_bloom_default_meta(shader_bloom));
	pass_bloom.meta.set_sampler_at_binding(0, sampler_blit);
	pass_bloom.pass.add_dep({pass_filter_high_light.id(), 0, 0});
	pass_bloom.pass.add_local({1, compositor::no_slot});


	compositor::post_process_meta meta{
			shader_hdr_to_sdr, {
				{{0}, compositor::no_slot, 0},
				{{1}, 0, compositor::no_slot},
			}
		};
	meta.sockets.at_out(0).get<compositor::image_requirement>().usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

	auto pass_h2s = manager.add_pass<compositor::post_process_stage>(meta);
	pass_h2s.id()->add_dep({pass_bloom.id(), 0, 0});
	pass_h2s.id()->add_local({compositor::no_slot, 0});

	manager.sort();
#pragma endregion

	auto post_process_cmd = ctx.get_compute_command_pool().obtain();
	ctx.register_post_resize("test", [&](backend::vulkan::context& context, window_instance::resize_event event){
		renderer.resize({event.size.width, event.size.height});
		gui::global::manager.resize(math::rect_ortho{tags::from_extent, {}, event.size.width, event.size.height}.as<float>());

		ui_input.resource = compositor::image_entity{.handle = renderer.get_base()};
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

	ctx.wait_on_device();
}

int main(){
	using namespace mo_yanxi;
	using namespace graphic;

#ifndef NDEBUG
	if(auto ptr = std::getenv("NSIGHT"); ptr != nullptr && std::strcmp(ptr, "1") == 0){
		vk::enable_validation_layers = false;
	}else{
		vk::enable_validation_layers = true;
	}
#endif

	font::initialize();
	backend::glfw::initialize();
	gui::global::initialize();
	gui::global::initialize_assets_manager(gui::global::manager.get_arena_id());


	prepare();

	gui::global::terminate_assets_manager();
	gui::global::terminate();
	backend::glfw::terminate();
	font::terminate();
}