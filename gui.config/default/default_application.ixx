export module mo_yanxi.gui.cfg.default_application;

import std;

export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.gui.elem.group;
export import mo_yanxi.backend.vulkan.context;
export import mo_yanxi.backend.vulkan.renderer;
export import mo_yanxi.graphic.image_atlas;
export import mo_yanxi.input_handle.input_event_queue;

namespace mo_yanxi::gui::cfg{

export
struct default_application_config{
	std::string app_name{"XRGUI Application"};
	std::filesystem::path executable_path{};
	bool hide_native_cursor{true};
	bool enable_validation_layers{false};
};

export
class default_application{
public:
	explicit default_application(default_application_config config);
	virtual ~default_application();

	default_application(const default_application&) = delete;
	default_application(default_application&&) = delete;
	default_application& operator=(const default_application&) = delete;
	default_application& operator=(default_application&&) = delete;

	int run();

protected:
	virtual void build_gui(gui::scene& scene, gui::loose_group& root) = 0;
	virtual void before_frame(std::chrono::duration<float> delta){}
	virtual void after_frame(){}
	virtual void on_unhandled_input(input_handle::input_event_variant event){}

	backend::vulkan::context& context();
	backend::vulkan::renderer& renderer();
	graphic::image_atlas& image_atlas();
	gui::scene& scene();
	gui::window_thread_dispatcher& window_dispatcher();

private:
	struct state;

	std::unique_ptr<state> state_{};
	default_application_config config_;
};

export
template <std::derived_from<default_application> App>
int run_default_application(int argc, char** argv, default_application_config config = {}){
	if(config.executable_path.empty() && argc > 0 && argv != nullptr && argv[0] != nullptr){
		config.executable_path = argv[0];
	}

	App app{std::move(config)};
	return app.run();
}

}
