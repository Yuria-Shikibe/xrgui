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
import mo_yanxi.gui.examples;
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

struct slide_line_config{
	float angle{45};
	float scale{1};

	float spacing{5};
	float stroke{15};

	float speed{15};
	float phase{0};

	float margin{0.05f};

	float opacity{0};
};


mo_yanxi::vk::sampler sampler_ui{};
mo_yanxi::vk::shader_module ui_basic{};
mo_yanxi::vk::shader_module ui_msdf{};
mo_yanxi::vk::shader_module ui_blit{};

auto make_renderer(
	mo_yanxi::backend::vulkan::context& ctx
) -> mo_yanxi::backend::vulkan::renderer{
	using namespace mo_yanxi;
	using namespace mo_yanxi::backend::vulkan;

	std::filesystem::path shader_spv_path = std::filesystem::current_path().append("assets/shader/spv").make_preferred();
	ui_basic = {ctx.get_device(), shader_spv_path / "ui.draw.basic.spv"};
	ui_msdf = {ctx.get_device(), shader_spv_path / "ui.draw.sdf.spv"};
	ui_blit = {ctx.get_device(), shader_spv_path / "ui.blit.basic.spv"};
	sampler_ui = {ctx.get_device(), vk::preset::ui_texture_sampler};

	ctx.add_dispose([] noexcept {
		sampler_ui = {};
		ui_basic = {};
		ui_msdf = {};
		ui_blit = {};
	});

	static constexpr auto default_creator = +[](
		draw_pipeline_data& data,
		const draw_pipeline_config& pconfig,
		const draw_attachment_create_info& attachments){
			vk::graphic_pipeline_template gtp{};
			gtp.set_shaders({pconfig.config.shader_modules});
			gtp.set_blending_dynamic();
			data.enables_multisample = pconfig.enables_multisample;
			if(attachments.enables_multisample()){
				gtp.set_multisample(attachments.multisample, 1, pconfig.enables_multisample);
			}

			for(const auto& attachment_config : attachments.attachments){
				gtp.push_color_attachment_format(attachment_config.attachment.format, vk::blending::overwrite);
			}

			data.pipeline = vk::pipeline{
					data.pipeline_layout.get_device(), data.pipeline_layout,
					VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
					gtp
				};
		};

	namespace instr = graphic::draw::instruction;
	std::array draw_user_data_index_tables{
			descriptor_create_config{
				instr::user_data_index_table(std::in_place_type<std::tuple<slide_line_config>>),
				[]{
					vk::descriptor_layout_builder builder{};
					builder.push_seq(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT);
					return builder;
				}()
			}
		};

	std::array draw_pipelines{
			draw_pipeline_config{
				{
					{
						ui_basic.get_create_info(VK_SHADER_STAGE_MESH_BIT_EXT, "main_mesh"),
						ui_basic.get_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, "main_frag")
					},
					{{0, 3}}
				},
				false, default_creator
			},
			draw_pipeline_config{
				{
					{
						ui_basic.get_create_info(VK_SHADER_STAGE_MESH_BIT_EXT, "main_mesh"),
						ui_basic.get_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, "main_frag")
					},
					{{0, 3}}
				},
				true, default_creator
			},
			draw_pipeline_config{
				{
					{
						ui_msdf.get_create_info(VK_SHADER_STAGE_MESH_BIT_EXT, "main_mesh"),
						ui_msdf.get_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, "main_frag")
					},
					{{0, 3}}
				},
				false, default_creator
			}
		};

	std::array blit_pipelines{
			pipeline_config{{ui_blit.get_create_info(VK_SHADER_STAGE_COMPUTE_BIT)}, {}},
		};


	return renderer{
			{
				.allocator = ctx.get_allocator(),
				.sampler = sampler_ui,
				.queue = ctx.graphic_queue(),
				.command_pool = ctx.get_graphic_command_pool(),
				.draw_create_info = {draw_pipelines, draw_user_data_index_tables},
				.blit_create_info = {blit_pipelines},
				.draw_attachment_create_info = draw_attachment_create_info{
					{
						{{VK_FORMAT_R16G16B16A16_SFLOAT}},
						{{VK_FORMAT_R8G8B8A8_UNORM}},
					},
					// VK_SAMPLE_COUNT_4_BIT
				},
				.blit_attachment_create_info = blit_attachment_create_info{
					{
						{VK_FORMAT_R16G16B16A16_SFLOAT},
						{VK_FORMAT_R8G8B8A8_UNORM},

					},
				}
			}
		};
}

