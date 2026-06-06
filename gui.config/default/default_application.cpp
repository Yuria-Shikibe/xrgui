module;

#include <cstdlib>
#include <vulkan/vulkan.h>

module mo_yanxi.gui.cfg.default_application;

import std;

import mo_yanxi.vk;
import mo_yanxi.vk.cmd;

import mo_yanxi.backend.glfw.window;
import mo_yanxi.backend.communicator;
import mo_yanxi.backend.application_timer;
import mo_yanxi.backend.vulkan.context;
import mo_yanxi.backend.vulkan.renderer;

import mo_yanxi.graphic.g2d;
import mo_yanxi.graphic.image_atlas;

import mo_yanxi.gui.global;
import mo_yanxi.gui.assets.manager;
import mo_yanxi.gui.image_regions;
import mo_yanxi.gui.fx.instruction_extension;
import mo_yanxi.gui.cfg.builtin.assets;
import mo_yanxi.gui.cfg.builtin.constants;
import mo_yanxi.gui.cfg.builtin.font_styles;
import mo_yanxi.gui.cfg.builtin.log_channels;
import mo_yanxi.gui.cfg.builtin.main_loop;
import mo_yanxi.gui.cfg.builtin.scene;

import mo_yanxi.font;
import mo_yanxi.font.manager;
import mo_yanxi.typesetting.rich_text;

import mo_yanxi.core.platform;

