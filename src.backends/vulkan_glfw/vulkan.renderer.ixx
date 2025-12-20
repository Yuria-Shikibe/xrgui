module;

#include <cassert>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include "gch/small_vector.hpp"

export module mo_yanxi.backend.vulkan.renderer;

export import mo_yanxi.gui.renderer.frontend;

import mo_yanxi.math.rect_ortho;
import mo_yanxi.math.vector2;
import mo_yanxi.math.matrix3;

export import mo_yanxi.graphic.draw.instruction.batch;
import mo_yanxi.graphic.draw.instruction;

import mo_yanxi.vk.util.uniform;
import mo_yanxi.vk.util;
import mo_yanxi.vk.cmd;
import mo_yanxi.vk;

import mo_yanxi.gui.alloc;

import std;

namespace mo_yanxi::backend::vulkan{
using namespace gui;

constexpr VkPipelineColorBlendAttachmentState get_blending(blending_type type) noexcept{
	switch(type){
	case blending_type::alpha: return vk::blending::alpha_blend;
	case blending_type::add: return vk::blending::add;
	case blending_type::reverse: return vk::blending::reverse;
	case blending_type::lock_alpha: return vk::blending::lock_alpha;
	default: std::unreachable();
	}
}

//TODO adopt heap allocator from scene?

#pragma region DescriptorCustomize

export
using user_data_table = graphic::draw::user_data_index_table<>;

export
using descriptor_slots = graphic::draw::instruction::batch_descriptor_slots;

export
struct descriptor_create_config{
	user_data_table user_data_table{};
	vk::descriptor_layout_builder builder{};
};


#pragma endregion

#pragma region PipelineCustomize

export
struct attachment_config{
	VkFormat format;
	VkImageUsageFlags usage;
};

export
struct draw_attachment_config{
	attachment_config attachment{};
	std::array<blending_type, std::to_underlying(blending_type::SIZE)> swizzle{[]{
		std::array<blending_type, std::to_underlying(blending_type::SIZE)> arr;
		arr.fill(blending_type::SIZE);
		return arr;
	}()};

};

export struct draw_attachment_create_info{
	mr::vector<draw_attachment_config> attachments;
	//TODO ?
	// attachment_create_info depth_stencil_attachment;

	//TODO MSAA?
	VkSampleCountFlagBits multisample{VK_SAMPLE_COUNT_1_BIT};

	bool enables_multisample() const noexcept{
		return multisample != VK_SAMPLE_COUNT_1_BIT;
	}

};

export struct blit_attachment_create_info{
	mr::vector<attachment_config> attachments;
};

export
struct descriptor_use_entry{
	std::uint32_t source;
	std::uint32_t target;
};

export
struct pipeline_data{
	vk::pipeline_layout pipeline_layout{};
	vk::pipeline pipeline{};

	gch::small_vector<descriptor_use_entry, 4, mr::unvs_allocator<descriptor_use_entry>> used_descriptor_sets{};

	[[nodiscard]] pipeline_data() = default;
};

export
struct draw_pipeline_data : pipeline_data{
	bool enables_multisample{};
};

export struct pipeline_configurator{
	std::vector<VkPipelineShaderStageCreateInfo> shader_modules;
	std::vector<descriptor_use_entry> entries;
};


export struct draw_pipeline_config{
	pipeline_configurator config{};
	bool enables_multisample{};
	void(*creator)(
		draw_pipeline_data&,
		const draw_pipeline_config&,
		const draw_attachment_create_info&
	);
};

#pragma endregion

#pragma region BlitUtil

struct indirect_dispatcher{
	using value_type = VkDispatchIndirectCommand;

private:
	vk::buffer buffer{};
	value_type current{};

public:
	[[nodiscard]] indirect_dispatcher() = default;

	[[nodiscard]] indirect_dispatcher(
		vk::allocator_usage allocator,
		const std::size_t chunk_count)
		: buffer(allocator, VkBufferCreateInfo{
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = sizeof(value_type) * chunk_count,
			.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
		}, {
			.usage = VMA_MEMORY_USAGE_CPU_TO_GPU,
		})
	{}

	void set(math::u32size2 extent, math::u32size2 group_extent = {16, 16}) noexcept{
		//Ceil Div
		extent.add(group_extent.copy().sub(1u, 1u)).div(group_extent);

		current = value_type{
			.x = extent.x,
			.y = extent.y,
			.z = 1
		};
	}

	void set_divided(math::u32size2 workgroup_extent) noexcept{
		current = value_type{
			.x = workgroup_extent.x,
			.y = workgroup_extent.y,
			.z = 1
		};
	}

	void update(std::size_t index){
		assert(index < buffer.get_size() / sizeof(value_type));

		(void)vk::buffer_mapper{buffer}.load(
				current, sizeof(value_type) * index
			);
	}

