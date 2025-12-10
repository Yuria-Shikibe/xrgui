module;

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

module mo_yanxi.backend.vulkan.context;


namespace mo_yanxi::backend::vulkan{
/**
 * @return All Required Extensions
 */
std::vector<const char*> get_required_extensions_glfw(){
	std::uint32_t glfwExtensionCount{};
	const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

	return {glfwExtensions, glfwExtensions + glfwExtensionCount};
}

std::vector<const char*> get_required_extensions(){
	auto extensions = get_required_extensions_glfw();

	if(enable_validation_layers){
		extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}

	extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);


	return extensions;
}

void context::flush(){
	if(!final_staging_image.image) return;
	if(!std::ranges::all_of(swap_chain_frames, &SwapChainFrameData::post_command)){
		return;
	}

	auto& current_syncs = ++sync_arr;
	std::uint32_t imageIndex;

	const VkAcquireNextImageInfoKHR acquire_info{
			.sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
			.pNext = nullptr,
			.swapchain = swap_chain,
			.timeout = std::numeric_limits<std::uint64_t>::max(),
			.semaphore = current_syncs.fetch_semaphore, // Acquire 成功后 signal 这个 semaphore
			.fence = nullptr,
			.deviceMask = 0x1
		};

	const auto result = vkAcquireNextImage2KHR(device, &acquire_info, &imageIndex);

	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		current_syncs.fence.wait_and_reset();
		recreate(false);
		return;
	} else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		throw vk_error(result, "failed to acquire swap chain image!");
	}

	const auto& frame = swap_chain_frames[imageIndex];

	current_syncs.fence.wait_and_reset();

	cmd::submit_command(
		device.primary_graphics_queue(),
		frame.post_command,
		current_syncs.fence,           // 提交完成后 signal 这个 fence
		current_syncs.fetch_semaphore, // 等待 Acquire 完成
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, // 等待阶段 (根据 post_command 实际操作调整，通常是 Transfer)
		frame.flush_semaphore,         // 渲染/拷贝完成后 signal 这个 semaphore
		VK_PIPELINE_STAGE_2_TRANSFER_BIT // Signal 阶段
	);

	const VkPresentInfoKHR info{
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.pNext = nullptr,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = frame.flush_semaphore.as_data(), // 等待 submit 完成
			.swapchainCount = 1,
			.pSwapchains = swap_chain.as_data(),
			.pImageIndices = &imageIndex,
			.pResults = nullptr,
		};

	const auto result2 = vkQueuePresentKHR(device.present_queue(), &info);

	if(result2 == VK_ERROR_OUT_OF_DATE_KHR || result2 == VK_SUBOPTIMAL_KHR || window_.check_resized()){

		recreate(false);
	} else if(result2 != VK_SUCCESS){
		throw vk_error(result2, "Failed to present swap chain image!");
	}
}

void context::wait_on_device() const{
	if(const auto rst = vkDeviceWaitIdle(device)){
		throw vk_error{rst, "Failed to wait on device."};
	}
}

