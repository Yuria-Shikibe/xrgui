module;

#include <vulkan/vulkan.h>

export module mo_yanxi.graphic.compositor.sub_pass;

import std;
export import mo_yanxi.graphic.compositor.post_process_pass;

namespace mo_yanxi::graphic::compositor{
export
struct sub_pass_baked_sync{
	std::vector<VkImageMemoryBarrier2> image_barriers{};
	std::vector<VkBufferMemoryBarrier2> buffer_barriers{};
};

export
struct sub_pass_stage : post_process_stage{
	using post_process_stage::post_process_stage;

protected:
	struct image_view_binding_state{
		binding_info binding{};
		resource_entity resource{};
		resource_requirement requirement{};
		image_binding_override override{};
	};

	struct sub_pass_runtime_setup{
		sub_pass_setup setup{};
		std::vector<image_view_binding_state> image_bindings{};
	};

	std::vector<sub_pass_runtime_setup> prepared_sub_passes_{};
	std::vector<sub_pass_baked_sync> baked_sync_{};
	std::vector<vk::image_view> transient_image_views_{};
	std::vector<VkPushConstantRange> push_constant_ranges_{};

	[[nodiscard]] virtual std::vector<sub_pass_setup> build_sub_passes(const pass_data& pass,
		const math::u32size2 extent) const = 0;

	[[nodiscard]] virtual std::vector<sub_pass_baked_sync> bake_sub_pass_sync(const pass_data& pass,
		const math::u32size2 extent,
		std::span<const sub_pass_runtime_setup> prepared_sub_passes) const{
		return std::vector<sub_pass_baked_sync>(prepared_sub_passes.size() > 1 ? prepared_sub_passes.size() - 1 : 0);
	}

	[[nodiscard]] virtual std::vector<VkPushConstantRange> push_constant_ranges() const{
		const auto ranges = meta().push_constant_ranges();
		return {ranges.begin(), ranges.end()};
	}

	void prepare(const vk::allocator_usage& alloc, const pass_data& pass, const math::u32size2 extent) override{
		push_constant_ranges_ = push_constant_ranges();
		init_pipeline_and_ubo(alloc, push_constant_ranges_);

		prepared_sub_passes_.clear();
		baked_sync_.clear();
		transient_image_views_.clear();

		auto sub_passes = build_sub_passes(pass, extent);
		if(sub_passes.empty()){
			throw std::runtime_error("sub_pass_stage requires at least one sub-pass");
		}

		reset_descriptor_buffer(alloc, static_cast<std::uint32_t>(sub_passes.size()));
		default_bind_uniform_buffers(static_cast<std::uint32_t>(sub_passes.size()));

		prepared_sub_passes_.reserve(sub_passes.size());
		for(std::size_t idx = 0; idx < sub_passes.size(); ++idx){
			auto& runtime = prepared_sub_passes_.emplace_back();
			runtime.setup = std::move(sub_passes[idx]);
			prepare_image_overrides(alloc, pass, runtime);
			bind_sub_pass_resources(pass, runtime, static_cast<std::uint32_t>(idx));
		}

		baked_sync_ = bake_sub_pass_sync(pass, extent, prepared_sub_passes_);
		if(prepared_sub_passes_.size() > 1 && baked_sync_.size() != prepared_sub_passes_.size() - 1){
			throw std::runtime_error("sub-pass sync count mismatch");
		}
	}

	void record_command(const vk::allocator_usage& alloc, const pass_data& pass, math::u32size2 extent, VkCommandBuffer buffer) override{
		pipeline_.bind(buffer, VK_PIPELINE_BIND_POINT_COMPUTE);
		for(std::uint32_t idx = 0; idx < prepared_sub_passes_.size(); ++idx){
			bind_descriptor_sets(buffer, idx);

			const auto& runtime = prepared_sub_passes_[idx];
			if(!runtime.setup.push_constants.empty()){
				vkCmdPushConstants(buffer, pipeline_layout_, runtime.setup.push_stages,
					runtime.setup.push_offset, static_cast<std::uint32_t>(runtime.setup.push_constants.size()),
					runtime.setup.push_constants.data());
			}

			const auto groups = resolve_sub_pass_groups(runtime.setup, extent);
			vkCmdDispatch(buffer, groups.width, groups.height, groups.depth);

			if(idx + 1 < prepared_sub_passes_.size()){
				record_sync(buffer, baked_sync_[idx]);
			}
		}
	}

	[[nodiscard]] VkExtent3D resolve_sub_pass_groups(const sub_pass_setup& setup,
		const math::u32size2 fallback_extent) const{
		if(setup.dispatch.fixed_group_count){
			return *setup.dispatch.fixed_group_count;
		}
		const auto target_extent = resolve_dispatch_extent(setup, fallback_extent);
		const auto& unit_size = meta().shader_info().thread_group_size;
		return {
			.width = (target_extent.width + unit_size.x - 1) / unit_size.x,
			.height = (target_extent.height + unit_size.y - 1) / unit_size.y,
			.depth = 1,
		};
	}