void app_run(
	mo_yanxi::backend::vulkan::context& ctx,
	mo_yanxi::graphic::compositor::manager& manager,
	mo_yanxi::vk::command_buffer& cmd
	){
	using namespace mo_yanxi;


	backend::application_timer timer{backend::application_timer<double>::get_default()};
	while(!ctx.window().should_close()){

		ctx.window().poll_events();
		timer.fetch_time();

		gui::global::manager.update(timer.global_delta_tick());
		gui::global::manager.layout();

		gui::global::manager.draw();
		vk::cmd::submit_command(ctx.graphic_queue(), cmd);
		ctx.flush();
	}
}

void run_ui(){

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

	{
		constexpr VkApplicationInfo ApplicationInfo{
			.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
			.pApplicationName = "Hello Xrgui",
			.applicationVersion = VK_MAKE_API_VERSION(1, 0, 0, 0),
			.pEngineName = "No Engine",
			.engineVersion = VK_MAKE_API_VERSION(1, 0, 0, 0),
			.apiVersion = VK_API_VERSION_1_3,
		};

		backend::vulkan::context ctx{ApplicationInfo};
		vk::load_ext(ctx.get_instance());


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

		auto renderer = make_renderer(ctx);
		auto& ui_root = gui::global::manager;
		const auto scene_add_rst = ui_root.add_scene<gui::loose_group>("main", true, renderer.create_frontend());
		scene_add_rst.scene.resize(math::rect_ortho{tags::from_extent, {}, ctx.get_extent().width, ctx.get_extent().height}.as<float>());
		gui::example::build_main_ui(ctx, scene_add_rst.scene, scene_add_rst.root_group);
		scene_add_rst.scene.renderer().push(slide_line_config{});


#pragma region SetupRenderGraph

		compositor::manager manager{ctx.get_allocator()};
		std::filesystem::path shader_spv_path = std::filesystem::current_path().append("assets/shader/spv").make_preferred();
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

		auto cmd = ctx.get_compute_command_pool().obtain();
		ctx.register_post_resize("test", [&](backend::vulkan::context& context, window_instance::resize_event event){
			renderer.resize({event.size.width, event.size.height});
			gui::global::manager.resize(math::rect_ortho{tags::from_extent, {}, event.size.width, event.size.height}.as<float>());

			ctx.wait_on_device();
			ui_input.resource = compositor::image_entity{.handle = renderer.get_base()};
			manager.resize(event.size, true);

			ctx.wait_on_device();

			{
				cmd = ctx.get_compute_command_pool().obtain();
				vk::scoped_recorder s{cmd, VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT};
				manager.create_command(s);
			}

			context.set_staging_image(
				{
					.image = pass_h2s.pass.get_used_resources().get_out(0).as_image().handle.image,
					.extent = event.size,
					.clear = false,
					.owner_queue_family = context.compute_family(),
					.src_stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.src_access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
					.dst_stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.dst_access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
					.src_layout = VK_IMAGE_LAYOUT_GENERAL,
					.dst_layout = VK_IMAGE_LAYOUT_GENERAL
				}, false);
		});
		ctx.record_post_command(true);

		app_run(ctx, manager, cmd);

		ctx.wait_on_device();
	}

	gui::global::terminate_assets_manager();
	gui::global::terminate();
	backend::glfw::terminate();
	font::terminate();

}

int main(){
	using namespace mo_yanxi;
	using namespace graphic;

	run_ui();
}