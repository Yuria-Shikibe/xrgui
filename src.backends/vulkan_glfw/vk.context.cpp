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

	auto& current_syncs = ++sync_arr;
	std::uint32_t imageIndex;


	const VkAcquireNextImageInfoKHR acquire_info{
			.sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
			.pNext = nullptr,
			.swapchain = swap_chain,
			.timeout = std::numeric_limits<std::uint64_t>::max(),
			.semaphore = current_syncs.fetch_semaphore,
			.fence = nullptr,
			.deviceMask = 0x1
		};

	current_syncs.fence.wait_and_reset();


	const auto result = vkAcquireNextImage2KHR(device, &acquire_info, &imageIndex);

	bool out_of_date{};
	if(result == VK_ERROR_OUT_OF_DATE_KHR){
		out_of_date = true;
	} else if(result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR){
		throw vk_error(result, "failed to acquire swap chain image!");
	}

	const auto& frame = swap_chain_frames[imageIndex];
	if(!frame.post_command){
		if(window_.check_resized()){
			recreate(true);
		}

		return;
	}

	cmd::submit_command(
		device.primary_graphics_queue(),
		frame.post_command,
		current_syncs.fence,
		current_syncs.fetch_semaphore,
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
		frame.flush_semaphore,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT
	);


	const VkPresentInfoKHR info{
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.pNext = nullptr,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = frame.flush_semaphore.as_data(),
			.swapchainCount = 1,
			.pSwapchains = swap_chain.as_data(),
			.pImageIndices = &imageIndex,
			.pResults = nullptr,
		};


	if(const auto result2 = vkQueuePresentKHR(device.present_queue(), &info);
		false
		|| out_of_date
		|| result2 == VK_ERROR_OUT_OF_DATE_KHR
		|| result2 == VK_SUBOPTIMAL_KHR
		|| window_.check_resized()
	){
		(void)window_.check_resized();
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

	if(!no_fence_wait){
		for(const auto& arr : sync_arr){
			arr.fence.wait();
		}
	}else{
		for(auto& arr : sync_arr){
			arr.fence = vk::fence{get_device(), true};
		}
	}

	if(!image_data.image){
		//Do nothing when there is no image to post
		for(auto&& swap_chain_frame : swap_chain_frames){
			swap_chain_frame.post_command = {};
		}
		return;
	}

	for(auto&& swap_chain_frame : swap_chain_frames){
		auto& bf = swap_chain_frame.post_command;
		bf = main_graphic_command_pool.obtain();
		bf.begin(VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);

		cmd::dependency_gen dependency_gen{};
		constexpr VkImageSubresourceRange subresource_range{
				VK_IMAGE_ASPECT_COLOR_BIT,
				0, 1, 0, 1
			};

		{
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

			dependency_gen.push(
				swap_chain_frame.image,
				image_data.src_stage,
				VK_ACCESS_2_NONE,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				subresource_range
			);

			dependency_gen.apply(bf);
		}

		if(image_data.extent.width == get_extent().width && image_data.extent.height == get_extent().height){
			cmd::copy_image_to_image(
				bf,
				image_data.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				swap_chain_frame.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, {
					VkImageCopy{
						.srcSubresource = {
							.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
							.mipLevel = 0,
							.baseArrayLayer = 0,
							.layerCount = 1
						},
						.srcOffset = {},
						.dstSubresource = {
							.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
							.mipLevel = 0,
							.baseArrayLayer = 0,
							.layerCount = 1
						},
						.dstOffset = {},
						.extent = get_extent3()
					}
				}
			);
		} else{
			cmd::image_blit(
				bf,
				image_data.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				swap_chain_frame.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_FILTER_LINEAR, {
					VkImageBlit{
						.srcSubresource = {
							.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
							.mipLevel = 0,
							.baseArrayLayer = 0,
							.layerCount = 1
						},
						.srcOffsets = {
							{},
							{
								static_cast<std::int32_t>(image_data.extent.width),
								static_cast<std::int32_t>(image_data.extent.height), 1
							}
						},
						.dstSubresource = {
							.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
							.mipLevel = 0,
							.baseArrayLayer = 0,
							.layerCount = 1
						},
						.dstOffsets = {
							{},
							{
								static_cast<std::int32_t>(swap_chain_extent.width),
								static_cast<std::int32_t>(swap_chain_extent.height), 1
							}
						}
					}
				}
			);
		}


		if(image_data.clear){
			dependency_gen.push(
				image_data.image,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				VK_ACCESS_2_TRANSFER_READ_BIT,
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
			dependency_gen.push_post_write(
				swap_chain_frame.image,
				VK_PIPELINE_STAGE_2_NONE,
				VK_ACCESS_2_NONE,
				VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
				subresource_range
			);

			if(image_data.dst_layout != VK_IMAGE_LAYOUT_UNDEFINED)
				dependency_gen.push(
					image_data.image,
					VK_PIPELINE_STAGE_2_TRANSFER_BIT,
					image_data.clear ? VK_ACCESS_2_TRANSFER_WRITE_BIT : VK_ACCESS_2_TRANSFER_READ_BIT,
					image_data.dst_stage,
					image_data.dst_access,
					image_data.clear ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
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
