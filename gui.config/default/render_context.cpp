module;

#include <vulkan/vulkan.h>

module mo_yanxi.gui.cfg.render_context;

import std;

import mo_yanxi.vk;

import mo_yanxi.graphic.g2d;
import mo_yanxi.gui.global;
import mo_yanxi.gui.assets.manager;
import mo_yanxi.gui.image_regions;
import mo_yanxi.gui.fx.instruction_extension;
import mo_yanxi.gui.cfg.builtin.assets;
import mo_yanxi.gui.cfg.builtin.font_styles;

import mo_yanxi.font;

namespace mo_yanxi::gui::cfg{
namespace{

std::filesystem::path default_shader_spv_path(){
	return std::filesystem::current_path().append("assets/shader/spv").make_preferred();
}

std::filesystem::path default_image_asset_path(){
	return std::filesystem::current_path().append("assets/images").make_preferred();
}

VkApplicationInfo make_default_application_info(const std::string& app_name){
	VkApplicationInfo app_info{
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = app_name.c_str(),
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

std::vector<VkSamplerCreateInfo> make_default_sampler_create_infos(){
	return {vk::preset::ui_texture_sampler};
}

VkSamplerCreateInfo normalize_sampler_create_info(VkSamplerCreateInfo create_info){
	if(create_info.sType == VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO){
		return create_info;
	}

	return vk::preset::ui_texture_sampler;
}

graphic::image_page_sampler_indices make_default_image_page_sampler_indices(
	graphic::sampler_descriptor_index sampler_index){
	return {
		{graphic::image_page_usage::regular, sampler_index},
		{graphic::image_page_usage::normal, sampler_index},
		{graphic::image_page_usage::msdf, sampler_index},
	};
}

void normalize_sampler_create_infos(std::vector<VkSamplerCreateInfo>& create_infos){
	if(create_infos.empty()){
		create_infos = make_default_sampler_create_infos();
	}

	for(auto& create_info : create_infos){
		create_info = normalize_sampler_create_info(create_info);
	}
}

std::vector<graphic::sampler_descriptor_index> register_sampler_vector(
	graphic::image_view_registry& registry,
	const vk::sampler_vector& sampler_vector){
	std::vector<graphic::sampler_descriptor_index> registered_indices{};
	registered_indices.reserve(sampler_vector.size());
	for(const auto sampler : sampler_vector){
		registered_indices.push_back(registry.register_sampler(sampler));
	}
	return registered_indices;
}

graphic::image_page_sampler_indices resolve_image_page_sampler_indices(
	graphic::image_page_sampler_indices configured_indices,
	std::span<const graphic::sampler_descriptor_index> registered_sampler_indices){
	if(configured_indices.empty()){
		if(registered_sampler_indices.empty()){
			return {};
		}
		return make_default_image_page_sampler_indices(registered_sampler_indices.front());
	}

	for(auto& sampler_index : configured_indices | std::views::values){
		if(sampler_index == graphic::auto_sampler_index){
			continue;
		}

		if(sampler_index >= registered_sampler_indices.size()){
			throw std::invalid_argument{
				"render_context image_page_sampler_indices references a missing sampler_create_infos entry"
			};
		}
		sampler_index = registered_sampler_indices[sampler_index];
	}

	return configured_indices;
}

const VkApplicationInfo& prepare_config_for_context(render_context_config& config){
	if(config.app_info.sType == VK_STRUCTURE_TYPE_APPLICATION_INFO){
		config.app_info.pApplicationName = config.app_info.pApplicationName != nullptr
			                                  ? config.app_info.pApplicationName
			                                  : config.app_name.c_str();
	} else{
		config.app_info = make_default_application_info(config.app_name);
	}

	normalize_sampler_create_infos(config.sampler_create_infos);
	return config.app_info;
}

}

[[nodiscard]] renderer_create_info_bundle make_default_renderer_create_info(
	backend::vulkan::context& ctx,
	graphic::image_view_registry& image_view_registry,
	const std::filesystem::path& shader_spv_path){
	renderer_create_info_bundle bundle{};
	auto& shader_modules = bundle.shader_modules;
	shader_modules.reserve(10);

	auto& draw_shader_vert = shader_modules.emplace_back(ctx.get_device(), shader_spv_path / "ui.draw.vert.spv");
	auto& draw_shader_frag_basic = shader_modules.emplace_back(ctx.get_device(), shader_spv_path / "ui.draw.frag_basic.spv");
	auto& draw_shader_frag_outlined = shader_modules.emplace_back(ctx.get_device(), shader_spv_path / "ui.draw.frag_outlined.spv");
	auto& draw_shader_coord = shader_modules.emplace_back(ctx.get_device(), shader_spv_path / "ui.draw.coord_draw.spv");
	auto& draw_shader_mask = shader_modules.emplace_back(ctx.get_device(), shader_spv_path / "ui.draw.frag_mask.spv");
	auto& draw_shader_mask_apply = shader_modules.emplace_back(ctx.get_device(), shader_spv_path / "ui.draw.frag_mask_apply.spv");

	auto& blit_shader_merge = shader_modules.emplace_back(ctx.get_device(), shader_spv_path / "ui.blit.basic.spv");
	auto& blit_shader_blend = shader_modules.emplace_back(ctx.get_device(), shader_spv_path / "ui.blit.alpha_blend.spv");
	auto& blit_shader_inverse = shader_modules.emplace_back(ctx.get_device(), shader_spv_path / "ui.blit.inverse.spv");

	auto& shader_instr_resolve = shader_modules.emplace_back(ctx.get_device(), shader_spv_path / "ui.instruction_resolve_comp.spv");

	using namespace backend::vulkan;
	bundle.create_info = renderer_create_info{
		.allocator_usage = ctx.get_allocator(),
		.command_queue_family = ctx.graphic_family(),
		.image_view_registry = &image_view_registry,
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
				attachment_config{VK_FORMAT_R16_UINT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL},
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
								{4, 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
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
	};
	return bundle;
}

render_context::render_context(render_context_config config)
	: config_(std::move(config)),
	  ctx_(prepare_config_for_context(config_)),
	  rich_text_table_{
		  ([this]{
			  vk::load_ext(ctx_.get_instance());
			  vk::register_default_requirements(ctx_.get_device(), ctx_.get_physical_device());
		  }(), typesetting::rich_text_look_up_table{})
	  },
	  sampler_vector_(ctx_.get_device(), config_.sampler_create_infos),
	  atlas_({
		  .ctx_info = ctx_,
		  .graphic_family_index = ctx_.graphic_family(),
		  .loader_working_queue = ctx_.get_device().graphic_queue(1),
		  .image_view_registry = std::addressof(image_view_registry_),
		  .page_sampler_indices = [this]{
			  auto registered_sampler_indices = register_sampler_vector(image_view_registry_, sampler_vector_);
			  return resolve_image_page_sampler_indices(
				  std::move(config_.image_page_sampler_indices),
				  registered_sampler_indices);
		  }()
	  }){
	try{
		initialize();
	} catch(...){
		shutdown();
		throw;
	}
}

render_context::~render_context(){
	shutdown();
}

void render_context::initialize(){
	typesetting::look_up_table = &rich_text_table_;

	if(config_.initialize_gui_globals){
		gui::global::initialize();
		gui_initialized_ = true;
		gui::global::initialize_assets_manager(gui::global::manager.get_arena_id());
		assets_manager_initialized_ = true;
	}

	builtin::init_font_manager(fonts_, atlas_);

	if(config_.load_default_assets){
		load_default_assets();
	}
}

void render_context::load_default_assets(){
	auto& logo_page = atlas_.create_image_page("tex.logo", {
		.extent = {1920, 1080},
		.format = VK_FORMAT_R8G8B8A8_SRGB,
		.margin = 0,
		.usage = graphic::image_page_usage::regular
	});

	const auto image_path = config_.image_asset_path.empty()
		                        ? default_image_asset_path()
		                        : config_.image_asset_path;
	auto rst = logo_page.register_named_region("logo", graphic::image_load_description{
		graphic::bitmap_path_load{(image_path / "logo.png").string()}
	}, true);

	gui::assets::builtin::get_page().insert(
		gui::assets::builtin::shape_id::logo,
		gui::constant_image_region_borrow{rst.region});

	builtin::generate_default_shapes(atlas_);
	builtin::load_default_icons(atlas_);
	generated_shapes_initialized_ = true;
}

backend::vulkan::context& render_context::context(){
	return ctx_;
}

renderer_create_info_bundle render_context::make_renderer_create_info(){
	auto& ctx = context();
	auto& registry = image_view_registry();
	const auto shader_path = config_.shader_spv_path.empty()
		                         ? default_shader_spv_path()
		                         : config_.shader_spv_path;
	auto bundle = make_default_renderer_create_info(ctx, registry, shader_path);
	if(config_.configure_renderer_create_info){
		config_.configure_renderer_create_info(bundle.create_info);
	}
	if(bundle.create_info.image_view_registry == nullptr){
		throw std::invalid_argument{"renderer_create_info requires a non-null image view registry"};
	}
	if(bundle.create_info.image_view_registry != std::addressof(registry)){
		throw std::invalid_argument{
			"render_context renderer_create_info must use the render_context image view registry"
		};
	}
	return bundle;
}

graphic::image_view_registry& render_context::image_view_registry(){
	return image_view_registry_;
}

const graphic::image_view_registry& render_context::image_view_registry() const{
	return image_view_registry_;
}

graphic::image_atlas& render_context::image_atlas(){
	return atlas_;
}

font::font_manager& render_context::font_manager(){
	return fonts_;
}

void render_context::wait_on_device(){
	context().wait_on_device();
}

void render_context::shutdown() noexcept{
	if(shutdown_done_){
		return;
	}
	shutdown_done_ = true;

	if(generated_shapes_initialized_){
		builtin::dispose_generated_shapes();
		generated_shapes_initialized_ = false;
	}

	atlas_.request_stop();

	try{
		ctx_.wait_on_device();
	} catch(...){
	}

	if(assets_manager_initialized_){
		// The global assets manager stores borrowed image regions owned by atlas_.
		global::terminate_assets_manager();
		assets_manager_initialized_ = false;
	}

	font::default_font_manager = nullptr;
	typesetting::look_up_table = nullptr;

	if(gui_initialized_){
		global::terminate();
		gui_initialized_ = false;
	}
}

}
