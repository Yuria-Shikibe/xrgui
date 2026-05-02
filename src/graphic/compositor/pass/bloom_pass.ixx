module;

#include <vulkan/vulkan.h>

export module mo_yanxi.graphic.compositor.bloom;

import std;
export import mo_yanxi.graphic.compositor.sub_pass;
import mo_yanxi.vk;
import mo_yanxi.math.vector2;

namespace mo_yanxi::graphic::compositor{
[[nodiscard]] constexpr std::uint32_t reverse_after(std::uint32_t value, std::uint32_t ceil) noexcept{
	if(value < ceil) return value;
	return (ceil * 2 - value - 1);
}

[[nodiscard]] constexpr std::uint32_t get_real_mip_level(std::uint32_t expected, math::u32size2 extent) noexcept{
	return std::min(expected, vk::get_recommended_mip_level(extent.x, extent.y));
}

struct bloom_defines {
	math::vector2<std::uint32_t> currentLayerExtent;
	std::uint32_t current_layer;
	std::uint32_t up_scaling;
	std::uint32_t total_layer;
	std::uint32_t target_scale;
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
struct bloom_pass final : sub_pass_stage{
	using sub_pass_stage::sub_pass_stage;

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

	std::uint32_t max_mip_level_{7};

	struct mip_info{
		std::uint32_t total_mip_level;
		std::uint32_t target_scale;
	};

	[[nodiscard]] mip_info resolve_mip_info(const math::u32size2 extent) const{
		const int raw_scale = get_target_scale();
		if(raw_scale < 0){
			throw std::invalid_argument("Bloom target scale cannot be less than 0");
		}

		const auto total_mip_level = std::min(max_mip_level_, get_real_mipmap_level(extent));
		if(total_mip_level == 0){
			throw std::runtime_error("Bloom mip level resolved to zero");
		}

		const auto target_scale = std::min(static_cast<std::uint32_t>(raw_scale),
			total_mip_level > 0 ? total_mip_level - 1 : 0u);
		return {total_mip_level, target_scale};
	}

	[[nodiscard]] std::uint32_t get_required_mipmap_level() const noexcept{
		return sockets().at_out<image_requirement>(0).mip_level;
	}

	[[nodiscard]] std::uint32_t get_real_mipmap_level(const math::u32size2 extent) const noexcept{
		return get_real_mip_level(get_required_mipmap_level(), extent);
	}

