module;

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

module mo_yanxi.backend.vulkan.context;

namespace mo_yanxi::backend::vulkan{
constexpr inline std::array device_extensions{
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,

		VK_KHR_MAINTENANCE_5_EXTENSION_NAME,
		VK_KHR_MAINTENANCE_4_EXTENSION_NAME,
		VK_KHR_MAINTENANCE_9_EXTENSION_NAME,
		// VK_KHR_SHADER_UNTYPED_POINTERS_EXTENSION_NAME,

		// VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME,
		// VK_EXT_MESH_SHADER_EXTENSION_NAME,
		VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME,
	};

namespace RequiredFeatures{
constexpr VkPhysicalDeviceMeshShaderFeaturesEXT MeshShaderFeatures{
		[]{
			VkPhysicalDeviceMeshShaderFeaturesEXT features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};

			features.meshShader = true;

			return features;
		}()
	};

constexpr VkPhysicalDeviceVulkan13Features PhysicalDeviceVulkan13Features{
		[]{
			VkPhysicalDeviceVulkan13Features features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};

			features.synchronization2 = true;
			features.dynamicRendering = true;
			features.maintenance4 = true;

			return features;
		}()
	};

constexpr VkPhysicalDeviceVulkan14Features PhysicalDeviceVulkan14Features{
		[]{
			VkPhysicalDeviceVulkan14Features features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES};


			return features;
		}()
	};

constexpr VkPhysicalDeviceShaderUntypedPointersFeaturesKHR UntypedPointer{
		[]{
			VkPhysicalDeviceShaderUntypedPointersFeaturesKHR features{
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_UNTYPED_POINTERS_FEATURES_KHR
				};

			features.shaderUntypedPointers = true;

			return features;
		}()
	};

constexpr VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR PhysicalDeviceComputeShaderDerivativesFeaturesKHR{
		[]{
			VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR features{
					VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR
				};

			features.computeDerivativeGroupQuads = true;
			features.computeDerivativeGroupLinear = true;

			return features;
		}()
	};

constexpr VkPhysicalDeviceVulkan12Features PhysicalDeviceVulkan12Features{
		[]{
			VkPhysicalDeviceVulkan12Features features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};

			features.bufferDeviceAddress = true;
			features.timelineSemaphore = true;
			features.descriptorBindingPartiallyBound = true;
			features.runtimeDescriptorArray = true;
			features.scalarBlockLayout = true;

			return features;
		}()
	};

constexpr VkPhysicalDeviceVulkan11Features PhysicalDeviceVulkan11Features{
		[]{
			VkPhysicalDeviceVulkan11Features features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};

			features.storageBuffer16BitAccess = true;

			return features;
		}()
	};

constexpr VkPhysicalDeviceFeatures RequiredFeatures{
		[]{
			VkPhysicalDeviceFeatures features{};

			features.samplerAnisotropy = true;
			features.independentBlend = true;
			features.sampleRateShading = true;
			features.geometryShader = true;

			return features;
		}()
	};

constexpr VkPhysicalDeviceDescriptorBufferFeaturesEXT DescriptorBufferFeatures{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT,
		.pNext = nullptr,
		.descriptorBuffer = true,
		.descriptorBufferCaptureReplay = false,
		.descriptorBufferImageLayoutIgnored = false,
		.descriptorBufferPushDescriptors = false
	};

constexpr VkPhysicalDeviceDescriptorHeapFeaturesEXT DescriptorHeapFeatures = []{
	VkPhysicalDeviceDescriptorHeapFeaturesEXT rst{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT};
	rst.descriptorHeap = true;
	return rst;
}();

constexpr VkPhysicalDeviceExtendedDynamicState3FeaturesEXT PhysicalDeviceExtendedDynamicState3Features = []{
	VkPhysicalDeviceExtendedDynamicState3FeaturesEXT rst{
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT
		};
	rst.extendedDynamicState3ColorBlendEnable = true;
	rst.extendedDynamicState3ColorBlendEquation = true;
	rst.extendedDynamicState3ColorWriteMask = true;
	return rst;
}();

