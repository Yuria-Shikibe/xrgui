module;

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

export module mo_yanxi.backend.vulkan.context;

import mo_yanxi.vk;
import mo_yanxi.vk.util;
import mo_yanxi.vk.cmd;

import mo_yanxi.backend.glfw.window;

import mo_yanxi.meta_programming;
import mo_yanxi.circular_array;
import mo_yanxi.heterogeneous;
import std;

export import mo_yanxi.vk.universal_handle;

using namespace mo_yanxi::vk;

namespace mo_yanxi::backend::vulkan{

struct InFlightData{
	fence fence{};
	semaphore fetch_semaphore{};
};

struct SwapChainFrameData{
	VkImage image{};
	command_buffer post_command{};

	semaphore flush_semaphore{};
};


/**
 * @return All Required Extensions
 */
std::vector<const char*> get_required_extensions_glfw();

std::vector<const char*> get_required_extensions();

constexpr inline std::array device_extensions{
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
	// VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME,
	VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,
	VK_EXT_MESH_SHADER_EXTENSION_NAME,
	// VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME,
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

			return features;
		}()
	};

constexpr VkPhysicalDeviceVulkan11Features PhysicalDeviceVulkan11Features{
		[]{
			VkPhysicalDeviceVulkan11Features features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};

			features.storageBuffer16BitAccess = true;
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
			features.fragmentStoresAndAtomics = true;
			features.shaderClipDistance = true;

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

constexpr VkPhysicalDeviceExtendedDynamicState3FeaturesEXT PhysicalDeviceExtendedDynamicState3Features{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT,
		.extendedDynamicState3ColorBlendEnable = true,
		.extendedDynamicState3ColorBlendEquation = true
	};

const extension_chain extChain{
		PhysicalDeviceVulkan11Features,
		PhysicalDeviceVulkan12Features,
		PhysicalDeviceVulkan13Features,

		// PhysicalDeviceExtendedDynamicState3Features,
		// PhysicalDeviceComputeShaderDerivativesFeaturesKHR,

		DescriptorBufferFeatures,
		MeshShaderFeatures,
	};
}



export
struct swap_chain_staging_image_data{
	VkImage image{};
	VkExtent2D extent{};

	bool clear{};
	std::uint32_t owner_queue_family{};

	VkPipelineStageFlags2 src_stage{};
	VkAccessFlags2 src_access{};
	VkPipelineStageFlags2 dst_stage{};
	VkAccessFlags2 dst_access{};

	VkImageLayout src_layout{};
	VkImageLayout dst_layout{VK_IMAGE_LAYOUT_UNDEFINED};
};


export
class context{
private:
	instance instance{};

	window_instance window_{};

	physical_device physical_device{};
	logical_device device{};

	allocator allocator_{};
	validation_entry validationEntry{};


	command_pool main_graphic_command_pool{};
	command_pool main_graphic_command_pool_transient{};
	command_pool main_compute_command_pool{};
	command_pool main_compute_command_pool_transient{};

	//Swap Chains
	exclusive_handle_member<VkSwapchainKHR> last_swap_chain{nullptr};
	exclusive_handle_member<VkSwapchainKHR> swap_chain{};
	exclusive_handle_member<VkSurfaceKHR> surface{};
	VkExtent2D swap_chain_extent{};
	VkFormat swapChainImageFormat{};
	std::vector<SwapChainFrameData> swap_chain_frames{};
	circular_array<InFlightData, 3> sync_arr{};
	swap_chain_staging_image_data final_staging_image{};

	string_hash_map<std::move_only_function<void(context&, const window_instance::resize_event&) const>> eventManager{};

	std::vector<void(*)() noexcept> append_disposers{};

public:
	[[nodiscard]] context() = default;

	[[nodiscard]] explicit(false) context(const VkApplicationInfo& app_info){
		init(app_info);
	}

	explicit(false) operator context_info() const noexcept{
		return {instance, physical_device, device};
	}

	void set_staging_image(const swap_chain_staging_image_data& image_data, bool instantly_create_command = true){
		this->final_staging_image = image_data;
		if(instantly_create_command) record_post_command(true);
	}

	void flush();

	void submit_graphics(
		VkCommandBuffer commandBuffer,
		VkFence fence = nullptr,
		VkSemaphore toWait = nullptr,
		VkSemaphore toSignal = nullptr,
		VkPipelineStageFlags wait_before = VK_PIPELINE_STAGE_2_NONE,
		VkPipelineStageFlags signal_after = VK_PIPELINE_STAGE_2_NONE
	) const{
		cmd::submit_command(device.primary_graphics_queue(), commandBuffer, fence, toWait, wait_before, toSignal,
			signal_after);
	}