	[[nodiscard]] VkDeviceSize offset_at(std::size_t index) const noexcept{
		assert(index < buffer.get_size() / sizeof(value_type));
		return index * sizeof(value_type);
	}

	explicit(false) operator VkBuffer() const noexcept{
		return buffer;
	}
};

struct blit_resources{
	std::span<const vk::combined_image> inputs;
	std::span<const vk::combined_image> outputs;
};


struct blitter{
	struct ui_blit_info{
		math::usize2 offset{};
		math::usize2 cap{};

		friend bool operator==(const ui_blit_info& lhs, const ui_blit_info& rhs) noexcept = default;
	};

private:
	user_data_table user_data_table_{};
	std::size_t current_blit_chunk_index{};
	vk::command_chunk<mr::unvs_allocator<VkCommandBuffer>> blit_command_chunk{};

	mr::vector<ui_blit_info> blit_infos{};
	indirect_dispatcher dispatcher{};

	descriptor_slots main_resource{};
	mr::vector<descriptor_slots> custom_resources{};

	vk::uniform_buffer uniform_buffer_{};

	mr::vector<pipeline_data> pipelines_{};

public:
	[[nodiscard]] blitter() = default;

	[[nodiscard]] blitter(
		const vk::allocator_usage allocator,
		VkCommandPool command_pool,
		const std::uint32_t chunk_count,
		const std::uint32_t io_attachments_count,
		std::span<const pipeline_configurator> pipeline_creators,
		std::span<const descriptor_create_config> create_configs = {}
		) :
	blit_command_chunk(allocator.get_device(), command_pool, chunk_count)
	, blit_infos(chunk_count)
	, dispatcher(allocator, chunk_count)
	, main_resource{
		allocator,
		[&](){
			vk::descriptor_layout_builder builder;
			builder.push_seq(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);

			for(unsigned i = 0; i < io_attachments_count; ++i){
				builder.push_seq(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT);
			}
			return builder;
		}(),
		chunk_count
	}
	, custom_resources(create_configs.size())
	, uniform_buffer_(allocator, sizeof(ui_blit_info) * chunk_count)
	, pipelines_(pipeline_creators.size())
	{
		{
			mr::vector<VkDescriptorSetLayout> layouts;

			for (const auto & [pipe, creator] : std::views::zip(pipelines_, pipeline_creators)){
				pipe.pipeline_layout = {};

				layouts.clear();
				layouts.push_back(main_resource.descriptor_set_layout());

				std::ranges::copy(creator.entries, std::back_inserter(pipe.used_descriptor_sets));

				for (auto used_descriptor_set : pipe.used_descriptor_sets){
					layouts.push_back(custom_resources.at(used_descriptor_set.source).descriptor_set_layout());
				}

				pipe.pipeline_layout = vk::pipeline_layout(allocator.get_device(), 0, layouts);

				pipe.pipeline = vk::pipeline{
					allocator.get_device(), pipe.pipeline_layout,
					VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
					creator.shader_modules.front()};
			}
		}

		for (const auto& [idx, cfg] : create_configs | std::views::enumerate){
			assert(!cfg.user_data_table.empty());
			user_data_table_.append(cfg.user_data_table);
			custom_resources[idx] = descriptor_slots{
				allocator, cfg.builder, chunk_count
			};

		}

		const auto ubo_chunk_size = sizeof(ui_blit_info) + user_data_table_.required_capacity();
		uniform_buffer_ = vk::uniform_buffer(allocator, ubo_chunk_size * chunk_count);

		{
			const auto map = get_mapper();
			for(std::size_t i = 0; i < main_resource.dbo().get_chunk_count(); ++i){
				(void)map.set_uniform_buffer(0,
					uniform_buffer_.get_address() + ubo_chunk_size * i,
					sizeof(ui_blit_info),
					i
				);
			}
		}

		using namespace graphic::draw;
		using namespace graphic::draw::instruction;
		for(auto&& [group, chunk] :
		    user_data_table_.get_entries()
		    | std::views::chunk_by([](const user_data_identity_entry& l, const user_data_identity_entry& r){
			    return l.entry.group_index == r.entry.group_index;
		    })
		    | std::views::enumerate){
			custom_resources[group].bind([&](const std::uint32_t idx, const vk::descriptor_mapper& mapper){
				for(auto&& [binding, entry] : chunk | std::views::enumerate){
					(void)mapper.set_uniform_buffer(
						binding,
						sizeof(ui_blit_info) + uniform_buffer_.get_address() + idx * ubo_chunk_size + entry.entry.
						global_offset,
						entry.entry.size,
						idx
					);
				}
			});
		}

	}

	VkCommandPool get_command_pool() const noexcept{
		return blit_command_chunk.get_pool();
	}

