module;

#include <vulkan/vulkan.h>
#include <cassert>

export module mo_yanxi.graphic.draw.instruction.util;

import mo_yanxi.vk;
import mo_yanxi.vk.cmd;

import std;

namespace mo_yanxi::graphic::draw{

export
struct descriptor_buffer_usage{
	VkDescriptorBufferBindingInfoEXT info;
	VkDeviceSize chunk_size;
	std::uint32_t target_set;
};

export
template <typename Alloc = std::allocator<descriptor_buffer_usage>>
struct record_context{
private:
	std::vector<descriptor_buffer_usage, typename std::allocator_traits<Alloc>::template rebind_alloc<descriptor_buffer_usage>> binding_infos;
	std::vector<std::uint32_t, typename std::allocator_traits<Alloc>::template rebind_alloc<std::uint32_t>> designators{};
	std::vector<VkDeviceSize, typename std::allocator_traits<Alloc>::template rebind_alloc<VkDeviceSize>> tempOffsets{};
	std::vector<VkDescriptorBufferBindingInfoEXT, typename std::allocator_traits<Alloc>::template rebind_alloc<VkDescriptorBufferBindingInfoEXT>> tempBindInfos{};

public:
	[[nodiscard]] explicit record_context(
		const Alloc& alloc = {})
	: binding_infos(alloc)
	, designators(alloc)
	, tempOffsets(alloc)
	, tempBindInfos(alloc){}

	auto& get_bindings() noexcept{
		return binding_infos;
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

		vk::cmd::bindDescriptorBuffersEXT(buf, std::ranges::size(tempBindInfos), std::ranges::data(tempBindInfos));

		std::uint32_t current_src{};
		for(auto&& chunk : binding_infos | std::views::chunk_by(is_consecutive)){
			const std::uint32_t chunk_size = std::ranges::size(chunk);
			designators.assign_range(std::views::iota(current_src, current_src + chunk_size));
			tempOffsets.assign_range(chunk | std::views::transform([&](const descriptor_buffer_usage& info){
				return info.chunk_size * index;
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