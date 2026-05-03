module;

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cassert>
#include <cstring>

module mo_yanxi.graphic.image_atlas;

import mo_yanxi.vk.cmd;
import mo_yanxi.platform.thread;

namespace mo_yanxi::graphic{
void async_image_loader::gpu_work_func(std::stop_token stop_token, async_image_loader& self) try {
	circular_queue<texture_allocation_request> texture_alloc_queue{};

	while (true) {
		texture_alloc_queue.clear();

		{
			std::unique_lock lock(self.gpu_queue_mutex_);

			self.gpu_queue_cond_.wait(lock, [&]{
				return stop_token.stop_requested() || !self.alloc_queue.empty() || !self.prepared_queue_.empty();
			});


			if (!self.alloc_queue.empty()) {
				std::ranges::swap(texture_alloc_queue, self.alloc_queue);
			}
		}

		if (stop_token.stop_requested()) {
			self.discard_texture_alloc_queue_(texture_alloc_queue);
			self.discard_pending_work_();
			break;
		}

		while(auto request = texture_alloc_queue.try_retrieve_front()){
			if(stop_token.stop_requested()){
				async_image_loader::cancel_texture_wait_(request->done_ptr);
				self.on_work_item_finished_();
				self.discard_texture_alloc_queue_(texture_alloc_queue);
				self.discard_pending_work_();
				break;
			}

			self.load(*request);
			self.on_work_item_finished_();
		}

		if(stop_token.stop_requested()){
			break;
		}

		while(auto prepared = self.prepared_queue_.try_consume()){
			if(stop_token.stop_requested()){
				self.on_work_item_finished_();
				self.discard_pending_work_();
				break;
			}
			self.load(std::move(*prepared));
			self.on_work_item_finished_();
		}

		if(stop_token.stop_requested()){
			break;
		}
	}

	self.region_fence_.wait();
} catch(...){
	std::println(std::cerr, "Image GPU Loader Thread Exceptionally Exited");
	throw;
}

void async_image_loader::cpu_work_func(std::stop_token stop_token, async_image_loader& self, unsigned worker_index) try {
	auto& worker = self.cpu_workers_[worker_index];
	while (true) {
		allocated_image_load_description item{};

		{
			std::unique_lock lock(self.load_queue_mutex_);

			self.load_queue_cond_.wait(lock, [&]{
				return !self.load_queue.empty() || stop_token.stop_requested();
			});

			if (stop_token.stop_requested() && self.load_queue.empty()) {
				break;
			}

			item = std::move(self.load_queue.front());
			self.load_queue.pop_front();
		}

		if(stop_token.stop_requested()){
			self.on_work_item_finished_();
			break;
		}

		if(auto* sdf = std::get_if<sdf_load>(&item.desc.raw)){
			std::visit([&](auto& gen){
				using T = std::decay_t<decltype(gen)>;
				if constexpr(std::same_as<T, msdf::msdf_glyph_generator_crop>){
					if(const auto* meta = gen.meta){
						auto* cached = worker.font_cache_.get(meta);
						if(cached == nullptr){
							worker.font_cache_.emplace(meta, font::msdf_handle_wrapper{font::font_face_handle{meta->data(), meta}});
							cached = worker.font_cache_.get(meta);
						}
						if(cached != nullptr){
							gen.msdf_face = cached->msdf_handle;
						}
					}
				}
			}, sdf->generator);
		}

		auto prepared = self.prepare(std::move(item));
		if(!prepared){
			self.on_work_item_finished_();
			break;
		}

		if(stop_token.stop_requested()){
			self.on_work_item_finished_();
			break;
		}

		self.prepared_queue_.push(std::move(*prepared));
		self.gpu_queue_cond_.notify_one();
	}
} catch(...){
	std::println(std::cerr, "Image CPU Worker {} Exceptionally Exited", worker_index);
	throw;
}


std::optional<prepared_image_upload> async_image_loader::prepare(allocated_image_load_description&& desc) const{
		if(stop_requested()){
			return std::nullopt;
		}

		static constexpr std::uint32_t max_prov = 3;

		const auto mipmap_level = std::min(std::min(max_prov, desc.mip_level), desc.desc.get_prov_levels());
		assert(mipmap_level > 0);

		prepared_image_upload prepared{
			.texture = std::move(desc.texture),
			.mip_level = desc.mip_level,
			.layer_index = desc.layer_index,
			.region = desc.region,
		};
		prepared.mip_data.reserve(mipmap_level);

		for(std::uint32_t mip_lv = 0; mip_lv < mipmap_level; ++mip_lv){
			if(stop_requested()){
				return std::nullopt;
			}

			const auto scl = 1u << mip_lv;
			const VkExtent2D extent{desc.region.width() / scl, desc.region.height() / scl};
			auto bitmap = desc.desc.get(extent.width, extent.height, mip_lv);
			if(stop_requested()){
				return std::nullopt;
			}
			auto bytes = bitmap.get_bytes();
			std::vector<std::byte> level_data(bytes.size());
			std::memcpy(level_data.data(), bytes.data(), bytes.size());
			prepared.mip_data.push_back(std::move(level_data));
		}

		return prepared;
	}