	auto get_to_wait(VkPipelineStageFlags2 flags2) const noexcept{
		return blit_command_chunk.get_waiting_semaphores(flags2);
	}

	[[nodiscard]] vk::descriptor_mapper get_mapper() {
		return main_resource.get_mapper();
	}

	void update(const blit_resources& resources){
		{
			const auto map = get_mapper();
			for(std::size_t i = 0; i < main_resource.dbo().get_chunk_count(); ++i){
				for (const auto & [idx, input] : resources.inputs | std::views::enumerate){
					map.set_storage_image(1 + idx, input.get_image_view(), VK_IMAGE_LAYOUT_GENERAL, i);
				}

				for (const auto & [idx, input] : resources.outputs | std::views::enumerate){
					map.set_storage_image(1 + idx + resources.inputs.size(), input.get_image_view(), VK_IMAGE_LAYOUT_GENERAL, i);
				}
			}
		}

		create_command(resources);
	}

	[[nodiscard]] auto& blit(math::rect_ortho_trivial<unsigned> region){
		auto& cur = blit_command_chunk[current_blit_chunk_index];

		static constexpr math::usize2 wg_unit_size{16, 16};
		const auto wg_size = region.extent.copy().add(wg_unit_size.copy().sub(1u, 1u)).div(wg_unit_size);
		const auto [rx, ry] = (wg_size * wg_unit_size - region.extent) / 2;
		if(region.src.x > rx)region.src.x -= rx;
		if(region.src.y > ry)region.src.y -= ry;

		if(blit_infos[current_blit_chunk_index].offset != region.src){
			const ui_blit_info info{region.src};
			blit_infos[current_blit_chunk_index] = info;

			cur.wait(blit_command_chunk.get_device());
			(void)vk::buffer_mapper{uniform_buffer_}.load(info, sizeof(ui_blit_info) * current_blit_chunk_index);
		}else{
			cur.wait(blit_command_chunk.get_device());
		}

		dispatcher.set_divided(wg_size);
		dispatcher.update(current_blit_chunk_index);
		current_blit_chunk_index = (current_blit_chunk_index + 1) % blit_command_chunk.size();

		return cur;
	}

private:
	void create_command(const blit_resources& resources){
		vk::cmd::dependency_gen dependency_gen{};

		const graphic::draw::descriptor_buffer_usage main_dbo_info{
			main_resource.dbo(),
			main_resource.dbo().get_chunk_size(),
			0
		};

		graphic::draw::record_context recoreder(mr::unvs_allocator<std::byte>{});


		for(const auto& [pIdx, pipeline] : pipelines_ | std::views::enumerate){
			recoreder.get_bindings().clear();
			recoreder.get_bindings().push_back(main_dbo_info);
			for(const auto& used_descriptor_set : pipeline.used_descriptor_sets){
				auto& dbo = custom_resources.at(used_descriptor_set.source);
				recoreder.get_bindings().push_back({
						dbo.dbo(),
						dbo.dbo().get_chunk_size(),
						used_descriptor_set.target,
					});
			}
			recoreder.prepare_bindings();


			for(auto&& [idx, unit] : blit_command_chunk | std::views::enumerate){
				const vk::scoped_recorder scoped_recorder{unit[pIdx], VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT};

				for(const auto& attachment : resources.inputs){
					dependency_gen.push(
						attachment.get_image(),
						VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
						VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
						VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
						VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
						VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
						VK_IMAGE_LAYOUT_GENERAL,
						vk::image::default_image_subrange
					);
				}

				dependency_gen.apply(scoped_recorder);

				pipeline.pipeline.bind(scoped_recorder, VK_PIPELINE_BIND_POINT_COMPUTE);
				recoreder(pipeline.pipeline_layout, scoped_recorder, idx, VK_PIPELINE_BIND_POINT_COMPUTE);
				vkCmdDispatchIndirect(scoped_recorder, dispatcher, dispatcher.offset_at(idx));

				for(const auto& attachment : resources.inputs){
					dependency_gen.push(
						attachment.get_image(),
						VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
						VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
						VK_PIPELINE_STAGE_2_TRANSFER_BIT,
						VK_ACCESS_2_TRANSFER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL,
						VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						vk::image::default_image_subrange
					);
				}

				dependency_gen.apply(scoped_recorder);

				static constexpr VkClearColorValue clear{};
				for(const auto& attachment : resources.inputs){
					vkCmdClearColorImage(
						scoped_recorder,
						attachment.get_image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						&clear,
						1, &vk::image::default_image_subrange);
				}

				for(const auto& attachment : resources.inputs){
					dependency_gen.push(
						attachment.get_image(),
						VK_PIPELINE_STAGE_2_TRANSFER_BIT,
						VK_ACCESS_2_TRANSFER_WRITE_BIT,
						VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
						VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
						VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
						vk::image::default_image_subrange
					);
				}


				dependency_gen.apply(scoped_recorder);
			}
		}
	}
};
#pragma endregion

export
struct alignas(16) ubo_ui_state{
	float time;
	std::uint32_t _cap[3];
};


export
struct draw_pipeline_create_group{
	std::span<const draw_pipeline_config> pipeline_create_info;
	std::span<const descriptor_create_config> descriptor_create_info;
};


export
struct blit_pipeline_create_group{
	std::span<const pipeline_configurator> pipeline_create_info;
	std::span<const descriptor_create_config> descriptor_create_info;
};

export
struct renderer_create_info{
	vk::allocator_usage allocator;
	std::size_t batch_instruction_capacity;
	VkSampler sampler;
	VkQueue queue;
	VkCommandPool command_pool;
	draw_pipeline_create_group draw_create_info;
	blit_pipeline_create_group blit_create_info;