void context::record_post_command(bool no_fence_wait){
	auto& image_data = final_staging_image;

	// FIX: 优化 Fence 处理，避免创建销毁
	if(!no_fence_wait){
		for(const auto& arr : sync_arr){
			arr.fence.wait();
		}
	} else {
		for(auto& arr : sync_arr){
             arr.fence = vk::fence{get_device(), true};
		}
	}

	if(!image_data.image){
		for(auto&& swap_chain_frame : swap_chain_frames){
			swap_chain_frame.post_command = {};
		}
		return;
	}

	cmd::dependency_gen dependency_gen{};
	constexpr VkImageSubresourceRange subresource_range{
		VK_IMAGE_ASPECT_COLOR_BIT,
		0, 1, 0, 1
	};

	for(auto&& swap_chain_frame : swap_chain_frames){
		auto& bf = swap_chain_frame.post_command;
		bf = main_graphic_command_pool.obtain();

		bf.begin(VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);

		// --- Barrier Logic ---
		// 注意：这里假设 image_data.image 在每次 Flush 前都处于 src_layout。
		// 并且在执行完这个 CB 后，它会变回 dst_layout。

		{
			// 1. Staging Image: src_layout -> TRANSFER_SRC
			dependency_gen.push(
				image_data.image,
				image_data.src_stage,
				image_data.src_access,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				VK_ACCESS_2_TRANSFER_READ_BIT,
				image_data.src_layout,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				subresource_range,
				image_data.owner_queue_family,
				graphic_family()
			);

			// 2. Swapchain Image: UNDEFINED -> TRANSFER_DST
			dependency_gen.push(
				swap_chain_frame.image,
				VK_PIPELINE_STAGE_2_NONE, // 不需要等待之前的内容，因为我们将完全覆盖
				VK_ACCESS_2_NONE,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, // 我们不关心 Swapchain 之前的内容
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				subresource_range
			);

			dependency_gen.apply(bf);
		}

		// Copy or Blit
		if(image_data.extent.width == get_extent().width && image_data.extent.height == get_extent().height){
			cmd::copy_image_to_image(
				bf,
				image_data.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				swap_chain_frame.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, {
					VkImageCopy{
						.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
						.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
						.extent = get_extent3()
					}
				}
			);
		} else {
			cmd::image_blit(
				bf,
				image_data.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				swap_chain_frame.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_FILTER_LINEAR, {
					VkImageBlit{
						.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
						.srcOffsets = {{}, {static_cast<std::int32_t>(image_data.extent.width), static_cast<std::int32_t>(image_data.extent.height), 1}},
						.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
						.dstOffsets = {{}, {static_cast<std::int32_t>(swap_chain_extent.width), static_cast<std::int32_t>(swap_chain_extent.height), 1}}
					}
				}
			);
		}

		// Clear if requested (Clear the staging image? This is unusual but kept as per logic)
		if(image_data.clear){
            // ... (保持原有 Clear 逻辑)
			// 注意：Clear 操作是在 TRANSFER_DST_OPTIMAL 布局下进行的，所以上面的 barrier 已经涵盖了。
			// 但这里又加入了一个 barrier? 检查 dependency_gen 实现。
            // 修正建议：如果 copy 后要 clear 原图，需要 barrier 保证 copy 完成。
            // 原代码：push(..., READ, WRITE, ... SRC_OPT, DST_OPT) -> 正确。
			dependency_gen.push(
				image_data.image,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				VK_ACCESS_2_TRANSFER_READ_BIT, // 等待之前的 Copy Read
				VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				subresource_range
			);
			dependency_gen.apply(bf);

			cmd::clear_color_image(
				bf,
				image_data.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				{}, {subresource_range});
		}

		{
			// Final Barriers

			// 1. Swapchain Image: TRANSFER_DST -> PRESENT_SRC
			dependency_gen.push_post_write(
				swap_chain_frame.image,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				0,
				VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
				subresource_range
			);

			// 2. Staging Image: Restore Layout
			// 如果进行了 Clear，当前 Layout 是 TRANSFER_DST_OPTIMAL。
			// 如果没 Clear，当前 Layout 是 TRANSFER_SRC_OPTIMAL。
			VkImageLayout current_staging_layout = image_data.clear ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			VkAccessFlags2 current_access = image_data.clear ? VK_ACCESS_2_TRANSFER_WRITE_BIT : VK_ACCESS_2_TRANSFER_READ_BIT;

			if(image_data.dst_layout != VK_IMAGE_LAYOUT_UNDEFINED)
				dependency_gen.push(
					image_data.image,
					VK_PIPELINE_STAGE_2_TRANSFER_BIT,
					current_access,
					image_data.dst_stage,
					image_data.dst_access,
					current_staging_layout,
					image_data.dst_layout,
					subresource_range,
					graphic_family(),
					image_data.owner_queue_family
				);
			dependency_gen.apply(bf);
		}

		bf.end();
	}
}

void context::recreate(bool no_fence_wait){
	(void)window_.check_resized();

	while(window_.iconified() || window_.get_size().width == 0 || window_.get_size().height == 0){
		window_.wait_event();
	}

	wait_on_device();

	if(last_swap_chain) vkDestroySwapchainKHR(device, last_swap_chain, nullptr);
	last_swap_chain.handle = swap_chain;

	createSwapChain();

	for(const auto& reference : eventManager | std::views::values){
		reference(*this, window_instance::resize_event(get_extent()));
	}

	record_post_command(no_fence_wait);
}
}
