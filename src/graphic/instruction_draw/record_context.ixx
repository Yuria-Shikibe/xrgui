module;

#include <vulkan/vulkan.h>
#include <cassert>

export module mo_yanxi.vk.record_context;

import mo_yanxi.vk;
import mo_yanxi.vk.cmd;

import std;

namespace mo_yanxi::graphic::g2d{

export
/**
 * @brief One descriptor buffer binding and the set it targets.
 *
 * `chunk_size` and `initial_offset` let the renderer select per-frame or
 * per-draw slices from a descriptor buffer when recording command buffer
 * offsets.
 */
struct descriptor_buffer_usage{
	VkDescriptorBufferBindingInfoEXT info;
	std::uint32_t target_set;
	VkDeviceSize chunk_size;
	VkDeviceSize initial_offset;
};

export
/**
 * @brief Batches Vulkan descriptor-buffer bind and offset commands.
 *
 * Callers push the descriptor buffers needed by a compute or graphics pass,
 * then call `prepare_bindings()` before recording. The context sorts by target
 * set, binds descriptor buffers only when the actual buffer addresses changed,
 * and emits consecutive `vkCmdSetDescriptorBufferOffsetsEXT` ranges. This is
 * the current renderer path; Descriptor Heap support is not wired into command
 * recording yet.
 */
template <typename Alloc = std::allocator<descriptor_buffer_usage>>
struct record_context{
private:
	std::vector<descriptor_buffer_usage, typename std::allocator_traits<Alloc>::template rebind_alloc<descriptor_buffer_usage>> binding_infos;
	std::vector<std::uint32_t, typename std::allocator_traits<Alloc>::template rebind_alloc<std::uint32_t>> designators{};
	std::vector<VkDeviceSize, typename std::allocator_traits<Alloc>::template rebind_alloc<VkDeviceSize>> tempOffsets{};
	std::vector<VkDescriptorBufferBindingInfoEXT, typename std::allocator_traits<Alloc>::template rebind_alloc<VkDescriptorBufferBindingInfoEXT>> tempBindInfos{};

	// Tracks the descriptor buffers currently bound in this command buffer.
	std::vector<VkDescriptorBufferBindingInfoEXT, typename std::allocator_traits<Alloc>::template rebind_alloc<VkDescriptorBufferBindingInfoEXT>> boundBindInfos{};

public:
	[[nodiscard]] explicit record_context(
		const Alloc& alloc = {})
	: binding_infos(alloc)
	, designators(alloc)
	, tempOffsets(alloc)
	, tempBindInfos(alloc)
	, boundBindInfos(alloc){}

	void clear() noexcept{
		binding_infos.clear();
		designators.clear();
		tempOffsets.clear();
		tempBindInfos.clear();
		// Keep boundBindInfos across pass-level clears inside one command buffer.
	}

	// Call when switching or rerecording command buffers.
	void reset_binding_state() noexcept{
		boundBindInfos.clear();
	}

	auto& get_bindings() noexcept{
		return binding_infos;
	}

	void push(std::uint32_t target_set, VkDescriptorBufferBindingInfoEXT info, VkDeviceSize chunk_size = 0, VkDeviceSize initial_off = 0){
		binding_infos.emplace_back(info, target_set, chunk_size, initial_off);
	}

	void prepare_bindings(){
		std::ranges::sort(binding_infos, {}, &descriptor_buffer_usage::target_set);
		tempBindInfos.assign_range(binding_infos | std::views::transform(&descriptor_buffer_usage::info));
	}

	void operator()(VkPipelineLayout layout, const VkCommandBuffer buf, std::uint32_t index, VkPipelineBindPoint bindingPoint){
		static constexpr auto is_consecutive = [
			](const descriptor_buffer_usage& left, const descriptor_buffer_usage& right){
			assert(right.target_set != left.target_set);
			return right.target_set == left.target_set + 1;
		};

		bool needs_bind = tempBindInfos.size() != boundBindInfos.size();
		if(!needs_bind){
			for(std::size_t i = 0; i < tempBindInfos.size(); ++i){
				if(tempBindInfos[i].address != boundBindInfos[i].address){
					needs_bind = true;
					break;
				}
			}
		}

		if(needs_bind){
			vk::cmd::bindDescriptorBuffersEXT(buf, (std::uint32_t)std::ranges::size(tempBindInfos), std::ranges::data(tempBindInfos));
			boundBindInfos.assign(tempBindInfos.begin(), tempBindInfos.end());
		}

		std::uint32_t current_src{};
		for(auto&& chunk : binding_infos | std::views::chunk_by(is_consecutive)){
			const std::uint32_t chunk_size = (std::uint32_t)std::ranges::size(chunk);
			designators.assign_range(std::views::iota(current_src, current_src + chunk_size));
			tempOffsets.assign_range(chunk | std::views::transform([&](const descriptor_buffer_usage& info){
				return info.chunk_size * index + info.initial_offset;
			}));

			vk::cmd::setDescriptorBufferOffsetsEXT(
				buf, bindingPoint, layout,
				chunk.front().target_set,
				chunk_size,
				std::ranges::data(designators), std::ranges::data(tempOffsets));

			current_src += chunk_size;
		}
	}
};
}