	draw_attachment_create_info draw_attachment_create_info;
	blit_attachment_create_info blit_attachment_create_info;
};

/**
 * @brief
 * Reserved UBO bindings:
 * set
 * @code
 * [0](Mesh Draw Config)
 * [1](UI Viewport Info)
 * [2](UI State Info)
 * @endcode
 *
 */
export
struct renderer{
	using draw_state_update_handler =
		std::add_pointer_t<graphic::draw::instruction::batch_backend_interface::function_signature_update_state_entry>;

private:
	graphic::draw::instruction::batch batch_{};
	descriptor_slots general_descriptors_{};

	//TODO support constant layout
	mr::vector<descriptor_slots> custom_descriptors_{};

	vk::uniform_buffer uniform_buffer_{};

	std::array<draw_pipeline_data, std::to_underlying(draw_mode::COUNT_or_fallback)> pipeline_slots{};
	vk::command_seq<mr::unvs_allocator<VkCommandBuffer>> command_slots{};

	vk::uniform_buffer ui_state_uniform_buffer_{};
	vk::descriptor_layout ui_state_descriptor_layout_{};
	vk::descriptor_buffer ui_state_descriptor_buffer_{};

	//screen space to uniform space viewport
	mr::vector<layer_viewport> viewports{};
	math::mat3 uniform_proj{};

	draw_attachment_create_info draw_attachment_create_info_{};
	blit_attachment_create_info blit_attachment_create_info_{};

	mr::vector<vk::combined_image> attachments_{};
	mr::vector<vk::combined_image> attachments_multisamples_{};
	vk::command_buffer attachments_multisamples_clean_cmd_{};
	//Blit State
	blitter blitter_{};

	//Draw Mode State
	mr::vector<draw_mode_param> draw_mode_history_{};
	draw_mode_param current_draw_mode_{};

	//Cache
	mr::vector<VkCommandBufferSubmitInfo> cache_command_buffer_submit_info_{};
	mr::vector<VkSubmitInfo2> cache_submit_info_{};

	VkQueue queue_;


public:
	// std::array<draw_state_update_handler, std::to_underlying(state_type::reserved_count)> state_handlers{
	// 	+[](void* host, std::span<const std::byte> payload){
	// 		auto& r = *static_cast<struct renderer*>(host);
	// 		r.process_blit_(payload);
	// 	},
	// 	+[](void* host, std::span<const std::byte> payload){
	// 		auto& r = *static_cast<struct renderer*>(host);
	// 		r.process_draw_mode_(payload);
	// 	},
	//
	// };

	[[nodiscard]] renderer() = default;