namespace mo_yanxi::gui::cfg{
namespace{

constexpr std::string_view default_scene_name{"xrgui.default_application"};

struct default_application_payload{
};

using default_application_loop = builtin::main_loop<default_application_payload>;

void configure_runtime_working_directory(const std::filesystem::path& executable_path){
	if(executable_path.empty()){
		return;
	}

	const auto executable_dir = std::filesystem::absolute(executable_path).parent_path();
	if(!executable_dir.empty()){
		std::filesystem::current_path(executable_dir);
	}
}

VkApplicationInfo make_application_info(const default_application_config& config){
	VkApplicationInfo app_info{
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = config.app_name.c_str(),
		.applicationVersion = VK_MAKE_API_VERSION(1, 0, 0, 0),
		.pEngineName = "No Engine",
		.engineVersion = VK_MAKE_API_VERSION(1, 0, 0, 0),
		.apiVersion = VK_API_VERSION_1_3,
	};

	if(std::uint32_t supported_version = 0; vkEnumerateInstanceVersion(&supported_version) == VK_SUCCESS){
		app_info.apiVersion = supported_version >= VK_API_VERSION_1_3
			                      ? VK_API_VERSION_1_3
			                      : VK_API_VERSION_1_0;
	}

	return app_info;
}

bool env_flag_enabled(const char* name){
#ifdef _WIN32
	char* value{};
	std::size_t value_size{};
	if(_dupenv_s(&value, &value_size, name) != 0 || value == nullptr){
		return false;
	}

	std::unique_ptr<char, decltype(&std::free)> holder{value, std::free};
	return std::string_view{value} == "1";
#else
	if(auto value = std::getenv(name); value != nullptr){
		return std::string_view{value} == "1";
	}
	return false;
#endif
}

backend::vulkan::renderer make_default_renderer(
	backend::vulkan::context& ctx,
	VkSampler sampler,
	const std::filesystem::path& shader_spv_path){
	using namespace mo_yanxi;

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
	return renderer{
		renderer_create_info{
			.allocator_usage = ctx.get_allocator(),
			.command_pool = ctx.get_graphic_command_pool(),
			.sampler = sampler,
			.attachment_draw_config = {
				{
					draw_attachment_config{
						.attachment = {VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT}
					},
					draw_attachment_config{
						.attachment = {VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT}
					},
				},
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
}

void set_default_scene_pass_config(builtin::example_scene& scene){
	scene.pass_config = {
		{
			gui::fx::scene_render_pass_config::value_type{
				.begin_config = {
					.draw_targets = 0b1,
				},
				.end_config = std::nullopt
			},
			gui::fx::scene_render_pass_config::value_type{
				.begin_config = {
					.draw_targets = 0b10,
				},
				.end_config = std::nullopt
			}
		},
		gui::fx::blit_pipeline_config{}
	};
}

}

struct default_application::state{
	default_application& app;

	typesetting::rich_text_look_up_table rich_text_table{};

	std::optional<backend::vulkan::context> ctx{};
	std::optional<vk::sampler> ui_sampler{};
	std::optional<graphic::image_atlas> atlas{};
	std::optional<font::font_manager> fonts{};
	std::optional<default_application_loop> loop{};

	gui::scene* scene_ptr{};

	bool platform_initialized{};
	bool font_initialized{};
	bool glfw_initialized{};
	bool gui_initialized{};
	bool assets_manager_initialized{};
	bool generated_shapes_initialized{};
	bool scene_created{};
	bool shutdown_done{};

	explicit state(default_application& app)
		: app(app){
	}

	~state(){
		shutdown();
	}

	void initialize(){
		builtin::configure_gui_log_channels();
		configure_runtime_working_directory(app.config_.executable_path);

		platform::initialize();
		platform_initialized = true;

		font::initialize();
		font_initialized = true;

		backend::glfw::initialize();
		glfw_initialized = true;

		typesetting::look_up_table = &rich_text_table;

		gui::global::initialize();
		gui_initialized = true;
		gui::global::initialize_assets_manager(gui::global::manager.get_arena_id());
		assets_manager_initialized = true;

		vk::enable_validation_layers = app.config_.enable_validation_layers;
		if(env_flag_enabled("NSIGHT")){
			vk::enable_validation_layers = false;
		}

		auto app_info = make_application_info(app.config_);
		ctx.emplace(app_info);
		vk::load_ext(ctx->get_instance());
		vk::register_default_requirements(ctx->get_device(), ctx->get_physical_device());

		const auto shader_spv_path = std::filesystem::current_path().append("assets/shader/spv").make_preferred();
		ui_sampler.emplace(ctx->get_device(), vk::preset::ui_texture_sampler);
		auto renderer = make_default_renderer(*ctx, *ui_sampler, shader_spv_path);

		atlas.emplace(
			*ctx,
			ctx->graphic_family(),
			ctx->get_device().graphic_queue(1),
			renderer.get_image_view_registry(),
			renderer.get_default_sampler_index());

		fonts.emplace();
		builtin::init_font_manager(*fonts, *atlas);

		load_default_images();

		loop.emplace(std::move(renderer), *ctx, default_application_loop::functions{
			.init_fn = [this](default_application_loop& loop){
				return initialize_scene(loop);
			},
			.main_loop_fn = [this](default_application_loop& loop){
				run_gui_frame(loop);
			},
			.exit_fn = [this](const default_application_loop&){
				clear_scene();
			}
		});

		ctx->register_post_resize("xrgui.default_application", [this](
			backend::vulkan::context& context,
			const window_instance::resize_event& event){
			loop->resize({event.size.width, event.size.height});
			context.set_staging_image({
				.image = loop->get_renderer().get_base().image,
				.extent = event.size,
				.clear = false,
				.force_blit = true,
				.owner_queue_family = context.graphic_family(),
				.src_stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.src_access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				.dst_stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.dst_access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				.src_layout = VK_IMAGE_LAYOUT_GENERAL,
				.dst_layout = VK_IMAGE_LAYOUT_GENERAL
			}, false);
		});
		ctx->record_post_command(true);
	}

	int run(){
		initialize();

		backend::application_timer timer{backend::application_timer<double>::get_default()};

		loop->wait_term_and_reset();
		while(!ctx->window().should_close()){
			ctx->window().poll_events();
			loop->get_window_dispatcher().drain();

			timer.fetch_time();
			gui::global::event_queue.push_frame_split(timer.global_delta());

			loop->get_window_dispatcher().drain();
			loop->permit_burst();
			loop->get_scene().get_output_communicate_async_task_queue(0).consume();

			loop->wait_term();
			loop->get_window_dispatcher().drain();

			vk::cmd::submit_command(
				ctx->graphic_queue(),
				loop->get_renderer().get_valid_cmd_buf(),
				loop->get_renderer().get_fence());
			ctx->flush();
			loop->reset_term();
		}

		shutdown();
		return 0;
	}

	void load_default_images(){
		auto& logo_page = atlas->create_image_page("tex.logo", {
			.extent = {1920, 1080},
			.format = VK_FORMAT_R8G8B8A8_SRGB,
			.margin = 0
		});

		const auto image_path = std::filesystem::current_path().append("assets/images").make_preferred();
		auto rst = logo_page.register_named_region("logo", graphic::image_load_description{
			graphic::bitmap_path_load{(image_path / "logo.png").string()}
		}, true);

		gui::assets::builtin::get_page().insert(
			gui::assets::builtin::shape_id::logo,
			gui::constant_image_region_borrow{rst.region});

		builtin::generate_default_shapes(*atlas);
		builtin::load_default_icons(*atlas);
		generated_shapes_initialized = true;
	}

	builtin::main_loop_init_return_t initialize_scene(default_application_loop& loop){
		auto& ui_root = gui::global::manager;
		auto& resources = ui_root.add_scene_resources(default_scene_name);
		auto style_pal_prov = builtin::make_styles(resources);

		const auto scene_add_rst = ui_root.add_scene<builtin::example_scene, gui::loose_group>(
			default_scene_name,
			resources,
			true,
			loop.get_renderer().create_frontend());

		auto& scene = scene_add_rst.scene;
		auto& root = scene_add_rst.root_group;
		scene_ptr = &scene;
		scene_created = true;

		style_pal_prov.add_to_scene(scene);
		scene.enable_elem_async_task_post(true);
		scene.drop_and_reset_communicate_async_task_queue_size(1);
		builtin::set_cursors(scene);
		set_default_scene_pass_config(scene);

		scene.resources().set_native_communicator<backend::glfw::communicator>(
			ctx->window().get_handle(),
			loop.get_window_dispatcher());
		scene.get_communicator()->set_native_cursor_visibility(!app.config_.hide_native_cursor);

		root.set_style();
		app.build_gui(scene, root);

		return {&scene};
	}

	void run_gui_frame(default_application_loop& loop){
		auto& current_focus = loop.get_scene();
		const auto delta = gui::global::consume_current_input(current_focus, [this](input_handle::input_event_variant event){
			app.on_unhandled_input(event);
		});

		app.before_frame(std::chrono::duration<float>{static_cast<float>(delta.count())});

		current_focus.layout();

		auto& renderer = loop.get_renderer();
		auto& frontend = current_focus.renderer();

		renderer.batch_host.begin_rendering();
		renderer.batch_host.get_data_group_non_vertex_info().push_default(gui::fx::ui_state(
			frontend.get_region().extent(),
			static_cast<float>(current_focus.get_current_time() / 60.)));
		renderer.batch_host.get_data_group_non_vertex_info().push_default(gui::fx::slide_line_config{});

		frontend.init_timeline_variable();
		frontend.update_state(frontend.get_full_screen_scissor());
		frontend.update_state(frontend.get_full_screen_viewport());
		frontend.update_state(gui::fx::pipeline_config{.pipeline_index = builtin::gpip::idx::def});
		frontend.update_state(gui::fx::push_constant{builtin::gpip::default_draw_constants{}});
		frontend.update_state(gui::fx::blend::pma::standard);
		frontend.update_state(gui::fx::make_blend_write_mask(true), 0);

		current_focus.draw();

		renderer.batch_host.end_rendering();
		renderer.upload();
		renderer.create_command();

		app.after_frame();
	}

	void clear_scene() noexcept{
		if(!scene_created){
			return;
		}

		auto& ui_root = gui::global::manager;
		ui_root.erase_scene(default_scene_name);
		ui_root.erase_resource(default_scene_name);
		scene_ptr = nullptr;
		scene_created = false;
	}

	void shutdown() noexcept{
		if(shutdown_done){
			return;
		}
		shutdown_done = true;

		try{
			if(loop){
				loop->get_window_dispatcher().drain();
				loop->join();
				loop->get_window_dispatcher().drain();
				loop->get_window_dispatcher().stop();
			}
		} catch(...){
		}

		clear_scene();

		if(generated_shapes_initialized){
			builtin::dispose_generated_shapes();
			generated_shapes_initialized = false;
		}
		if(assets_manager_initialized){
			gui::global::terminate_assets_manager();
			assets_manager_initialized = false;
		}
		if(gui_initialized){
			gui::global::terminate();
			gui_initialized = false;
		}
		if(atlas){
			atlas->request_stop();
		}
		if(ctx){
			try{
				ctx->wait_on_device();
			} catch(...){
			}
		}

		loop.reset();
		font::default_font_manager = nullptr;
		fonts.reset();
		atlas.reset();
		ui_sampler.reset();
		ctx.reset();

		typesetting::look_up_table = nullptr;

		if(glfw_initialized){
			backend::glfw::terminate();
			glfw_initialized = false;
		}
		if(font_initialized){
			font::terminate();
			font_initialized = false;
		}
		if(platform_initialized){
			platform::terminate();
			platform_initialized = false;
		}
	}

	backend::vulkan::context& context(){
		if(!ctx){
			throw std::logic_error{"default_application context is not initialized"};
		}
		return *ctx;
	}

	backend::vulkan::renderer& renderer(){
		if(!loop){
			throw std::logic_error{"default_application renderer is not initialized"};
		}
		return loop->get_renderer();
	}

	graphic::image_atlas& image_atlas(){
		if(!atlas){
			throw std::logic_error{"default_application image atlas is not initialized"};
		}
		return *atlas;
	}

	gui::scene& scene(){
		if(scene_ptr == nullptr){
			throw std::logic_error{"default_application scene is not initialized"};
		}
		return *scene_ptr;
	}

	gui::window_thread_dispatcher& window_dispatcher(){
		if(!loop){
			throw std::logic_error{"default_application window dispatcher is not initialized"};
		}
		return loop->get_window_dispatcher();
	}
};

default_application::default_application(default_application_config config)
	: config_(std::move(config)){
}

default_application::~default_application() = default;

int default_application::run(){
	if(state_){
		throw std::logic_error{"default_application::run called while the application is already running"};
	}

	state_ = std::make_unique<state>(*this);
	try{
		const int result = state_->run();
		state_.reset();
		return result;
	} catch(...){
		state_.reset();
		throw;
	}
}

backend::vulkan::context& default_application::context(){
	if(!state_){
		throw std::logic_error{"default_application is not running"};
	}
	return state_->context();
}

backend::vulkan::renderer& default_application::renderer(){
	if(!state_){
		throw std::logic_error{"default_application is not running"};
	}
	return state_->renderer();
}

graphic::image_atlas& default_application::image_atlas(){
	if(!state_){
		throw std::logic_error{"default_application is not running"};
	}
	return state_->image_atlas();
}

gui::scene& default_application::scene(){
	if(!state_){
		throw std::logic_error{"default_application is not running"};
	}
	return state_->scene();
}

gui::window_thread_dispatcher& default_application::window_dispatcher(){
	if(!state_){
		throw std::logic_error{"default_application is not running"};
	}
	return state_->window_dispatcher();
}

}
