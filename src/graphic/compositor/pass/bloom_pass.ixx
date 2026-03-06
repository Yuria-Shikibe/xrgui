module;

#include <vulkan/vulkan.h>

export module mo_yanxi.graphic.compositor.bloom;

import std;
export import mo_yanxi.graphic.compositor.post_process_pass;
import mo_yanxi.vk;
import mo_yanxi.vk.cmd;
import mo_yanxi.math.vector2;

namespace mo_yanxi::graphic::compositor{
[[nodiscard]] constexpr std::uint32_t reverse_after(std::uint32_t value, std::uint32_t ceil) noexcept{
	if(value < ceil) return value;
	return (ceil * 2 - value - 1);
}

[[nodiscard]] constexpr std::uint32_t reverse_after_bit(std::uint32_t value, std::uint32_t ceil) noexcept{
	if(value < ceil) return value;
	return (ceil * 2 - value - 1) | ~(~0u >> 1);
}

[[nodiscard]] constexpr std::uint32_t get_real_mip_level(std::uint32_t expected, math::u32size2 extent) noexcept{
	return std::min(expected, vk::get_recommended_mip_level(extent.x, extent.y));
}

struct bloom_defines {
	math::vector2<std::uint32_t> currentLayerExtent;
	std::uint32_t current_layer;
	std::uint32_t up_scaling;
	std::uint32_t total_layer;
	std::uint32_t target_scale; // 新增字段，保持和 shader 的 PushConstant 对齐
};

struct bloom_uniform_block{
	float scale;
	float strength_src;
	float strength_dst;

	/**
	 * @brief 0(Additive) - 1(Screen)
	 */
	float mix_factor;
};

export
struct bloom_pass final : post_process_stage{
	using post_process_stage::post_process_stage;

	void set_scale(const float scale){
		if(current_param.scale != scale){
			current_param.scale = scale;
			(void)vk::buffer_mapper{ubo()}.load(current_param);
		}
	}

	void set_mix_factor(const float factor){
		if(current_param.mix_factor != factor){
			current_param.mix_factor = factor;
			(void)vk::buffer_mapper{ubo()}.load(current_param);
		}
	}

	void set_strength(const float strengthSrc = 1.f, const float strengthDst = 1.f){
		if(current_param.strength_src != strengthSrc || current_param.strength_dst != strengthDst){
			current_param.strength_src = strengthSrc;
			current_param.strength_dst = strengthDst;
			(void)vk::buffer_mapper{ubo()}.load(current_param);
		}
	}

	void set_strength_src(const float strengthSrc = 1.f){
		if(current_param.strength_src != strengthSrc){
			current_param.strength_src = strengthSrc;
			(void)vk::buffer_mapper{ubo()}.load(current_param);
		}
	}

	void set_strength_dst(const float strengthDst = 1.f){
		if(current_param.strength_dst != strengthDst){
			current_param.strength_dst = strengthDst;
			(void)vk::buffer_mapper{ubo()}.load(current_param);
		}
	}

	void set_max_mip_level(std::uint32_t max_level){
		max_mip_level_ = max_level;
	}

private:
	bloom_uniform_block current_param{1, 1, 1, 0.5f};

	std::uint32_t total_mip_level_{0};
	std::uint32_t max_mip_level_{7};
	std::vector<vk::image_view> down_mipmap_image_views{};
	std::vector<vk::image_view> up_mipmap_image_views{};

	[[nodiscard]] std::uint32_t get_required_mipmap_level() const noexcept{
		return sockets().at_out<image_requirement>(0).mip_level;
	}

	[[nodiscard]] std::uint32_t get_real_mipmap_level(const math::u32size2 extent) const noexcept{
		return get_real_mip_level(get_required_mipmap_level(), extent);
	}

private:
	void post_init(const vk::allocator_usage& alloc, const math::u32size2 extent) override{
		init_pipeline(alloc, {
			VkPushConstantRange{
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.offset = 0,
				.size = sizeof(bloom_defines)
			}});
	}

