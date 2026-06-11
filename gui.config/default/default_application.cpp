module;

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
import mo_yanxi.backend.miniaudio.audio;

import mo_yanxi.graphic.g2d;
import mo_yanxi.graphic.image_atlas;
import mo_yanxi.audio;

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
import mo_yanxi.gui.cfg.render_context;

import mo_yanxi.font;
import mo_yanxi.font.manager;
import mo_yanxi.typesetting.rich_text;

import mo_yanxi.platform;
import mo_yanxi.log;

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

	std::optional<render_context> gui_render_context{};
	std::optional<audio::audio_system> audio_system{};
	std::optional<vk::command_pool> post_command_pool{};
	std::vector<vk::command_buffer> post_commands{};
	std::optional<default_application_loop> loop{};

	gui::scene* scene_ptr{};

	bool platform_initialized{};
	bool font_initialized{};
	bool glfw_initialized{};
	bool scene_created{};
	bool shutdown_done{};

	explicit state(default_application& app)
		: app(app){
	}

	~state(){
		shutdown();
	}

	void ensure_post_commands(backend::vulkan::context& context){
		if(!post_command_pool){
			throw std::logic_error{"default_application post command pool is not initialized"};
		}

		const auto frame_count = context.output_image_count();
		if(post_commands.size() == frame_count){
			return;
		}

		post_commands.clear();
		post_commands.reserve(frame_count);
		for(std::uint32_t frame_slot = 0; frame_slot < frame_count; ++frame_slot){
			post_commands.push_back(post_command_pool->obtain());
		}
	}

	void record_context_post_commands(backend::vulkan::context& context){
		ensure_post_commands(context);
		context.record_post_command(std::span<const vk::command_buffer>{post_commands});
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

		audio_system.emplace(backend::miniaudio::make_audio_driver(app.config_.audio_device));
		static_cast<void>(audio_system->register_channel(audio::channel_id_from_bus(audio::bus::ui)));

		vk::enable_validation_layers = app.config_.enable_validation_layers;
		if(platform::environment_flag_enabled("NSIGHT")){
			vk::enable_validation_layers = false;
		}

		auto app_info = make_application_info(app.config_);
		gui_render_context.emplace(render_context_config{
			.app_info = app_info
		});
		auto& ctx = gui_render_context->context();
		post_command_pool.emplace(
			ctx.get_device(),
			ctx.graphic_family(),
			VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

		auto renderer_create_bundle = gui_render_context->make_renderer_create_info();
		backend::vulkan::renderer renderer{std::move(renderer_create_bundle.create_info)};

		loop.emplace(std::move(renderer), ctx, default_application_loop::functions{
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

		ctx.register_post_resize("xrgui.default_application", [this](
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
			});
			record_context_post_commands(context);
		});
	}

	int run(){
		initialize();

		backend::application_timer timer{backend::application_timer<double>::get_default()};

		loop->wait_term_and_reset();
		auto& ctx = gui_render_context->context();
		while(!ctx.window().should_close()){
			ctx.window().poll_events();
			loop->get_window_dispatcher().drain();

			timer.fetch_time();
			gui::global::event_queue.push_frame_split(timer.global_delta());

			loop->get_window_dispatcher().drain();
			pump_audio_events();
			loop->permit_burst();
			loop->get_scene().get_output_communicate_async_task_queue(0).consume();

			loop->wait_term();
			loop->get_window_dispatcher().drain();

			vk::cmd::submit_command(
				ctx.graphic_queue(),
				loop->get_renderer().get_valid_cmd_buf(),
				loop->get_renderer().get_fence());
			ctx.flush();
			loop->reset_term();
		}

		shutdown();
		return 0;
	}

	builtin::main_loop_init_return_t initialize_scene(default_application_loop& loop){
		auto& ui_root = gui::global::manager;
		auto& resources = ui_root.add_scene_resources(default_scene_name);
		if(audio_system){
			resources.attach_audio_system(*audio_system);
		}
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
			gui_render_context->context().window().get_handle(),
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

	void pump_audio_events(){
		if(!audio_system || scene_ptr == nullptr){
			return;
		}

		audio_system->poll_events([this](const audio::audio_event& event){
			if(event.type == audio::audio_event_type::backend_error){
				log::warn({"Audio"}, "audio backend error");
			}
			if(scene_ptr == nullptr){
				return;
			}
			scene_ptr->get_input_communicate_async_task_queue().post([event](gui::scene& scene){
				scene.resources().audio_resources().consume_audio_event(event);
			});
		});
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

		if(gui_render_context){
			try{
				gui_render_context->wait_on_device();
			} catch(...){
			}
		}

		loop.reset();
		audio_system.reset();
		post_commands.clear();
		post_command_pool.reset();
		gui_render_context.reset();

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
		if(!gui_render_context){
			throw std::logic_error{"default_application context is not initialized"};
		}
		return gui_render_context->context();
	}

	backend::vulkan::renderer& renderer(){
		if(!loop){
			throw std::logic_error{"default_application renderer is not initialized"};
		}
		return loop->get_renderer();
	}

	graphic::image_atlas& image_atlas(){
		if(!gui_render_context){
			throw std::logic_error{"default_application image atlas is not initialized"};
		}
		return gui_render_context->image_atlas();
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

	audio::audio_system& audio(){
		if(!audio_system){
			throw std::logic_error{"default_application audio system is not initialized"};
		}
		return *audio_system;
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

audio::audio_system& default_application::audio(){
	if(!state_){
		throw std::logic_error{"default_application is not running"};
	}
	return state_->audio();
}

}
