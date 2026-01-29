module;

#include <vulkan/vulkan.h>
#include <cassert>
#include <vk_mem_alloc.h>

#ifndef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
#include "plf_hive.h"
#endif


export module mo_yanxi.graphic.image_atlas;

export import mo_yanxi.graphic.image_atlas.util;
export import mo_yanxi.graphic.image_region;
export import mo_yanxi.graphic.color;
export import mo_yanxi.graphic.bitmap;

import mo_yanxi.referenced_ptr;
import mo_yanxi.meta_programming;
import mo_yanxi.handle_wrapper;
import mo_yanxi.allocator2d;



import mo_yanxi.vk;
import mo_yanxi.vk.util;
import mo_yanxi.vk.cmd;
import mo_yanxi.vk.universal_handle;

import mo_yanxi.heterogeneous;
import mo_yanxi.concurrent.condition_variable_single;
import mo_yanxi.concurrent.mpsc_queue;
import mo_yanxi.utility;

export import mo_yanxi.graphic.msdf;
import mo_yanxi.io.image;

import std;


#ifdef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
import <plf_hive.h>;
#endif


namespace mo_yanxi::graphic{
constexpr math::usize2 DefaultTexturePageSize = math::vectors::constant2<std::uint32_t>::base_vec2 * (4096);

using region_type = combined_image_region<size_awared_uv<uniformed_rect_uv>>;


export
struct bitmap_represent{
	using byte_span = std::span<const std::byte>;

	std::variant<byte_span, bitmap> present{};

	[[nodiscard]] constexpr bitmap_represent() = default;

	template <typename T>
		requires std::constructible_from<decltype(present), T>
	[[nodiscard]] constexpr explicit(false) bitmap_represent(T&& present)
	: present(std::forward<T>(present)){
	}

	[[nodiscard]] constexpr byte_span get_bytes() const noexcept{
		return std::visit([] <typename T>(const T& data){
			if constexpr(std::same_as<T, bitmap>){
				const std::span<const bitmap::value_type> s = data.to_span();
				return byte_span{reinterpret_cast<const std::byte*>(s.data()), s.size_bytes()};
			} else{
				return data;
			}
		}, present);
	}
};

export
struct bitmap_path_load{
	std::string path{};

	[[nodiscard]] math::usize2 get_extent() const{
		const auto ext = io::image::read_image_extent(path.c_str());
		if(!ext){
			throw bad_image_asset{};
		}
		return std::bit_cast<math::usize2>(ext.value());
	}

	bitmap_represent operator()(unsigned w, unsigned h, unsigned level) const{
		return load_bitmap(path);
	}
};

export
struct bitmap_load{
	bitmap bitmap{};

	[[nodiscard]] math::usize2 get_extent() const{
		return bitmap.extent();
	}

	bitmap_represent operator()(unsigned w, unsigned h, unsigned level) const{
		if(!bitmap){
			return bitmap_represent{};
		}
		return bitmap.to_byte_span();
	}
};

export
struct sdf_load{
	std::variant<
		msdf::msdf_generator,
		msdf::msdf_glyph_generator_crop
	> generator{};

	std::optional<math::usize2> extent{};
	std::uint32_t prov_levels{3};

	[[nodiscard]] math::usize2 get_extent() const {
		return extent.value_or(std::visit<math::usize2>(overload{
			[this](const msdf::msdf_generator& generator){
				const auto sz = generator.get_extent();

				static constexpr math::vec2 minimum{64, 64};
				static constexpr auto targetRatio = minimum.slope();
				const auto sourceRatio = sz.slope();
				const auto scale = std::max(targetRatio < sourceRatio ? minimum.x / sz.x : minimum.y / sz.y, 1.f);
				return (sz * scale).ceil().as<unsigned>();
			},
			[this](const msdf::msdf_glyph_generator_crop& generator){
				return math::usize2{generator.font_w, generator.font_h};
			}
		}, generator));
	}

	bitmap_represent operator()(unsigned w, unsigned h, unsigned level) const{
		return std::visit<bitmap>([&]<vk::texture_source_prov T>(const T& prov){
			return prov.operator()(w, h, level);
		}, generator);
	}
};

export
struct image_load_description{
	std::variant<bitmap_path_load, sdf_load, bitmap_load> raw{};

	[[nodiscard]] math::usize2 get_extent() const{
		return std::visit([](const auto& v){
			return v.get_extent();
		}, raw);
	}

	[[nodiscard]] std::uint32_t get_prov_levels() const noexcept{
		return std::visit([]<typename T>(const T& t) -> std::uint32_t{
			if constexpr(std::same_as<T, sdf_load>){
				return t.prov_levels;
			} else{
				return 1;
			}
		}, raw);
	}