	[[nodiscard]] VkExtent2D resolve_dispatch_extent(const sub_pass_setup& setup, const math::u32size2 fallback_extent) const{
		for(const auto& binding_view : setup.bindings){
			const auto& req = get_requirement_for_binding(binding_view.binding);
			if(const auto* img_req = req.get_if<image_requirement>()){
				auto ext = img_req->extent.get_extent({fallback_extent.x, fallback_extent.y});
				ext.width = std::max(ext.width >> binding_view.view.base_mip_level, 1u);
				ext.height = std::max(ext.height >> binding_view.view.base_mip_level, 1u);
				return {ext.width, ext.height};
			}
		}

		return {fallback_extent.x, fallback_extent.y};
	}

	void record_sync(VkCommandBuffer buffer, const sub_pass_baked_sync& sync) const{
		if(sync.image_barriers.empty() && sync.buffer_barriers.empty()){
			return;
		}

		const VkDependencyInfo dep_info{
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.bufferMemoryBarrierCount = static_cast<std::uint32_t>(sync.buffer_barriers.size()),
			.pBufferMemoryBarriers = sync.buffer_barriers.data(),
			.imageMemoryBarrierCount = static_cast<std::uint32_t>(sync.image_barriers.size()),
			.pImageMemoryBarriers = sync.image_barriers.data(),
		};
		vkCmdPipelineBarrier2(buffer, &dep_info);
	}

	void prepare_image_overrides(const vk::allocator_usage& alloc, const pass_data& pass,
		sub_pass_runtime_setup& runtime){
		for(const auto& binding_view : runtime.setup.bindings){
			const auto default_connection = find_connection(binding_view.binding);
			const auto connection = binding_view.resource_slot.is_invalid()
				? default_connection
				: resource_map_entry{
					.binding = binding_view.binding,
					.slot = binding_view.resource_slot,
				};
			const auto res = get_resource_for_connection(pass, connection);
			if(res.type() != resource_type::image){
				runtime.image_bindings.push_back({
					.binding = binding_view.binding,
					.resource = res,
					.requirement = res.overall_requirement,
				});
				continue;
			}

			const auto& req = res.overall_requirement.get<image_requirement>();
			const auto& entity = res.as_image();

			VkImageView image_view = entity.handle.image_view;
			const auto default_level_count = req.mip_level > binding_view.view.base_mip_level
				? req.mip_level - binding_view.view.base_mip_level
				: 1u;
			const auto level_count = binding_view.view.level_count == 0 ? default_level_count : binding_view.view.level_count;
			if(binding_view.view.base_mip_level != 0 ||
				level_count != req.mip_level ||
				binding_view.view.base_array_layer != 0 ||
				binding_view.view.layer_count != 1){
				transient_image_views_.emplace_back(
					alloc.get_device(),
					VkImageViewCreateInfo{
						.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
						.image = entity.handle.image,
						.viewType = req.get_image_view_type(),
						.format = req.get_format(),
						.subresourceRange = VkImageSubresourceRange{
							.aspectMask = req.get_aspect(),
							.baseMipLevel = binding_view.view.base_mip_level,
							.levelCount = level_count,
							.baseArrayLayer = binding_view.view.base_array_layer,
							.layerCount = binding_view.view.layer_count,
						}
					});
				image_view = transient_image_views_.back();
			}

			runtime.image_bindings.push_back({
				.binding = binding_view.binding,
				.resource = res,
				.requirement = res.overall_requirement,
				.override = {
					.image_view = image_view,
					.image_layout = binding_view.view.image_layout,
				}
			});
		}
	}

	void bind_sub_pass_resources(const pass_data& pass, const sub_pass_runtime_setup& runtime,
		const std::uint32_t chunk_index){
		vk::descriptor_mapper mapper{descriptor_buffer_};
		for(const auto& connection : meta_.get_inout_map().get_connections()){
			if(connection.binding.set != 0) continue;
			const auto itr = std::ranges::find(runtime.image_bindings, connection.binding,
				&image_view_binding_state::binding);
			if(itr == runtime.image_bindings.end()){
				const auto res = get_resource_for_connection(pass, connection);
				bind_resource(mapper, chunk_index, connection, res, get_requirement_for_binding(connection.binding));
				continue;
			}

			const auto* image_override = itr->override.image_view == VK_NULL_HANDLE &&
				itr->override.image_layout == VK_IMAGE_LAYOUT_UNDEFINED ? nullptr : &itr->override;
			bind_resource(mapper, chunk_index, connection, itr->resource, itr->requirement, image_override);
		}
	}

	[[nodiscard]] resource_map_entry find_connection(const binding_info binding) const{
		if(auto itr = std::ranges::find(meta_.get_inout_map().get_connections(), binding, &resource_map_entry::binding);
			itr != meta_.get_inout_map().get_connections().end()){
			return *itr;
		}

		throw std::out_of_range("failed to find resource binding in sub-pass stage");
	}
};
}