	void prepare(const vk::allocator_usage& alloc, const pass_data& pass, const math::u32size2 extent) override{
		sub_pass_stage::prepare(alloc, pass, extent);
		if(samplers_.contains({0, 0})){
			const auto sampler = get_sampler_at({0, 0});
			samplers_.try_emplace(binding_info{1, 0}, sampler);
			samplers_.try_emplace(binding_info{2, 0}, sampler);
		}
		uniform_buffer_ = {alloc, sizeof(bloom_uniform_block)};
		(void)vk::buffer_mapper{uniform_buffer_}.load(current_param);
	}
	[[nodiscard]] std::vector<sub_pass_setup> build_sub_passes(const pass_data& pass,
		const math::u32size2 extent) const override{
		(void)pass;

		const auto [total_mip_level, target_scale] = resolve_mip_info(extent);
		const std::uint32_t total_passes = total_mip_level * 2 - target_scale;

		std::vector<sub_pass_setup> setups;
		setups.reserve(total_passes);
		for(std::uint32_t i = 0; i < total_passes; ++i){
			sub_pass_setup setup{};
			setup.push_constants.resize(sizeof(bloom_defines));
			const auto layer_info = get_current_defines(i, extent, total_mip_level);
			std::memcpy(setup.push_constants.data(), &layer_info, sizeof(layer_info));

			const auto& unit_size = meta().shader_info().thread_group_size;
			setup.dispatch.fixed_group_count = VkExtent3D{
				.width = (layer_info.currentLayerExtent.x + unit_size.x - 1) / unit_size.x,
				.height = (layer_info.currentLayerExtent.y + unit_size.y - 1) / unit_size.y,
				.depth = 1,
			};

			setup.bindings.push_back({.binding = {1, 0}, .view = {
				.base_mip_level = 0,
				.level_count = 0,
				.base_array_layer = 0,
				.layer_count = 1,
				.image_layout = VK_IMAGE_LAYOUT_GENERAL,
			}});

			setup.bindings.push_back({.binding = {2, 0}, .view = {
				.base_mip_level = 0,
				.level_count = 0,
				.base_array_layer = 0,
				.layer_count = 1,
				.image_layout = VK_IMAGE_LAYOUT_GENERAL,
			}});

			setup.bindings.push_back(sub_pass_binding_view{
				.binding = {3, 0},
				.resource_slot = i < total_mip_level ? slot_pair{1, no_slot} : slot_pair{no_slot, 0},
				.view = {
				.base_mip_level = i < total_mip_level ? i : reverse_after(i, total_mip_level) - target_scale,
				.level_count = 1,
				.base_array_layer = 0,
				.layer_count = 1,
				.image_layout = VK_IMAGE_LAYOUT_GENERAL,
			}});

			setups.push_back(std::move(setup));
		}

		return setups;
	}
	[[nodiscard]] std::vector<sub_pass_baked_sync> bake_sub_pass_sync(const pass_data& pass,
		const math::u32size2 extent,
		std::span<const sub_pass_runtime_setup> prepared_sub_passes) const override{
		const auto [total_mip_level, target_scale] = resolve_mip_info(extent);
		std::vector<sub_pass_baked_sync> baked(prepared_sub_passes.size() > 1 ? prepared_sub_passes.size() - 1 : 0);

		const auto down_sample_image = std::get<image_entity>(pass.get_used_resources().get_in(1).resource);
		const auto up_sample_image = std::get<image_entity>(pass.get_used_resources().get_out(0).resource);

		for(std::uint32_t i = 0; i + 1 < prepared_sub_passes.size(); ++i){
			auto& sync = baked[i];
			VkImage image = VK_NULL_HANDLE;
			std::uint32_t mip_level = 0;

			if(i + 1 <= total_mip_level){
				image = down_sample_image.handle.image;
				mip_level = reverse_after(i, total_mip_level);
			} else{
				image = down_sample_image.handle.image;
				mip_level = reverse_after(i + 1, total_mip_level) + 1;
			}

			sync.image_barriers.push_back({
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
				.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
				.newLayout = VK_IMAGE_LAYOUT_GENERAL,
				.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.image = image,
				.subresourceRange = {
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = mip_level,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				}
			});

			if(i >= total_mip_level && i + 1 > total_mip_level){
				sync.image_barriers.push_back({
					.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
					.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
					.newLayout = VK_IMAGE_LAYOUT_GENERAL,
					.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
					.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
					.image = up_sample_image.handle.image,
					.subresourceRange = {
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.baseMipLevel = reverse_after(i, total_mip_level) - target_scale,
						.levelCount = 1,
						.baseArrayLayer = 0,
						.layerCount = 1,
					}
				});
			}

			if(i + 1 == prepared_sub_passes.size() - 1 && i + 1 >= total_mip_level){
				sync.image_barriers.push_back({
					.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
					.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.srcAccessMask = VK_ACCESS_2_NONE,
					.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
					.newLayout = VK_IMAGE_LAYOUT_GENERAL,
					.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
					.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
					.image = up_sample_image.handle.image,
					.subresourceRange = {
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.baseMipLevel = reverse_after(i + 1, total_mip_level) - target_scale,
						.levelCount = 1,
						.baseArrayLayer = 0,
						.layerCount = 1,
					}
				});
			}
		}

		return baked;
	}

private:
	bloom_defines get_current_defines(unsigned layerIndex, const math::u32size2 extent,
		const std::uint32_t total_mip_level) const noexcept{
		return {
			.currentLayerExtent = [&] -> math::u32size2{
				if(layerIndex < total_mip_level){
					return {extent.x >> (layerIndex + 1), extent.y >> (layerIndex + 1)};
				} else{
					return {extent.x >> reverse_after(layerIndex, total_mip_level), extent.y >> reverse_after(layerIndex, total_mip_level)};
				}
			}(),
				.current_layer = reverse_after(layerIndex, total_mip_level),
				.up_scaling = layerIndex >= total_mip_level,
				.total_layer = total_mip_level,
				.target_scale = static_cast<std::uint32_t>(std::max(0, get_target_scale())),
		};
	}

	[[nodiscard]] int get_target_scale() const {
		const auto& req = meta().sockets.at_out<image_requirement>(0);
		return std::get<int>(req.extent.extent);
	}
};

struct bloom_meta_config{
	int target_scale = 0;

	VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
	VkImageLayout target_layout = VK_IMAGE_LAYOUT_UNDEFINED;


	VkImageLayout get_target_layout() const noexcept{
		return target_layout != VK_IMAGE_LAYOUT_UNDEFINED ? target_layout : VK_IMAGE_LAYOUT_GENERAL;
	}
};

export
	[[nodiscard]] post_process_meta get_bloom_default_meta(
		compute_shader_info&& shader_info,
		bloom_meta_config config){
		post_process_meta meta{
				std::move(shader_info),
				{
					{{0}, {0, no_slot}}, //Input
					{{1}, {1, no_slot}},
					{{2}, {no_slot, 0}}, //Output
					{{3}, {no_slot, 0}},
				}
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