	[[nodiscard]] bitmap_represent get(unsigned w, unsigned h, unsigned level) const{
		return std::visit<bitmap_represent>([&]<typename T>(const T& gen){
			return gen(w, h, level);
		}, raw);
	}
};

export
struct allocated_image_load_description{
	vk::image_handle texture{};
	std::uint32_t mip_level{};
	std::uint32_t layer_index{};

	image_load_description desc{};
	math::urect region{};
};

struct texture_allocation_request{
	VkExtent2D extent;
	VkClearColorValue clear_color_value;
	std::atomic<vk::texture*>* done_ptr;
};

struct async_loader_task{
	using variant_t = std::variant<allocated_image_load_description, texture_allocation_request>;
	variant_t task;
};

struct async_image_loader{
private:
	VkQueue working_queue_{};
	vk::allocator async_allocator_{};
	vk::command_pool command_pool_{};
	vk::command_buffer running_command_buffer_{};
	vk::fence region_fence_{};
	vk::fence allocation_fence_{};

	vk::buffer using_buffer_{};

	std::multimap<VkDeviceSize, vk::buffer> stagings{};

	std::mutex region_queue_mutex{};
	ccur::condition_variable_single region_queue_cond{};
	std::vector<async_loader_task> region_queue{};


	std::atomic_flag loading{};

	std::jthread working_thread{};

	static void work_func(std::stop_token stop_token, async_image_loader& self){
		std::vector<async_loader_task> dumped_queue{};

		while(!stop_token.stop_requested()){
			{
				std::unique_lock lock(self.region_queue_mutex);

				self.region_queue_cond.wait(lock, [&]{
					return !self.region_queue.empty() || stop_token.stop_requested();
				});

				dumped_queue = std::exchange(self.region_queue, {});
			}

			self.loading.test_and_set(std::memory_order::relaxed);
			for(auto& desc : dumped_queue){
				if(stop_token.stop_requested()) break;
				std::visit(overload{
					[&self](allocated_image_load_description& desc){
						self.load(std::move(desc));
					},
					[&self](texture_allocation_request& desc){
						self.load(desc);
					},
				}, desc.task);
			}
			self.loading.clear(std::memory_order::release);
			self.loading.notify_all();
		}

		self.region_fence_.wait();
	}

	void load(allocated_image_load_description&& desc);

	void load(const texture_allocation_request& desc);

public:
	[[nodiscard]] async_image_loader() = default;

	[[nodiscard]] explicit async_image_loader(
		const vk::context_info& context,
		std::uint32_t graphic_family_index,
		VkQueue working_queue
		)
	:
	working_queue_{working_queue},
	async_allocator_(vk::allocator(context, VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT)),
	command_pool_(context.device, graphic_family_index, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT),
	region_fence_(context.device, true),
	allocation_fence_(context.device, false),
	working_thread([](std::stop_token stop_token, async_image_loader& self){
		work_func(std::move(stop_token), self);
	}, std::ref(*this)){
	}

	void push(allocated_image_load_description&& desc){
		{
			std::lock_guard lock(region_queue_mutex);
			region_queue.emplace_back(std::move(desc));
		}
		region_queue_cond.notify_one();
	}

	void push(const texture_allocation_request& desc){
		{
			std::lock_guard lock(region_queue_mutex);
			region_queue.emplace_back(desc);
		}
		region_queue_cond.notify_one();
	}

	void wait() const noexcept{
		if(working_thread.joinable()){
			loading.wait(true, std::memory_order::acquire);
		}
	}

	[[nodiscard]] bool is_loading() const noexcept{
		return loading.test(std::memory_order::relaxed);
	}

	~async_image_loader(){
		working_thread.request_stop();
		region_queue_cond.notify_one();
		wait();
		region_fence_.wait();
	}
};

void prepare_command(VkCommandBuffer commandBuffer){
	static constexpr VkCommandBufferBeginInfo BeginInfo{
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		nullptr,
		VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};
	vkBeginCommandBuffer(commandBuffer, &BeginInfo);

}

void submit_command(VkCommandBuffer commandBuffer, VkQueue queue){
	vkEndCommandBuffer(commandBuffer);

	const VkSubmitInfo submitInfo{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &commandBuffer,
	};

	vkQueueSubmit(queue, 1, &submitInfo, nullptr);
}

struct image_register_result{
	allocated_image_region& region;
	bool inserted;

	image_register_result(allocated_image_region& region, bool inserted)
		: region(region),
		inserted(inserted){
		region.ref_incr();
	}

	~image_register_result(){
		region.ref_decr();
	}

	image_register_result(const image_register_result& other) = delete;
	image_register_result(image_register_result&& other) noexcept = delete;
	image_register_result& operator=(const image_register_result& other) = delete;
	image_register_result& operator=(image_register_result&& other) noexcept = delete;
};

export
struct image_page{
private:
	async_image_loader* loader_{};

	math::usize2 page_size_{};