	void async_image_loader::load(prepared_image_upload&& desc){
		if(stop_requested())return;

		static constexpr VkDeviceSize cahnnel_size = 4;

		const auto mipmap_level = static_cast<std::uint32_t>(desc.mip_data.size());
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
					if(stop_requested())return;

					const auto& level_data = desc.mip_data[mip_lv];
					(void)vk::buffer_mapper{buffer}.load_range(std::span{level_data}, static_cast<std::ptrdiff_t>(off));
					off += static_cast<VkDeviceSize>(level_data.size());
				}
			}

			if(stop_requested())return;

			vk::cmd::generate_mipmaps(command_buffer, desc.texture.image, buffer, {
									  static_cast<std::int32_t>(desc.region.src.x),
										  static_cast<std::int32_t>(desc.region.src.y), desc.region.width(),
										  desc.region.height()
								  }, desc.mip_level, mipmap_level, desc.layer_index, 1);
		}

		if(stop_requested())return;

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
		if(stop_requested()){
			cancel_texture_wait_(desc.done_ptr);
			return;
		}

		vk::command_buffer command_buffer{command_pool_.obtain()};
		vk::texture texture{async_allocator_, desc.extent};

		if(stop_requested()){
			cancel_texture_wait_(desc.done_ptr);
			return;
		}

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

		if(stop_requested()){
			cancel_texture_wait_(desc.done_ptr);
			return;
		}

		vk::cmd::submit_command(working_queue_, command_buffer, allocation_fence_);
		allocation_fence_.wait_and_reset();

		if(stop_requested()){
			cancel_texture_wait_(desc.done_ptr);
			return;
		}

		desc.done_ptr->store(std::addressof(texture), std::memory_order_release);
		desc.done_ptr->notify_one();
		desc.done_ptr->wait(std::addressof(texture), std::memory_order_relaxed);

	}

	async_image_loader::async_image_loader(
		const vk::context_info& context,
		std::uint32_t graphic_family_index,
		VkQueue working_queue/*, vk::resource_descriptor_heap& target_descriptor_heap*/,
		std::uint32_t target_descriptor_heap_section):
		working_queue_{working_queue},
		async_allocator_(vk::allocator(context, VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT)),
		command_pool_(context.device, graphic_family_index, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT),
		region_fence_(context.device, true),
		allocation_fence_(context.device, false),
		// target_descriptor_heap_(&target_descriptor_heap),
		target_descriptor_heap_section_(target_descriptor_heap_section){
		const auto cpu_worker_count = calculate_cpu_worker_count();
		cpu_workers_.reserve(cpu_worker_count);
		for(unsigned i = 0; i < cpu_worker_count; ++i){
			auto& worker = cpu_workers_.emplace_back();
			worker.work_thread = std::jthread([](std::stop_token stop_token, async_image_loader& self, unsigned worker_index){
				cpu_work_func(std::move(stop_token), self, worker_index);
			}, std::ref(*this), i);
			platform::set_thread_attributes(worker.work_thread, {
				.name = std::format("xrgui image cpu worker {}", i),
				.priority = platform::thread_priority::high
			});
		}

		gpu_thread_ = std::jthread([](std::stop_token stop_token, async_image_loader& self){
			gpu_work_func(std::move(stop_token), self);
		}, std::ref(*this));
		platform::set_thread_attributes(gpu_thread_, {
			.name = "xrgui image gpu thread",
			.priority = platform::thread_priority::high
		});
	}
}