	void reset_resources(const vk::allocator_usage& alloc, const pass_data& pass, const math::u32size2 extent) override{
		int raw_scale = get_target_scale();
		if (raw_scale < 0) {
			throw std::invalid_argument("Bloom target scale cannot be less than 0");
		}

		total_mip_level_ = std::min(max_mip_level_, get_real_mipmap_level(extent));

		// 限制 target_scale 不能大于等于 total_mip_level_，以保证至少有一次上采样操作写入 up_sample_image
		std::uint32_t target_scale = std::min(static_cast<std::uint32_t>(raw_scale),
		                                      total_mip_level_ > 0 ? total_mip_level_ - 1 : 0u);
		std::uint32_t total_passes = total_mip_level_ * 2 - target_scale;

		uniform_buffer_ = {alloc, sizeof(bloom_uniform_block)};
		reset_descriptor_buffer(alloc, total_passes);

		vk::buffer_mapper ubo_mapper{uniform_buffer_};
		(void)ubo_mapper.load(current_param);

		{
			vk::descriptor_mapper info{descriptor_buffer_};
			for(std::uint32_t i = 0; i < total_passes; ++i){
				(void)info.set_uniform_buffer(
					4,
					uniform_buffer_.get_address(), sizeof(bloom_uniform_block), i);
			}
		}

		const auto down_sample_image = std::get<image_entity>(pass.get_used_resources().get_in(1).resource);
		const auto up_sample_image = std::get<image_entity>(pass.get_used_resources().get_out(0).resource);

		// 从对应的 slot 需求中动态获取格式
		const auto down_format = sockets().at_in<image_requirement>(1).get_format();
		const auto up_format = sockets().at_out<image_requirement>(0).get_format();

		down_mipmap_image_views.resize(total_mip_level_);
		up_mipmap_image_views.resize(total_mip_level_ - target_scale);

		for(auto&& [idx, view] : down_mipmap_image_views | std::views::enumerate){
			view = vk::image_view(
				alloc.get_device(), {
					.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
					.pNext = nullptr,
					.flags = 0,
					.image = down_sample_image.handle.image,
					.viewType = VK_IMAGE_VIEW_TYPE_2D,
					.format = down_format, // 使用动态推断的格式
					.components = {},
					.subresourceRange = VkImageSubresourceRange{
						VK_IMAGE_ASPECT_COLOR_BIT, static_cast<std::uint32_t>(idx), 1, 0, 1
					}
				}
			);
		}

		for(auto&& [idx, view] : up_mipmap_image_views | std::views::enumerate){
			view = vk::image_view(
				alloc.get_device(), {
					.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
					.pNext = nullptr,
					.flags = 0,
					.image = up_sample_image.handle.image,
					.viewType = VK_IMAGE_VIEW_TYPE_2D,
					.format = up_format, // 使用动态推断的格式
					.components = {},
					.subresourceRange = VkImageSubresourceRange{
						VK_IMAGE_ASPECT_COLOR_BIT, static_cast<std::uint32_t>(idx), 1, 0, 1
					}
				}
			);
		}

		VkSampler sampler = get_sampler_at({0, 0});
		vk::descriptor_mapper mapper{descriptor_buffer_};

		for(std::uint32_t i = 0; i < total_passes; ++i){
			mapper.set_image(0, std::get<image_entity>(pass.get_used_resources().get_in(0).resource).handle.image_view,
			                 i, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, sampler);
			mapper.set_image(1, down_sample_image.handle.image_view, i, VK_IMAGE_LAYOUT_GENERAL, sampler);
			mapper.set_image(2, up_sample_image.handle.image_view, i, VK_IMAGE_LAYOUT_GENERAL, sampler);

			if(i < total_mip_level_){
				mapper.set_storage_image(3, down_mipmap_image_views[i], VK_IMAGE_LAYOUT_GENERAL, i);
			} else{
				// 计算逻辑层级并与 target_scale 抵消，使得最后一趟的 conceptual_mip == target_scale 正好写入 up_mipmap 索引 0
				std::uint32_t conceptual_mip = reverse_after(i, total_mip_level_);
				mapper.set_storage_image(3, up_mipmap_image_views[conceptual_mip - target_scale],
				                         VK_IMAGE_LAYOUT_GENERAL, i);
			}
		}
	}

