module;

#include <vulkan/vulkan.h>

export module mo_yanxi.pipeline_configure;

import mo_yanxi.vk;
import mo_yanxi.backend.vulkan.renderer;
import mo_yanxi.backend.vulkan.context;
import mo_yanxi.gui.draw_config;
import mo_yanxi.gui.renderer.frontend;

import std;

namespace mo_yanxi::gui{
using gui::draw_config::slide_line_config;

mo_yanxi::vk::sampler sampler_ui{};
mo_yanxi::vk::shader_module ui_basic{};
mo_yanxi::vk::shader_module ui_msdf{};
mo_yanxi::vk::shader_module ui_blit{};

export
auto make_renderer(
	mo_yanxi::backend::vulkan::context& ctx
) -> mo_yanxi::backend::vulkan::renderer{
	using namespace mo_yanxi;
	using namespace mo_yanxi::backend::vulkan;

	std::filesystem::path shader_spv_path = std::filesystem::current_path().append("assets/shader/spv").
	                                                                        make_preferred();
	ui_basic = {ctx.get_device(), shader_spv_path / "ui.draw.basic.spv"};
	ui_msdf = {ctx.get_device(), shader_spv_path / "ui.draw.sdf.spv"};
	ui_blit = {ctx.get_device(), shader_spv_path / "ui.blit.basic.spv"};
	sampler_ui = {ctx.get_device(), vk::preset::ui_texture_sampler};

	ctx.add_dispose([] noexcept{
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
			gtp.set_blending_dynamic(true, false);
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

	using namespace graphic::draw;
	namespace instr = graphic::draw::instruction;
	std::array draw_user_data_index_tables{
			descriptor_create_config{
				user_data_index_table(std::in_place_type<std::tuple<slide_line_config>>),
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
			pipeline_configurator{{ui_blit.get_create_info(VK_SHADER_STAGE_COMPUTE_BIT)}, {}},
		};


	renderer r{
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

	r.create_frontend().push(slide_line_config{});

	return r;
}
}