	[[nodiscard]] explicit(false) renderer(
		renderer_create_info&& create_info
	)
	: batch_(create_info.allocator, create_info.sampler, [&]{
		user_data_table base{std::in_place_type<gui_reserved_user_data_tuple>};
		for (const auto & tb : create_info.draw_create_info.descriptor_create_info){
			base.append(tb.user_data_table);
		}
		return base;
	}())
	, general_descriptors_(batch_.get_allocator(), [](vk::descriptor_layout_builder& b){
		b.push_seq(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);
		b.push_seq(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);
	}, batch_.work_group_count())
	, command_slots{batch_.get_device(), create_info.command_pool,
		pipeline_slots.size()
		* std::to_underlying(gui::blending_type::SIZE)
		* batch_.work_group_count()
	}
	, ui_state_uniform_buffer_(batch_.get_allocator(), sizeof(ubo_ui_state))
	, ui_state_descriptor_layout_(batch_.get_device(), VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT, [](vk::descriptor_layout_builder& b){
		b.push_seq(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT);
	})
	, ui_state_descriptor_buffer_(batch_.get_allocator(), ui_state_descriptor_layout_, ui_state_descriptor_layout_.binding_count())
	, draw_attachment_create_info_(std::move(create_info.draw_attachment_create_info))
	, blit_attachment_create_info_(std::move(create_info.blit_attachment_create_info))
	, attachments_(draw_attachment_create_info_.attachments.size() + blit_attachment_create_info_.attachments.size())
	, attachments_multisamples_(draw_attachment_create_info_.enables_multisample() ? draw_attachment_create_info_.attachments.size() : 0)
	, blitter_{create_info.allocator, create_info.command_pool, 2, 4, create_info.blit_create_info.pipeline_create_info, create_info.blit_create_info.descriptor_create_info}
	, queue_(create_info.queue)
	{
		if(draw_attachment_create_info_.enables_multisample()){
			attachments_multisamples_clean_cmd_ = vk::command_buffer{batch_.get_device(), blitter_.get_command_pool()};
		}

		(void)vk::descriptor_mapper{ui_state_descriptor_buffer_}.set_uniform_buffer(0, ui_state_uniform_buffer_);

		cache_command_buffer_submit_info_.reserve(batch_.work_group_count());
		batch_.set_submit_callback([this](const graphic::draw::instruction::batch::command_acquire_config& config){
			using namespace graphic::draw::instruction;
			if(config.data_entries){
				vk::buffer_mapper mapper{uniform_buffer_};
				for(const auto& entry : config.data_entries.entries){
					const auto data_span = entry.to_range(config.data_entries.base_address);

					for(const unsigned idx : config.group_indices){
						(void)mapper.load_range(data_span,
							entry.global_offset + idx * batch_.get_ubo_table().required_capacity());
					}
				}
			}

			for(const auto [i, group_idx] : config.group_indices | std::views::enumerate){
				auto& cmd_ref = cache_command_buffer_submit_info_.emplace_back(VkCommandBufferSubmitInfo{
					.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
					.commandBuffer = get_cmd_matrix()[
						std::to_underlying(current_draw_mode_.mode),
						std::to_underlying(current_draw_mode_.blending),
						group_idx],
				});

				cache_submit_info_.push_back({
					.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
					.commandBufferInfoCount = 1,
					.pCommandBufferInfos = &cmd_ref,
					.signalSemaphoreInfoCount = 2,
					.pSignalSemaphoreInfos = config.group_semaphores.data() + i * 2
				});
			}

			if(!cache_submit_info_.empty()){
				vkQueueSubmit2(queue_, cache_submit_info_.size(), cache_submit_info_.data(), nullptr);

				cache_command_buffer_submit_info_.clear();
				cache_submit_info_.clear();
			}

		});

		custom_descriptors_.reserve(create_info.draw_create_info.descriptor_create_info.size());
		for (const auto & user_data_index_table : create_info.draw_create_info.descriptor_create_info){
			custom_descriptors_.push_back(graphic::draw::instruction::batch_descriptor_slots(
				batch_.get_allocator(), user_data_index_table.builder, batch_.work_group_count()));
		}

		init_pipeline(create_info.draw_create_info.pipeline_create_info);
		update_ubo_();

		draw_mode_history_.reserve(16);
		draw_mode_history_.push_back(current_draw_mode_);
	}

	void resize(VkExtent2D extent){
		{
			const auto [ox, oy] = attachments_.front().get_image().get_extent2();
			if(extent.width == ox && extent.height == oy)return;
		}

		static constexpr auto get_view_create_info = [](VkFormat format){
			return VkImageViewCreateInfo{
					.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
					.viewType = VK_IMAGE_VIEW_TYPE_2D,
					.format = format,
					.subresourceRange = vk::image::default_image_subrange
				};
		};

		const auto sz = draw_attachment_create_info_.attachments.size();
		vk::cmd::dependency_gen dep{};
		dep.image_memory_barriers.resize(attachments_.size());

		for (const auto & [idx, cfg] : draw_attachment_create_info_.attachments | std::views::enumerate){
			attachments_[idx] = vk::combined_image{
				vk::image{
					batch_.get_allocator(),
					{extent.width, extent.height, 1},
					VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | cfg.attachment.usage,
					cfg.attachment.format
				},
				get_view_create_info(cfg.attachment.format)
			};
			dep.image_memory_barriers[idx] = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
				.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
				.srcAccessMask = VK_ACCESS_2_NONE,
				.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				.image = attachments_[idx].get_image(),
				.subresourceRange = vk::image::default_image_subrange
			};
		}