	void wait_on_graphic() const{
		waitOnQueue(graphic_queue());
	}

	void wait_on_compute() const{
		waitOnQueue(compute_queue());
	}

	void wait_on_device() const;

	[[nodiscard]] VkQueue graphic_queue() const noexcept{
		return device.primary_graphics_queue();
	}

	[[nodiscard]] VkQueue compute_queue() const noexcept{
		return device.primary_compute_queue();
	}


	[[nodiscard]] const command_pool& get_graphic_command_pool() const noexcept{
		return main_graphic_command_pool;
	}

	[[nodiscard]] const command_pool& get_compute_command_pool() const noexcept{
		return main_compute_command_pool;
	}

	[[nodiscard]] const command_pool& get_compute_command_pool_transient() const noexcept{
		return main_compute_command_pool_transient;
	}

	[[nodiscard]] const command_pool& get_graphic_command_pool_transient() const noexcept{
		return main_graphic_command_pool_transient;
	}

	[[nodiscard]] transient_command get_transient_graphic_command_buffer() const noexcept{
		return main_graphic_command_pool_transient.get_transient(graphic_queue());
	}

	[[nodiscard]] transient_command get_transient_compute_command_buffer() const noexcept{
		return main_compute_command_pool_transient.get_transient(compute_queue());
	}

	[[nodiscard]] std::uint32_t graphic_family() const noexcept{
		return physical_device.queues.graphic.index;
	}

	[[nodiscard]] std::uint32_t present_family() const noexcept{
		return physical_device.queues.present.index;
	}

	[[nodiscard]] std::uint32_t compute_family() const noexcept{
		return physical_device.queues.compute.index;
	}


	[[nodiscard]] VkInstance get_instance() const noexcept{
		return instance;
	}

	[[nodiscard]] VkPhysicalDevice get_physical_device() const noexcept{
		return physical_device;
	}

	[[nodiscard]] const logical_device& get_device() const noexcept{
		return device;
	}

	[[nodiscard]] const allocator& get_allocator() const noexcept{
		return allocator_;
	}

	[[nodiscard]] allocator& get_allocator() noexcept{
		return allocator_;
	}

	[[nodiscard]] VkExtent2D get_extent() const noexcept{
		auto sz = window().get_size();
		sz.width = sz.width ? sz.width : 32;
		sz.height = sz.height ? sz.height : 32;
		return sz;
	}

	[[nodiscard]] VkExtent3D get_extent3() const noexcept{
		return {window().get_size().width, window().get_size().height, static_cast<std::uint32_t>(1)};
	}

	[[nodiscard]] VkRect2D get_screen_area() const noexcept{
		return {{}, get_extent()};
	}

	context(const context& other) = delete;
	context(context&& other) noexcept = default;
	context& operator=(const context& other) = delete;
	context& operator=(context&& other) noexcept = default;

	~context(){
		if(device){
			for(auto append_disposer : append_disposers){
				std::invoke(append_disposer);
			}
			vkDestroySwapchainKHR(device, swap_chain, nullptr);
			if(last_swap_chain) vkDestroySwapchainKHR(device, last_swap_chain, nullptr);
		}
		if(instance) vkDestroySurfaceKHR(instance, surface, nullptr);

		swap_chain_frames.clear();
	}

	[[nodiscard]] window_instance& window() noexcept{
		return window_;
	}

	[[nodiscard]] const window_instance& window() const noexcept{
		return window_;
	}

	void register_post_resize(const std::string_view name,
		std::move_only_function<void(context&, const window_instance::resize_event&) const>&& callback){
		auto [itr, suc] = eventManager.try_emplace(name, std::move(callback));
		if(suc){
			itr->second.operator()(*this, window_instance::resize_event{get_extent()});
		}
	}

	void add_dispose(std::convertible_to<void(*)() noexcept> auto fn){
		append_disposers.push_back(fn);
	}

	[[nodiscard]] allocator create_allocator(VmaAllocatorCreateFlags append_flags = 0) const{
		return vk::allocator{
				instance, physical_device, device, append_flags | VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT
			};
	}