	void record_command(const vk::allocator_usage& alloc, const pass_data& pass, math::u32size2 extent,
	                    VkCommandBuffer buffer) override{
		using namespace vk;

		int raw_scale = get_target_scale();
		if (raw_scale < 0) {
			throw std::invalid_argument("Bloom target scale cannot be negative");
		}
		std::uint32_t target_scale = std::min(static_cast<std::uint32_t>(raw_scale),
		                                      total_mip_level_ > 0 ? total_mip_level_ - 1 : 0u);
		std::uint32_t total_passes = total_mip_level_ * 2 - target_scale;

		pipeline_.bind(buffer, VK_PIPELINE_BIND_POINT_COMPUTE);
		cmd::bind_descriptors(buffer, {descriptor_buffer_});

		const auto down_sample_image = std::get<image_entity>(pass.get_used_resources().get_in(1).resource);
		const auto up_sample_image = std::get<image_entity>(pass.get_used_resources().get_out(0).resource);

		for(std::uint32_t i = 0; i < total_passes; ++i){
			const auto current_mipmap_index = reverse_after(i, total_mip_level_);
			const auto div = 1 << (current_mipmap_index + 1 - (i >= total_mip_level_));
			math::u32size2 current_ext{extent / div};

			if(i > 0){
				if(i <= total_mip_level_){
					// mipmap access barrier
					cmd::memory_barrier(
						buffer,
						down_sample_image.handle.image,
						VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
						VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
						VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
						VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL,
						VK_IMAGE_LAYOUT_GENERAL,

						VkImageSubresourceRange{
							VK_IMAGE_ASPECT_COLOR_BIT, reverse_after(i - 1, total_mip_level_), 1, 0, 1
						}
					);
				} else{
					cmd::memory_barrier(
						buffer,
						down_sample_image.handle.image,
						VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
						VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
						VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
						VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL,
						VK_IMAGE_LAYOUT_GENERAL,

						VkImageSubresourceRange{
							VK_IMAGE_ASPECT_COLOR_BIT, reverse_after(i, total_mip_level_) + 1, 1, 0, 1
						}
					);
				}
			}

			// 检查是否为最后一趟上采样，转换最终图像布局
			if(i == total_passes - 1 && i >= total_mip_level_){
				//Final, set output image layout to general
				cmd::memory_barrier(
					buffer,
					up_sample_image.handle.image,
					VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					VK_ACCESS_2_NONE,
					VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
					VK_IMAGE_LAYOUT_GENERAL,
					VK_IMAGE_LAYOUT_GENERAL
				);
			}

			cmd::set_descriptor_offsets(buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout_, 0, {0},
			                            {descriptor_buffer_.get_chunk_offset(i)});

			const auto groups = get_work_group_size(current_ext);

			const auto layerinfo = get_current_defines(i, extent);
			vkCmdPushConstants(buffer, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(bloom_defines), &layerinfo);

			vkCmdDispatch(buffer, groups.x, groups.y, 1);
		}
	}

private:
	bloom_defines get_current_defines(unsigned layerIndex, const math::u32size2 extent) const noexcept{
		return {
			.currentLayerExtent = [&] -> math::u32size2{
				if(layerIndex < total_mip_level_){
					return {extent.x >> (layerIndex + 1), extent.y >> (layerIndex + 1)};
				} else{
					return {extent.x >> reverse_after(layerIndex, total_mip_level_), extent.y >> reverse_after(layerIndex, total_mip_level_)};
				}
			}(),
				.current_layer = reverse_after(layerIndex, total_mip_level_),
				.up_scaling = layerIndex >= total_mip_level_,
				.total_layer = total_mip_level_,
				.target_scale = static_cast<std::uint32_t>(std::max(0, get_target_scale())), // 传入 target_scale
		};
	}

	[[nodiscard]] int get_target_scale() const {
		const auto& req = meta().sockets.at_out<image_requirement>(0);
		return std::get<int>(req.extent.extent);
	}
};

struct bloom_meta_config{
	int target_scale = 0;
	std::optional<VkSpecializationInfo> specializationInfo = std::nullopt;

	VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
	VkImageLayout target_layout = VK_IMAGE_LAYOUT_UNDEFINED;


	VkImageLayout get_target_layout() const noexcept{
		return target_layout != VK_IMAGE_LAYOUT_UNDEFINED ? target_layout : (target_layout == 0 ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
};

export
	[[nodiscard]] post_process_meta get_bloom_default_meta(
		const vk::shader_module& shader_module,
		bloom_meta_config config){
		post_process_meta meta{
				shader_module,
				{
					{{0}, {0, no_slot}}, //Input
					{{1}, {1, no_slot}},
					{{2}, {no_slot, 0}}, //Output
					{{3}, {no_slot, 0}},
				},
				config.specializationInfo
			};

	meta.set_format_at_in(1, config.format);
	meta.set_format_at_out(0, config.format);

	{
		auto& req = meta.sockets.at_out<image_requirement>(0);

		req.override_initial_layout = VK_IMAGE_LAYOUT_GENERAL;
		req.override_output_layout = config.get_target_layout();
		req.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		req.mip_level = 6 - config.target_scale;
		req.extent.extent = config.target_scale;
	}

	{
		auto& req = meta.sockets.at_in<image_requirement>(1);

		req.override_initial_layout = req.override_output_layout = VK_IMAGE_LAYOUT_GENERAL;
		req.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		req.mip_level = 6;
		req.extent.extent = 1;
	}

	return meta;
}

}