		for(const auto& [idx, cfg] : blit_attachment_create_info_.attachments | std::views::enumerate){
			attachments_[idx + sz] = vk::combined_image{
					vk::image{
						batch_.get_allocator(),
						{extent.width, extent.height, 1},
						VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | cfg.usage,
						cfg.format
					},
					get_view_create_info(cfg.format)
				};
			dep.image_memory_barriers[idx + sz] = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
				.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
				.srcAccessMask = VK_ACCESS_2_NONE,
				.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_GENERAL,
				.image = attachments_[idx + sz].get_image(),
				.subresourceRange = vk::image::default_image_subrange
			};
		}


		if(draw_attachment_create_info_.enables_multisample())for (const auto & [idx, cfg] : draw_attachment_create_info_.attachments | std::views::enumerate){
			attachments_multisamples_[idx] = vk::combined_image{
				vk::image{
					batch_.get_allocator(),
					{extent.width, extent.height, 1},
					VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
					cfg.attachment.format,
					1, 1, draw_attachment_create_info_.multisample
				},
				get_view_create_info(cfg.attachment.format)
			};
			dep.push(attachments_multisamples_[idx].get_image(),
				VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
				VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				vk::image::default_image_subrange

			);
		}


		{
			auto transient = vk::transient_command{batch_.get_device(), blitter_.get_command_pool(), queue_};
			dep.apply(transient);
		}

		blitter_.update(blit_resources(
			std::span{attachments_.data(), sz},
			std::span{attachments_.begin() + sz, attachments_.end()}
			));

		record_multisample_attachment_clean_cmd_();
		record_command();
	}