const extension_chain extChain{
		PhysicalDeviceVulkan11Features,
		PhysicalDeviceVulkan12Features,
		PhysicalDeviceVulkan13Features,
		PhysicalDeviceVulkan14Features,

		UntypedPointer,

		PhysicalDeviceExtendedDynamicState3Features,

		DescriptorBufferFeatures,
		// DescriptorHeapFeatures,

		MeshShaderFeatures,
	};
}


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


	current_syncs.fence.wait();

	const VkAcquireNextImageInfoKHR acquire_info{
			.sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
			.pNext = nullptr,
			.swapchain = swap_chain,
			.timeout = std::numeric_limits<std::uint64_t>::max(),
			.semaphore = current_syncs.fetch_semaphore,
			.fence = nullptr,
			.deviceMask = 0x1
		};

	const auto result = vkAcquireNextImage2KHR(device, &acquire_info, &imageIndex);

	if(result == VK_ERROR_OUT_OF_DATE_KHR){
		recreate(false);
		return;
	} else if(result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR){
		throw vk_error(result, "failed to acquire swap chain image!");
	}


	current_syncs.fence.reset();

	const auto& frame = swap_chain_frames[imageIndex];

	cmd::submit_command(
		device.primary_graphics_queue(),
		frame.post_command,
		current_syncs.fence,
		current_syncs.fetch_semaphore,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT,
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


	if(!no_fence_wait){
		for(const auto& arr : sync_arr){
			arr.fence.wait();
		}
	} else{
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

				VK_PIPELINE_STAGE_2_TRANSFER_BIT,
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
						.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
						.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
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
						.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
						.srcOffsets = {
							{},
							{
								static_cast<std::int32_t>(image_data.extent.width),
								static_cast<std::int32_t>(image_data.extent.height), 1
							}
						},
						.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
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
				VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				0,
				VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
				subresource_range
			);

			VkImageLayout current_staging_layout = image_data.clear
				                                       ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
				                                       : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			VkAccessFlags2 current_access = image_data.clear
				                                ? VK_ACCESS_2_TRANSFER_WRITE_BIT
				                                : VK_ACCESS_2_TRANSFER_READ_BIT;

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

void context::create_device(){
	auto [devices, rst] = enumerate(vkEnumeratePhysicalDevices, static_cast<VkInstance>(instance));

	if(devices.empty()){
		throw std::runtime_error("Failed to find GPUs with Vulkan support!");
	}

	std::multimap<std::uint32_t, struct physical_device, std::greater<std::uint32_t>> candidates{};

	for(const auto& device : devices){
		auto d = vk::physical_device{device};
		candidates.insert(std::make_pair(d.rate_device(), d));
	}

	for(const auto& [score, device] : candidates){
		if(score && device.valid(surface, device_extensions)){
			physical_device = device;
			break;
		}
	}

	if(!physical_device){
		std::println(std::cerr, "[Vulkan] Failed to find a suitable GPU");
		throw unqualified_error("Failed to find a suitable GPU!");
	} else{
		std::println("[Vulkan] On Physical Device: {}", physical_device.get_name());
	}

	physical_device.cache_properties(surface);

	device = logical_device{
			physical_device, physical_device.queues,
			device_extensions,
			RequiredFeatures::RequiredFeatures, RequiredFeatures::extChain
		};
}

void context::recreate(bool no_fence_wait){
	(void)window_.check_resized();

	while(window_.iconified() || window_.get_size().width == 0 || window_.get_size().height == 0){
		window_.wait_event();
	}

	wait_on_device();


	last_swap_chain.handle = swap_chain;

	createSwapChain();

	for(const auto& reference : eventManager | std::views::values){
		reference(*this, window_instance::resize_event(get_extent()));
	}


	record_post_command(no_fence_wait);
}
}
