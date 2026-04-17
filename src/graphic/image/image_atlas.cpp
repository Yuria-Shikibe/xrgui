module;

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cassert>

module mo_yanxi.graphic.image_atlas;

import mo_yanxi.vk.cmd;
import mo_yanxi.platform.thread;

namespace mo_yanxi::graphic{
	void async_image_loader::load(allocated_image_load_description&& desc){
		static constexpr VkDeviceSize cahnnel_size = 4;
		static constexpr std::uint32_t max_prov = 3;

		const auto mipmap_level = std::min(std::min(max_prov, desc.mip_level), desc.desc.get_prov_levels());
		assert(mipmap_level > 0);
		VkDeviceSize maximumsize = desc.region.area();
		maximumsize = vk::get_mipmap_pixels(maximumsize, mipmap_level);
		maximumsize *= cahnnel_size;

		vk::buffer buffer{};
		if(auto itr = stagings.lower_bound(maximumsize); itr != stagings.end()){
			buffer = std::move(stagings.extract(itr).mapped());
		}else{
			constexpr VkDeviceSize chunk_size = 256;
			auto ceil = std::max(std::bit_ceil(maximumsize * 2), chunk_size * chunk_size * mipmap_level);
			if(ceil > 2048 * 2048 * cahnnel_size){
				ceil = chunk_size * (maximumsize / chunk_size + (maximumsize % chunk_size != 0));
			}
			buffer = vk:: buffer{
				async_allocator_,
				{
					.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
					.size = ceil,
					.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
				}, {
					.usage = VMA_MEMORY_USAGE_CPU_TO_GPU,
				}
			};
		}

		vk::command_buffer command_buffer{command_pool_.obtain()};

		{
			vk::scoped_recorder scoped_recorder{command_buffer};
			{
				VkDeviceSize off{};
				for(std::uint32_t mip_lv = 0; mip_lv < mipmap_level; ++mip_lv){
					const auto scl = 1u << mip_lv;
					const VkExtent2D extent{desc.region.width() / scl, desc.region.height() / scl};
					auto bitmap = desc.desc.get(extent.width, extent.height, mip_lv);
					(void)vk::buffer_mapper{buffer}.load_range(bitmap.get_bytes(), static_cast<std::ptrdiff_t>(off));
					off += extent.width * extent.height * cahnnel_size;
				}
			}

			vk::cmd::generate_mipmaps(command_buffer, desc.texture.image, buffer, {
										  static_cast<std::int32_t>(desc.region.src.x),
										  static_cast<std::int32_t>(desc.region.src.y), desc.region.width(),
										  desc.region.height()
									  }, desc.mip_level, mipmap_level, desc.layer_index, 1);
		}

		region_fence_.wait_and_reset();
		vk::cmd::submit_command(working_queue_, command_buffer, region_fence_);

		running_command_buffer_ = std::move(command_buffer);


		if(auto sz = using_buffer_.get_size(); sz > 0){
			stagings.insert({sz, std::exchange(using_buffer_, std::move(buffer))});
		}else{
			using_buffer_ = std::move(buffer);
		}


		if(stagings.size() > 16){
			//TODO since stagings have fixed capacity, use array instead
			//TODO better resource recycle, LRU?
			stagings.clear();
		}

	}

	void async_image_loader::load(const texture_allocation_request& desc){
		vk::command_buffer command_buffer{command_pool_.obtain()};
		vk::texture texture{async_allocator_, desc.extent};

		{
			vk::scoped_recorder scoped_recorder{command_buffer};
			vk::cmd::clear_color(
				command_buffer,
				texture.get_image(),
				desc.clear_color_value,
				{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = texture.get_mip_level(),
					.baseArrayLayer = 0,
					.layerCount = texture.get_layers()
				},
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				VK_ACCESS_NONE,
				VK_ACCESS_NONE,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			);
		}

		vk::cmd::submit_command(working_queue_, command_buffer, allocation_fence_);
		allocation_fence_.wait_and_reset();

		desc.done_ptr->store(std::addressof(texture), std::memory_order_release);
		desc.done_ptr->notify_one();
		desc.done_ptr->wait(std::addressof(texture), std::memory_order_relaxed);

	}

	async_image_loader::async_image_loader(const vk::context_info& context, std::uint32_t graphic_family_index,
		VkQueue working_queue/*, vk::resource_descriptor_heap& target_descriptor_heap*/,
		std::uint32_t target_descriptor_heap_section):
		working_queue_{working_queue},
		async_allocator_(vk::allocator(context, VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT)),
		command_pool_(context.device, graphic_family_index, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT),
		region_fence_(context.device, true),
		allocation_fence_(context.device, false),
		// target_descriptor_heap_(&target_descriptor_heap),
		target_descriptor_heap_section_(target_descriptor_heap_section),
		working_thread([](std::stop_token stop_token, async_image_loader& self){
			work_func(std::move(stop_token), self);
		}, std::ref(*this)){
		platform::set_thread_attributes(working_thread, {
			.name = "xrgui image loader thread",
			.priority = platform::thread_priority::high
		});
	}
}