	std::mutex subpage_mtx_{};

	plf::hive<sub_page> subpages_{};
	std::atomic<vk::texture*> ptr_to_texture_temp_{nullptr};
	std::atomic<sub_page*> task_post_lock_{};

	VkClearColorValue clear_color_{};
	std::uint32_t margin{};

	//TODO using shared mutex instead?
	std::mutex named_image_regions_mtx_{};
	string_hash_map<allocated_image_region> named_image_regions{};

	std::mutex protected_regionss_mtx_{};
	std::unordered_set<allocated_image_region*> protected_regions{};

public:
	[[nodiscard]] image_page() = default;

	[[nodiscard]] explicit image_page(
		async_image_loader& loader,
		const math::usize2 page_size = DefaultTexturePageSize,
		const VkClearColorValue& clear_color = {},
		const std::uint32_t margin = 4)
	:
	loader_(&loader),
	page_size_(page_size),
	clear_color_(clear_color),
	margin(margin){
		loader_->push(texture_allocation_request{
					.extent = {page_size_.x, page_size_.y},
					.clear_color_value = clear_color_,
					.done_ptr = &ptr_to_texture_temp_
				});

		sub_page* sub_page = &*subpages_.emplace(page_size_);
		ptr_to_texture_temp_.wait(nullptr, std::memory_order::relaxed);
		sub_page->texture = std::move(*ptr_to_texture_temp_.load(std::memory_order::acquire));

		ptr_to_texture_temp_.store(nullptr, std::memory_order_release);
		ptr_to_texture_temp_.notify_one();
	}

private:
	allocated_image_region async_load(image_load_description&& desc, page_acquire_result&& result) const{
		loader_->push({
				.texture = *result.handle,
				.mip_level = result.handle->get_mip_level(),
				.layer_index = 0,
				.desc = std::move(desc),
				.region = result.region.get_region()
			});

		return std::move(result.region);
	}

public:
	allocated_image_region async_allocate(
		image_load_description&& desc
	){
		const auto extent = desc.get_extent();
		if(extent.x > page_size_.x || extent.y > page_size_.y){
			throw bad_image_allocation{};
		}

		while(true){
			//TODO optimize the mutex
			{
				std::lock_guard _{subpage_mtx_};
				for(auto& subpass : subpages_){
					if(auto rst = subpass.acquire(extent, margin)){
						return async_load(std::move(desc), std::move(rst.value()));
					}
				}
			}

			clear_unused();

			{
				std::lock_guard _{subpage_mtx_};
				for(auto& subpass : subpages_){
					if(auto rst = subpass.acquire(extent, margin)){
						return async_load(std::move(desc), std::move(rst.value()));
					}
				}
			}

			sub_page* sub_page{};
			if(task_post_lock_.exchange(nullptr, std::memory_order_relaxed)){
				try{
					loader_->push(texture_allocation_request{
						.extent = {page_size_.x, page_size_.y},
						.clear_color_value = clear_color_,
						.done_ptr = &ptr_to_texture_temp_
					});

					{
						std::lock_guard _{subpage_mtx_};
						sub_page = &*subpages_.emplace(page_size_);
					}
				}catch(...){
					task_post_lock_.store(&*subpages_.rbegin(), std::memory_order_release);
					throw;
				}

				ptr_to_texture_temp_.wait(nullptr, std::memory_order::relaxed);
				sub_page->texture = std::move(*ptr_to_texture_temp_.load(std::memory_order::acquire));
				ptr_to_texture_temp_.store(nullptr, std::memory_order_release);
				ptr_to_texture_temp_.notify_one();

				task_post_lock_.store(&*subpages_.rbegin(), std::memory_order_release);
			}else{
				while(task_post_lock_.compare_exchange_strong(sub_page, nullptr, std::memory_order_relaxed, std::memory_order_acquire)){}
			}

			if(auto rst = sub_page->acquire(extent, margin)){
				return async_load(std::move(desc), std::move(rst.value()));
			}
		}
	}

	template <typename Str, typename T>
		requires (std::constructible_from<image_load_description, T> && std::constructible_from<std::string_view, const Str&>)
	image_register_result register_named_region(
		Str&& name,
		T&& desc,
		const bool mark_as_protected = false){
		std::string_view sv{std::as_const(name)};

		allocated_image_region* rst;

		{
			std::lock_guard lg{named_image_regions_mtx_};
			if(const auto itr = named_image_regions.find(sv); itr != named_image_regions.end()){
				return {itr->second, false};
			}

			rst = &named_image_regions.try_emplace(std::forward<Str>(name), this->async_allocate(image_load_description{std::forward<T>(desc)})).first->second;
		}

		if(mark_as_protected){
			mark_protected(sv);
		}

		return {*rst, true};
	}