private:
	std::mdspan<const VkCommandBuffer, std::dextents<std::size_t, 3>> get_cmd_matrix() const noexcept {
		return std::mdspan{
			command_slots.data(),
			pipeline_slots.size(),
			std::to_underlying(gui::blending_type::SIZE),
			batch_.work_group_count()};
	}

	vk::dynamic_rendering get_dynamic_rendering(bool multiSample) const{
		if(multiSample){
			vk::dynamic_rendering dr{};

			for (const auto & [attac, multi] : std::views::zip(attachments_, attachments_multisamples_)){
				dr.push_color_attachment(
					multi.get_image_view(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
					attac.get_image_view(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
					);
			}

			return dr;
		}else{
			return vk::dynamic_rendering{
				attachments_
				| std::views::take(draw_attachment_create_info_.attachments.size())
				| std::views::transform(&vk::combined_image::get_image_view),
				nullptr
			};
		}
	}

	[[nodiscard]] VkRect2D get_screen_area() const noexcept{
		return {{}, attachments_.front().get_image().get_extent2()};
	}

	void init_pipeline(std::span<const draw_pipeline_config> creators){
		mr::vector<VkDescriptorSetLayout> layouts;
		for (const auto & [pipe, creator] : std::views::zip(pipeline_slots, creators)){
			layouts.clear();
			layouts.push_back(batch_.get_batch_descriptor_layout());
			layouts.push_back(general_descriptors_.descriptor_set_layout());
			layouts.push_back(ui_state_descriptor_layout_);

			std::ranges::copy(creator.config.entries, std::back_inserter(pipe.used_descriptor_sets));

			for (auto used_descriptor_set : pipe.used_descriptor_sets){
				layouts.push_back(custom_descriptors_.at(used_descriptor_set.source).descriptor_set_layout());
			}

			pipe.pipeline_layout = vk::pipeline_layout(
				batch_.get_device(), 0, layouts);

			creator.creator(pipe, creator, draw_attachment_create_info_);
		}
	}

public:
	void update_state(const ubo_ui_state& state){
		(void)vk::buffer_mapper{ui_state_uniform_buffer_}.load(state);
	}

	void wait_idle(){
		batch_.consume_all();
		batch_.wait_all();
		assert(batch_.is_all_done());
	}

#pragma region Getter
	std::span<const vk::combined_image> get_blit_attachments() const noexcept{
		return {attachments_.begin() + draw_attachment_create_info_.attachments.size(), attachments_.end()};
	}

	[[nodiscard]] vk::image_handle get_base() const noexcept{
		return get_blit_attachments().front();
	}

	[[nodiscard]] const math::mat3& get_screen_uniform_proj() const noexcept{
		return uniform_proj;
	}
#pragma endregion

	renderer_frontend create_frontend() noexcept {
		using namespace graphic::draw::instruction;
		return renderer_frontend{
				// batch_.get_ubo_table(), batch_backend_interface{
				// 	*this,
				// 	[](renderer& b, std::size_t size) static{
				// 		return b.batch_.acquire(size);
				// 	},
				// 	[](renderer& b) static{
				// 		b.batch_.consume_all();
				// 	},
				// 	[](renderer& b) static{
				// 		b.batch_.wait_all();
				// 	},
				// 	state_handlers
				// }
			};
	}

	auto get_blit_wait_semaphores(VkPipelineStageFlags2 flags2) const{
		return blitter_.get_to_wait(flags2);
	}
private:
	void process_draw_mode_(std::span<const std::byte> payload){
		//TODO lazy submit
		batch_.consume_all();
		draw_mode_param param;
		std::memcpy(&param, payload.data(), payload.size_bytes());
		if(param.mode == draw_mode::COUNT_or_fallback){
			draw_mode_history_.pop_back();
			current_draw_mode_ = draw_mode_history_.back();
		}else{
			draw_mode_history_.push_back(current_draw_mode_);
			current_draw_mode_ = param;
		}
	}

	void process_blit_(
		std::span<const std::byte> data_span
		){
		batch_.consume_all();
		batch_.wait_all();

		auto cfg = *reinterpret_cast<const blit_config*>(data_span.data());

		if(cfg.blit_region.src.x < 0){
			cfg.blit_region.extent.x += cfg.blit_region.src.x;
			cfg.blit_region.src.x = 0;
			if(cfg.blit_region.extent.x < 0)cfg.blit_region.extent.x = 0;
		}
		if(cfg.blit_region.src.y < 0){
			cfg.blit_region.extent.y += cfg.blit_region.src.y;
			cfg.blit_region.src.y = 0;
			if(cfg.blit_region.extent.y < 0)cfg.blit_region.extent.y = 0;
		}
		auto& cmd_unit = blitter_.blit({cfg.blit_region.src.as<unsigned>(), cfg.blit_region.extent.as<unsigned>()});
		//
		auto blit_semaphore_submit_info = cmd_unit.get_next_semaphore_submit_info(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		auto cmd_submit_info = cmd_unit.get_command_submit_info();
		// std::vector<VkSemaphoreSubmitInfo> submit_infos;
		// submit_infos.reserve(batch_.work_group_count());
		// batch_.for_each_submit([&](unsigned, const graphic::draw::instruction::working_group& working_group){
		// 	submit_infos.push_back(working_group.get_waiting_submit_info(1));
		// });

		std::array cmd_submits{cmd_submit_info, VkCommandBufferSubmitInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
			.commandBuffer = attachments_multisamples_clean_cmd_
		}};
		const VkSubmitInfo2 info{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
			// .waitSemaphoreInfoCount = static_cast<std::uint32_t>(submit_infos.size()),
			// .pWaitSemaphoreInfos = submit_infos.data(),
			.commandBufferInfoCount = static_cast<std::uint32_t>(1 + draw_attachment_create_info_.enables_multisample()),
			.pCommandBufferInfos = cmd_submits.data(),
			.signalSemaphoreInfoCount = 1,
			.pSignalSemaphoreInfos = &blit_semaphore_submit_info
		};

		vkQueueSubmit2(queue_, 1, &info, nullptr);

		// batch_.skip_wait();
	}

#pragma region Command
	void record_multisample_attachment_clean_cmd_() const{
		if(draw_attachment_create_info_.enables_multisample()){
			vk::scoped_recorder recorder{attachments_multisamples_clean_cmd_, VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT};
			vk::cmd::dependency_gen dependency_gen{};
			for (const auto & attachments_multisample : attachments_multisamples_){
				dependency_gen.push(
						attachments_multisample.get_image(),
						VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
						VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
						VK_PIPELINE_STAGE_2_TRANSFER_BIT,
						VK_ACCESS_2_TRANSFER_WRITE_BIT,
						VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
						VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						vk::image::default_image_subrange
					);
				dependency_gen.apply(recorder, true);
				dependency_gen.swap_stages();

				constexpr VkClearColorValue clear{};
				vkCmdClearColorImage(
						recorder,
						attachments_multisample.get_image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						&clear,
						1, &vk::image::default_image_subrange);

				dependency_gen.apply(recorder);
			}
		}

	}


	void record_command(){
		vk::dynamic_rendering dynamic_rendering[2]{
				get_dynamic_rendering(false)
			};
		dynamic_rendering[1] = (draw_attachment_create_info_.enables_multisample()
			                        ? get_dynamic_rendering(true)
			                        : vk::dynamic_rendering{dynamic_rendering[1]});


		const graphic::draw::descriptor_buffer_usage dbo_viewport_info{
			general_descriptors_.dbo(),
			general_descriptors_.dbo().get_chunk_size(),
			1
		};

		const graphic::draw::descriptor_buffer_usage dbo_state_info{
			ui_state_descriptor_buffer_, 0, 2
		};

		mr::vector<graphic::draw::descriptor_buffer_usage> dbo_infos{};
		const auto mtx = get_cmd_matrix();

		const mr::vector<VkBool32> colorBlendEnables(draw_attachment_create_info_.attachments.size(), true);
		static constexpr auto transformer = [](const VkPipelineColorBlendAttachmentState& cfg){
			return VkColorBlendEquationEXT{
				.srcColorBlendFactor = cfg.srcColorBlendFactor,
				.dstColorBlendFactor = cfg.dstColorBlendFactor,
				.colorBlendOp = cfg.colorBlendOp,
				.srcAlphaBlendFactor = cfg.srcAlphaBlendFactor,
				.dstAlphaBlendFactor = cfg.dstAlphaBlendFactor,
				.alphaBlendOp = cfg.alphaBlendOp
			};
		};

		mr::vector<VkColorBlendEquationEXT> blendEquation(draw_attachment_create_info_.attachments.size());

		for(unsigned blend_idx = 0; blend_idx < std::to_underlying(blending_type::SIZE); ++blend_idx){
			blendEquation.assign(
				draw_attachment_create_info_.attachments.size(),
				transformer(get_blending(static_cast<blending_type>(blend_idx))));


			blendEquation.append_range(
				draw_attachment_create_info_.attachments
				| std::views::transform([&](const draw_attachment_config& config){
					if(config.swizzle[blend_idx] != blending_type::SIZE){
						return config.swizzle[blend_idx];
					}
					return static_cast<blending_type>(blend_idx);
				})
				| std::views::transform(get_blending)
				| std::views::transform(transformer));



			for(const auto& [idx, pipe] : pipeline_slots | std::views::enumerate){
				dbo_infos.clear();
				dbo_infos.reserve(2 + pipe.used_descriptor_sets.size());
				dbo_infos.push_back(dbo_viewport_info);
				dbo_infos.push_back(dbo_state_info);

				for (const auto & used_descriptor_set : pipe.used_descriptor_sets){
					auto& dbo = custom_descriptors_.at(used_descriptor_set.source);
					dbo_infos.push_back({
						dbo.dbo(),
						dbo.dbo().get_chunk_size(),
						used_descriptor_set.target,
					});
				}

				const auto d1 = &mtx[idx, blend_idx, 0];
				const auto subrange = std::span{d1, batch_.work_group_count()};

				batch_.record_command(
					pipe.pipeline_layout,
					dbo_infos,
					[&] -> std::generator<VkCommandBuffer&&>{
					for(const auto& [idx, buf] : subrange | std::views::enumerate){
						vk::scoped_recorder recorder{buf, VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT};

						dynamic_rendering[draw_attachment_create_info_.enables_multisample()].begin_rendering(recorder, get_screen_area());
						pipe.pipeline.bind(recorder, VK_PIPELINE_BIND_POINT_GRAPHICS);

						vk::cmd::setColorBlendEnableEXT(recorder, 0, colorBlendEnables.size(), colorBlendEnables.data());
						vk::cmd::setColorBlendEquationEXT(recorder, 0, blendEquation.size(), blendEquation.data());

						vk::cmd::set_viewport(recorder, get_screen_area());
						vk::cmd::set_scissor(recorder, get_screen_area());

						co_yield buf;

						vkCmdEndRendering(recorder);
					}
				}());
			}

		}

	}

#pragma endregion

	void update_ubo_(){
		uniform_buffer_ = vk::uniform_buffer{
			batch_.get_allocator(),
			batch_.get_ubo_table().required_capacity() * batch_.work_group_count(),
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT};

		{//Clear
			(void)vk::buffer_mapper{uniform_buffer_}.fill(0);
		}


		using namespace graphic::draw;
		using namespace graphic::draw::instruction;
		general_descriptors_.bind([&](const std::uint32_t idx, const vk::descriptor_mapper& mapper){
			for(auto&& [binding, ientry] : batch_.get_ubo_table().get_entries()
				| std::views::take_while([](const user_data_identity_entry& l){
					return l.entry.group_index == 0;
				})
				| std::views::enumerate){
				auto& entry = ientry.entry;
				(void)mapper.set_uniform_buffer(
					binding,
					uniform_buffer_.get_address() + idx * batch_.get_ubo_table().required_capacity() + entry.global_offset, entry.size,
					idx
				);
			}
		});

		for(auto&& [group, chunk] :
		    batch_.get_ubo_table().get_entries()
		    | std::views::chunk_by([](const user_data_identity_entry& l, const user_data_identity_entry& r){
			    return l.entry.group_index == r.entry.group_index;
		    })
		    | std::views::drop(1)
		    | std::views::enumerate){
			custom_descriptors_[group].bind([&](const std::uint32_t idx, const vk::descriptor_mapper& mapper){
				for(auto&& [binding, entry] : chunk | std::views::enumerate){
					(void)mapper.set_uniform_buffer(
						binding,
						uniform_buffer_.get_address() + idx * batch_.get_ubo_table().required_capacity() +
						entry.entry.global_offset, entry.entry.size, idx
					);
				}
			});
		}

	}
};

}
