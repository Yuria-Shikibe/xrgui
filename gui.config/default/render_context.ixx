module;

#include <vulkan/vulkan.h>

export module mo_yanxi.gui.cfg.render_context;

import std;

import mo_yanxi.vk;
import mo_yanxi.typesetting.rich_text;

export import mo_yanxi.backend.vulkan.context;
export import mo_yanxi.backend.vulkan.renderer;
export import mo_yanxi.graphic.image_atlas;
export import mo_yanxi.font.manager;

namespace mo_yanxi::gui::cfg{

export
struct render_context_config{
	std::string app_name{"XRGUI Application"};
	VkApplicationInfo app_info{};
	std::filesystem::path shader_spv_path{};
	std::filesystem::path image_asset_path{};
	std::vector<VkSamplerCreateInfo> sampler_create_infos{};
	/**
	 * Uses `graphic::auto_sampler_index` (~0U) for automatic sampler resolution
	 * and `0..N-1` for entries in
	 * `sampler_create_infos`. The render context rewrites these to the actual
	 * image registry sampler descriptor indices after registration.
	 */
	graphic::image_page_sampler_indices image_page_sampler_indices{};
	std::move_only_function<void(backend::vulkan::renderer_create_info&)> configure_renderer_create_info{};
	bool initialize_gui_globals{true};
	bool load_default_assets{true};
};

export
struct renderer_create_info_bundle{
	renderer_create_info_bundle() = default;
	renderer_create_info_bundle(const renderer_create_info_bundle&) = delete;
	renderer_create_info_bundle(renderer_create_info_bundle&&) noexcept = default;
	renderer_create_info_bundle& operator=(const renderer_create_info_bundle&) = delete;
	renderer_create_info_bundle& operator=(renderer_create_info_bundle&&) noexcept = default;

	std::vector<vk::shader_module> shader_modules{};
	backend::vulkan::renderer_create_info create_info{};
};

export
[[nodiscard]] renderer_create_info_bundle make_default_renderer_create_info(
	backend::vulkan::context& ctx,
	graphic::image_view_registry& image_view_registry,
	const std::filesystem::path& shader_spv_path);

export
class render_context{
public:
	explicit render_context(render_context_config config);
	~render_context();

	render_context(const render_context&) = delete;
	render_context(render_context&&) = delete;
	render_context& operator=(const render_context&) = delete;
	render_context& operator=(render_context&&) = delete;

	backend::vulkan::context& context();
	[[nodiscard]] renderer_create_info_bundle make_renderer_create_info();
	graphic::image_view_registry& image_view_registry();
	const graphic::image_view_registry& image_view_registry() const;
	graphic::image_atlas& image_atlas();
	font::font_manager& font_manager();

	void wait_on_device();
	void shutdown() noexcept;

private:
	void initialize();
	void load_default_assets();

	render_context_config config_{};
	backend::vulkan::context ctx_{};
	typesetting::rich_text_look_up_table rich_text_table_{};

	vk::sampler_vector sampler_vector_{};
	graphic::image_view_registry image_view_registry_{};
	graphic::image_atlas atlas_{};
	font::font_manager fonts_{};

	bool gui_initialized_{};
	bool assets_manager_initialized_{};
	bool generated_shapes_initialized_{};
	bool shutdown_done_{};
};

}