	bool mark_protected(const std::string_view localName) noexcept{
		allocated_image_region* page;
		{
			std::lock_guard lg{named_image_regions_mtx_};
			page = named_image_regions.try_find(localName);
		}

		if(page){
			bool inserted;
			{
				std::lock_guard lg{protected_regionss_mtx_};
				inserted = protected_regions.insert(page).second;
			}

			if(inserted){
				//not inserted
				page->ref_incr();
				return true;
			}

			//already marked
			return true;
		}

		return false;
	}

	bool mark_unprotected(const std::string_view localName) noexcept{
		allocated_image_region* page;
		{
			std::lock_guard lg{named_image_regions_mtx_};
			page = named_image_regions.try_find(localName);
		}

		if(page){
			bool erased;
			{
				std::lock_guard lg{protected_regionss_mtx_};
				erased = protected_regions.erase(page);
			}

			if(erased){
				page->ref_decr();
				return true;
			}
		}

		return false;
	}

	void clear_unused() noexcept{
		std::lock_guard lg{named_image_regions_mtx_};

		auto cur = named_image_regions.begin();
		while(cur != named_image_regions.end()){
			auto check = cur->second.check_droppable_and_retire();
			if(check){
				cur = named_image_regions.erase(cur);
			}
		}
	}

	template <typename T>
	[[nodiscard]] auto* find(this T& self, const std::string_view localName) noexcept{
		std::lock_guard lg{self.named_image_regions_mtx_};
		return self.named_image_regions.try_find(localName);
	}

	template <typename T>
	[[nodiscard]] auto& at(this T& self, const std::string_view localName){
		std::lock_guard lg{self.named_image_regions_mtx_};
		return self.named_image_regions.at(localName);
	}

	template <typename T>
	[[nodiscard]] auto& operator[](this T& self, const std::string_view localName){
		std::lock_guard lg{self.named_image_regions_mtx_};
		return self.named_image_regions.at(localName);
	}


	~image_page(){
		drop();
	}

protected:
	void drop(){
		std::lock_guard _{protected_regionss_mtx_};
		for (auto& protected_region : protected_regions){
			protected_region->ref_decr();
		}
		if(!subpages_.empty())clear_unused();
	}
};

export
struct image_atlas{
private:
	std::unique_ptr<async_image_loader> async_image_loader_{};
	string_hash_map<image_page> pages{};

public:
	[[nodiscard]] image_atlas() = default;

	[[nodiscard]] image_atlas(
	const vk::context_info& ctx_info,
	std::uint32_t graphic_family_index,
	VkQueue loader_working_queue
	) :
	async_image_loader_{std::make_unique<async_image_loader>(ctx_info, graphic_family_index, loader_working_queue)}{
	}

	image_page& create_image_page(
		const std::string_view name,
		const VkClearColorValue& clearColor = {},
		const math::usize2 size = DefaultTexturePageSize,
		const std::uint32_t margin = 4){
		return pages.try_emplace(name, *async_image_loader_, size, clearColor, margin).first->second;
	}

	image_page& operator[](const std::string_view name){
		if(auto itr = pages.find(name); itr != pages.end()){
			return itr->second;
		}else{
			return create_image_page(name);
		}

	}

	void wait_load() const noexcept{
		async_image_loader_->wait();
	}

	[[nodiscard]] bool is_loading() const noexcept{
		return async_image_loader_->is_loading();
	}

	void clean_unused() noexcept{
		for(auto& page : pages | std::views::values){
			page.clear_unused();
		}
	}

	template <typename T>
	[[nodiscard]] image_page* find_page(this T& self, const std::string_view name) noexcept{
		return self.pages.try_find(name);
	}

	template <typename T>
	allocated_image_region* find(this T& self, const std::string_view name_category_local) noexcept{
		const auto [category, localName] = splitKey(name_category_local);
		if(const auto page = self.find_page(category)){
			return page->find(localName);
		}

		return nullptr;
	}

	template <typename T>
	allocated_image_region& at(this T& self, const std::string_view name_category_local){
		const auto [category, localName] = splitKey(name_category_local);
		if(const auto page = self.find_page(category)){
			return *page->find(localName);
		}

		// std::println(std::cerr, "TextureRegion Not Found: {}", name_category_local);
		throw std::out_of_range("Undefined Region Name");
	}


private:
	static std::pair<std::string_view, std::string_view> splitKey(const std::string_view name){
		const auto pos = name.find('.');
		const std::string_view category = name.substr(0, pos);
		const std::string_view localName = name.substr(pos + 1);

		return {category, localName};
	}

public:
	image_atlas(const image_atlas& other) = delete;
	image_atlas(image_atlas&& other) noexcept = default;
	image_atlas& operator=(const image_atlas& other) = delete;
	image_atlas& operator=(image_atlas&& other) noexcept = default;

	~image_atlas() = default;
};

}
