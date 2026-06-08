export module mo_yanxi.gui.cfg.default_application;

import std;

export import mo_yanxi.gui.infrastructure;
export import mo_yanxi.gui.elem.group;
export import mo_yanxi.backend.vulkan.context;
export import mo_yanxi.backend.vulkan.renderer;
export import mo_yanxi.graphic.image_atlas;
export import mo_yanxi.input_handle.input_event_queue;
export import mo_yanxi.gui.cfg.render_context;

namespace mo_yanxi::gui::cfg{

export
/**
 * @brief Configuration for the built-in GLFW + Vulkan application wrapper.
 *
 * This wrapper is the recommended first integration path. It owns platform
 * initialization, default renderer/font/image resources, a scene, and the
 * built-in main loop. Use the lower-level backend APIs when embedding XRGUI
 * into an existing renderer or window system.
 */
struct default_application_config{
	/**
	 * @brief Name passed to Vulkan application metadata and the native window.
	 */
	std::string app_name{"XRGUI Application"};

	/**
	 * @brief Executable path used to switch the runtime working directory.
	 *
	 * `run_default_application()` fills this from `argv[0]` when it is empty.
	 * Runtime assets are then loaded relative to the executable directory, which
	 * matches `xmake run` and the target post-build asset copy.
	 */
	std::filesystem::path executable_path{};

	/**
	 * @brief Hide the OS cursor and use XRGUI's cursor rendering instead.
	 */
	bool hide_native_cursor{true};

	/**
	 * @brief Enable Vulkan validation layers unless the `NSIGHT=1` environment
	 * flag disables them for profiling.
	 */
	bool enable_validation_layers{false};
};

export
/**
 * @brief Minimal default application shell for standalone XRGUI programs.
 *
 * The constructor only stores configuration. `run()` performs initialization in
 * order: platform, FreeType, GLFW, global GUI state, default Vulkan context,
 * renderer, image atlas, font manager, scene resources, scene, native
 * communicator, and the UI main loop. Shutdown reverses that ownership order.
 *
 * Derive from this class and implement `build_gui()` to create the root UI once.
 * Per-frame work belongs in `before_frame()` or `after_frame()`. Do not access
 * `context()`, `renderer()`, `image_atlas()`, `scene()`, or
 * `window_dispatcher()` before `run()` has started.
 */
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
	/**
	 * @brief Build the application's element tree.
	 *
	 * Called once on the GUI thread after the default scene and style resources
	 * are ready. Create elements through `scene` and insert them into `root`, or
	 * use a container's `create_back()` / `emplace_back()` helpers.
	 */
	virtual void build_gui(gui::scene& scene, gui::loose_group& root) = 0;

	/**
	 * @brief Called before layout and drawing for each GUI frame.
	 */
	virtual void before_frame(std::chrono::duration<float> delta){}

	/**
	 * @brief Called after drawing commands for each GUI frame are recorded.
	 */
	virtual void after_frame(){}

	/**
	 * @brief Receives input events that no GUI element intercepted.
	 */
	virtual void on_unhandled_input(input_handle::input_event_variant event){}

	/**
	 * @brief Access the owned Vulkan context while the application is running.
	 */
	backend::vulkan::context& context();

	/**
	 * @brief Access the owned default Vulkan renderer while the application is running.
	 */
	backend::vulkan::renderer& renderer();

	/**
	 * @brief Access the owned image atlas while the application is running.
	 */
	graphic::image_atlas& image_atlas();

	/**
	 * @brief Access the main GUI scene while the application is running.
	 */
	gui::scene& scene();

	/**
	 * @brief Access the window-thread dispatcher used for native clipboard,
	 * cursor, and IME requests.
	 */
	gui::window_thread_dispatcher& window_dispatcher();

private:
	struct state;

	std::unique_ptr<state> state_{};
	default_application_config config_;
};

export
/**
 * @brief Construct and run an App derived from `default_application`.
 *
 * If `config.executable_path` is empty, `argv[0]` is stored in the config so the
 * runtime working directory can be moved to the executable directory before
 * loading copied assets.
 */
template <std::derived_from<default_application> App>
int run_default_application(int argc, char** argv, default_application_config config = {}){
	if(config.executable_path.empty() && argc > 0 && argv != nullptr && argv[0] != nullptr){
		config.executable_path = argv[0];
	}

	App app{std::move(config)};
	return app.run();
}

}