	void record_post_command(bool no_fence_wait);

private:
	static void waitOnQueue(VkQueue queue){
		if(const auto rst = vkQueueWaitIdle(queue)){
			throw vk_error{rst, "Wait On Queue Failed"};
		}
	}

	void init(const VkApplicationInfo& app_info){
		auto exts = get_required_extensions();
		instance = vk::instance{app_info, exts};
		validationEntry = validation_entry{instance};

		window_ = window_instance{app_info.pApplicationName};
		surface = window_.create_surface(instance);

		create_device();

		main_compute_command_pool = {get_device(), compute_family(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
		main_graphic_command_pool = {get_device(), graphic_family(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
		main_compute_command_pool_transient = {get_device(), compute_family(), VK_COMMAND_POOL_CREATE_TRANSIENT_BIT};
		main_graphic_command_pool_transient = {get_device(), graphic_family(), VK_COMMAND_POOL_CREATE_TRANSIENT_BIT};

		for(auto& frame_data : sync_arr){
			frame_data.fence = fence{device, true};
			frame_data.fetch_semaphore = semaphore{device};
		}

		allocator_ = vk::allocator{
				instance, physical_device, device,
				VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT | VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT
			};

		createSwapChain();
	}

	void createSwapChain(){
		const swap_chain_info swapChainSupport(physical_device, surface);
		const VkSurfaceFormatKHR surfaceFormat = swap_chain_info::choose_swap_surface_format(swapChainSupport.formats);
		swap_chain_extent = choose_swap_extent(swapChainSupport.capabilities);

		const queue_family_indices indices(physical_device, surface);
		const std::array queueFamilyIndices{indices.graphic.index, indices.present.index};

		std::uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
		if(swapChainSupport.capabilities.maxImageCount){
			imageCount = std::min(imageCount, swapChainSupport.capabilities.maxImageCount);
		}

		VkSwapchainCreateInfoKHR createInfo{
				.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
				.surface = surface,
				.minImageCount = imageCount,
				.imageFormat = surfaceFormat.format,
				.imageColorSpace = surfaceFormat.colorSpace,
				.imageExtent = swap_chain_extent,
				.imageArrayLayers = 1,
				.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			};


		if(indices.graphic.index != indices.present.index){
			createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			createInfo.queueFamilyIndexCount = 2;
			createInfo.pQueueFamilyIndices = queueFamilyIndices.data();
		} else{
			createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			createInfo.queueFamilyIndexCount = 0; // Optional
			createInfo.pQueueFamilyIndices = nullptr; // Optional
		}

		createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
		createInfo.oldSwapchain = last_swap_chain;

		//Set Window Alpha Blend Mode
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

		createInfo.presentMode = swap_chain_info::choose_swap_present_mode(swapChainSupport.presentModes);
		createInfo.clipped = true;

		if(auto rst = vkCreateSwapchainKHR(device, &createInfo, nullptr, swap_chain.as_data())){
			throw vk_error(rst, "Failed to create swap chain!");
		}

		swapChainImageFormat = surfaceFormat.format;

		createImageViews();
	}

	void createImageViews(){
		auto [images, rst] = enumerate(vkGetSwapchainImagesKHR, device.get(), swap_chain.handle);
		swap_chain_frames.resize(images.size());

		for(const auto& [index, imageGroup] : swap_chain_frames | std::ranges::views::enumerate){
			imageGroup.image = images[index];
			imageGroup.flush_semaphore = semaphore{device};
		}
	}

	[[nodiscard]] VkExtent2D choose_swap_extent(const VkSurfaceCapabilitiesKHR& capabilities) const{
		if(capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()){
			return capabilities.currentExtent;
		}

		auto size = window_.get_size();

		size.width = std::clamp(size.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
		size.height = std::clamp(size.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

		return size;
	}

	void create_device(){
		auto [devices, rst] = enumerate(vkEnumeratePhysicalDevices, static_cast<VkInstance>(instance));

		if(devices.empty()){
			throw std::runtime_error("Failed to find GPUs with Vulkan support!");
		}

		// Use an ordered map to automatically sort candidates by increasing score
		std::multimap<std::uint32_t, struct physical_device, std::greater<std::uint32_t>> candidates{};

		for(const auto& device : devices){
			auto d = vk::physical_device{device};
			candidates.insert(std::make_pair(d.rate_device(), d));
		}

		// Check if the best candidate is suitable at all
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
			RequiredFeatures::RequiredFeatures, RequiredFeatures::extChain};
	}

	void recreate(bool no_fence_wait);
};
}
