module;

#include <vulkan/vulkan.h>
#include <cassert>
#include <vk_mem_alloc.h>

#ifndef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
#include "plf_hive.h"
#include <gtl/phmap.hpp>
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
import mo_yanxi.circular_queue;



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
import <gtl/phmap.hpp>;
#endif


namespace mo_yanxi::graphic{
#pragma region PRL_HashMap

struct StringHashEq {
	using is_transparent = void;
	static std::size_t operator()(std::string_view sv) noexcept {
		return gtl::HashState().combine(0, sv);
	}

	static bool operator()(std::string_view l, std::string_view r) noexcept {
		return l == r;
	}
};

template <typename V>
using concurrent_node_string_map = gtl::parallel_node_hash_map<
	std::string, V,
	StringHashEq, StringHashEq
>;

#pragma endregion
constexpr math::usize2 DefaultTexturePageSize = math::vectors::constant2<std::uint32_t>::base_vec2 * (4096);

using region_type = combined_image_region<size_awared_uv<uniformed_rect_uv>>;

#pragma region LoadSpec

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

struct prepared_image_upload{
	vk::image_handle texture{};
	std::uint32_t mip_level{};
	std::uint32_t layer_index{};
	math::urect region{};
	std::vector<std::vector<std::byte>> mip_data{};
};

struct async_loader_task{
	using variant_t = std::variant<allocated_image_load_description, texture_allocation_request>;
	variant_t task;
};

#pragma endregion

export struct image_atlas;

struct async_image_loader{
	friend struct image_atlas;

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


	circular_queue<texture_allocation_request> alloc_queue{};
	circular_queue<allocated_image_load_description> load_queue{};
	std::atomic_bool loading_flag_{};

	// vk::resource_descriptor_heap* target_descriptor_heap_{};
	std::uint32_t target_descriptor_heap_section_{};

	std::jthread working_thread{};

	static void work_func(std::stop_token stop_token, async_image_loader& self) try {
		circular_queue<texture_allocation_request> texture_alloc_queue{};
		std::vector<allocated_image_load_description> image_load_queue{};
		image_load_queue.reserve(16);

		while (true) {
			image_load_queue.clear();
			texture_alloc_queue.clear();

			{
				std::unique_lock lock(self.region_queue_mutex);

				self.region_queue_cond.wait(lock, [&]{
					return !self.alloc_queue.empty() || !self.load_queue.empty() || stop_token.stop_requested();
				});


				if (stop_token.stop_requested() && self.alloc_queue.empty() && self.load_queue.empty()) {
					break;
				}


				if (!self.alloc_queue.empty()) {
					std::ranges::swap(texture_alloc_queue, self.alloc_queue);
				}

				if (!self.load_queue.empty()) {
					auto count = std::min(self.load_queue.size(), image_load_queue.capacity());
					for(unsigned i = 0; i < count; ++i){
						image_load_queue.push_back(std::move(self.load_queue.front()));
						self.load_queue.pop_front();
					}
				}
			}

			if(!texture_alloc_queue.empty() && stop_token.stop_requested()){
				break;
			}
			self.loading_flag_.store(true, std::memory_order::release);

			texture_alloc_queue.for_each([&](auto, texture_allocation_request& r){
				self.load(r);
			});

			if(stop_token.stop_requested()){
				self.loading_flag_.store(false, std::memory_order::release);
				self.loading_flag_.notify_all();
				break;
			}

			for (auto && alloc : image_load_queue){
				self.load(self.prepare(std::move(alloc)));
			}

			self.loading_flag_.store(false, std::memory_order::release);
			self.loading_flag_.notify_all();
		}

		self.region_fence_.wait();
	} catch(...){
		std::println(std::cerr, "Loader Thread Exceptionally Exited");
		throw;
	}

	[[nodiscard]] prepared_image_upload prepare(allocated_image_load_description&& desc) const;

	void load(prepared_image_upload&& desc);

	void load(const texture_allocation_request& desc);

public:
	[[nodiscard]] async_image_loader() = default;

	[[nodiscard]] explicit async_image_loader(
		const vk::context_info& context,
		std::uint32_t graphic_family_index,
		VkQueue working_queue,

		// vk::resource_descriptor_heap& target_descriptor_heap,
		std::uint32_t target_descriptor_heap_section
	);

	std::uint32_t add_image_to_heap(VkImageViewCreateInfo create_info, VkImageLayout image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VkDescriptorType type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) const{
		return 0;
		// return target_descriptor_heap_->push_back_images(target_descriptor_heap_section_, std::span(&create_info, 1), image_layout, type);
	}

	void push(allocated_image_load_description&& desc){
		{
			std::lock_guard lock(region_queue_mutex);
			load_queue.emplace_back(std::move(desc));
		}
		region_queue_cond.notify_one();
	}

	void push(const texture_allocation_request& desc){
		{
			std::lock_guard lock(region_queue_mutex);
			alloc_queue.push_back(desc);
		}
		region_queue_cond.notify_one();
	}

public:
	void wait() const noexcept{
		if(working_thread.joinable()){
			loading_flag_.wait(true, std::memory_order::acquire);
		}
	}

	[[nodiscard]] bool is_loading() const noexcept{
		return loading_flag_.load(std::memory_order::relaxed);
	}

	~async_image_loader(){
		working_thread.request_stop();
		region_queue_cond.notify_one();
		wait();
		region_fence_.wait();
	}
};


struct image_register_result{
	allocated_image_region& region;
	bool inserted;

	image_register_result(allocated_image_region& region, bool inserted)
		: region(region),
		inserted(inserted){
		region.ref_incr();
	}

	image_register_result(allocated_image_region& region, bool inserted, std::adopt_lock_t)
		: region(region),
		inserted(inserted){
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
struct image_page_config{
	math::usize2 extent{DefaultTexturePageSize};
	VkClearColorValue clear_color{};
	VkFormat format{};
	std::uint32_t margin{4};
};

export
struct image_page{
private:
	async_image_loader* loader_{};

	std::mutex subpage_mtx_{};

	plf::hive<sub_page> subpages_{};
	std::atomic<vk::texture*> ptr_to_texture_temp_{nullptr};
	std::atomic<sub_page*> task_post_lock_{};

	image_page_config config_{};

	concurrent_node_string_map<allocated_image_region> named_image_regions{};

public:
	[[nodiscard]] image_page() = default;

	//TODO support format spec

	[[nodiscard]] explicit image_page(
		async_image_loader& loader,
		image_page_config config = {})
	:
	loader_(&loader),
	config_(config){
		loader_->push(texture_allocation_request{
					.extent = {config_.extent.x, config_.extent.y},
					.clear_color_value = config_.clear_color,
					.done_ptr = &ptr_to_texture_temp_
				});

		sub_page* sub_page = &*subpages_.emplace(config_.extent);
		ptr_to_texture_temp_.wait(nullptr, std::memory_order::relaxed);
		sub_page->texture = std::move(*ptr_to_texture_temp_.load(std::memory_order::acquire));
		ptr_to_texture_temp_.store(nullptr, std::memory_order_release);
		ptr_to_texture_temp_.notify_one();

		task_post_lock_.store(sub_page, std::memory_order_release);

		sub_page->heap_target_index = loader_->add_image_to_heap(VkImageViewCreateInfo{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = sub_page->texture.get_image(),
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = sub_page->texture.get_format(),
			.components = {},
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = sub_page->texture.get_mip_level(),
				.baseArrayLayer = 0,
				.layerCount = sub_page->texture.get_layers()
			}
		});

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
		if(extent.x + config_.margin > config_.extent.x || extent.y + config_.margin > config_.extent.y){
			throw bad_image_allocation{};
		}

		while(true){
			//TODO optimize the mutex
			{
				std::lock_guard _{subpage_mtx_};
				for(auto& subpass : subpages_){
					if(auto rst = subpass.acquire(extent, config_.margin)){
						return async_load(std::move(desc), std::move(rst.value()));
					}
				}
			}

			clear_unused();

			{
				std::lock_guard _{subpage_mtx_};
				for(auto& subpass : subpages_){
					if(auto rst = subpass.acquire(extent, config_.margin)){
						return async_load(std::move(desc), std::move(rst.value()));
					}
				}
			}

			sub_page* sub_page{};
			if(task_post_lock_.exchange(nullptr, std::memory_order_relaxed)){
				try{

					{
						std::lock_guard _{subpage_mtx_};
						sub_page = std::to_address(subpages_.emplace(config_.extent));
					}


					loader_->push(texture_allocation_request{
							.extent = {config_.extent.x, config_.extent.y},
							.clear_color_value = config_.clear_color,
							.done_ptr = &ptr_to_texture_temp_
						});


					ptr_to_texture_temp_.wait(nullptr, std::memory_order::relaxed);
					sub_page->texture = std::move(*ptr_to_texture_temp_.load(std::memory_order::acquire));
					ptr_to_texture_temp_.store(nullptr, std::memory_order_release);
					ptr_to_texture_temp_.notify_one();


					sub_page->heap_target_index = loader_->add_image_to_heap(VkImageViewCreateInfo{
							.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
							.image = sub_page->texture.get_image(),
							.viewType = VK_IMAGE_VIEW_TYPE_2D,
							.format = sub_page->texture.get_format(),
							.components = {},
							.subresourceRange = {
								.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
								.baseMipLevel = 0,
								.levelCount = sub_page->texture.get_mip_level(),
								.baseArrayLayer = 0,
								.layerCount = sub_page->texture.get_layers()
							}
						});


					task_post_lock_.store(&*subpages_.rbegin(), std::memory_order_release);
					task_post_lock_.notify_all();
				} catch(...){

					task_post_lock_.store(&*subpages_.rbegin(), std::memory_order_release);
					task_post_lock_.notify_all();
					throw;
				}
			} else{


				sub_page = task_post_lock_.load(std::memory_order_acquire);
				while(sub_page == nullptr){
					task_post_lock_.wait(nullptr, std::memory_order_relaxed);
					sub_page = task_post_lock_.load(std::memory_order_acquire);
				}
			}

			if(auto rst = sub_page->acquire(extent, config_.margin)){
				return async_load(std::move(desc), std::move(rst.value()));
			}
		}
	}

	template <typename T>
		requires (std::constructible_from<image_load_description, T>)
	image_register_result register_named_region(
		std::string_view name,
		T&& desc,
		const bool mark_as_protected = false) {

		allocated_image_region* rst = nullptr;


		named_image_regions.if_contains(name, [&](decltype(named_image_regions)::value_type& pair) {
			rst = &pair.second;
		});

		if (rst != nullptr) {
			if (mark_as_protected) rst->set_protected(true);
			return {*rst, false};
		}

		auto val = this->async_allocate(image_load_description{std::forward<T>(desc)});


		val.ref_incr();
		if (mark_as_protected) {
			val.set_protected(true);
		}

		bool inserted = named_image_regions.try_emplace_l(
			name,
			[&](decltype(named_image_regions)::value_type& pair) {
				rst = &pair.second;
			},
			std::move(val)
		);

		if (inserted) {
			named_image_regions.if_contains(name, [&](decltype(named_image_regions)::value_type& pair) {
				rst = &pair.second;
			});
			assert(rst != nullptr);
			return image_register_result{*rst, true, std::adopt_lock};
		} else {
			if (mark_as_protected) rst->set_protected(true);
			assert(rst != nullptr);
			return {*rst, false};
		}
	}

	void clear_unused() noexcept {
		std::vector<std::string> keys_to_remove;

		named_image_regions.for_each([&](const decltype(named_image_regions)::value_type& pair) {
			if (pair.second.droppable()) {
				keys_to_remove.push_back(pair.first);
			}
		});

		for (const auto& key : keys_to_remove) {
			named_image_regions.erase_if(key, [](decltype(named_image_regions)::value_type& pair) {
				return pair.second.check_droppable_and_retire();
			});
		}
	}

	template <typename T>
	[[nodiscard]] auto* find(this T& self, const std::string_view localName) noexcept {
		allocated_image_region* rst = nullptr;
		self.named_image_regions.if_contains(localName, [&](auto& pair) {
			rst = &pair.second;
		});
		return rst;
	}

	template <typename T>
	[[nodiscard]] auto& at(this T& self, const std::string_view localName) {
		allocated_image_region* rst = nullptr;
		self.named_image_regions.if_contains(localName, [&](auto& pair) {
			rst = &pair.second;
		});
		if (!rst) throw std::out_of_range("Key not found");
		return *rst;
	}

	template <typename T>
	[[nodiscard]] auto& operator[](this T& self, const std::string_view localName){
		return self.at(localName);
	}

	~image_page() = default;
	
protected:
	void unlocked_clean_unused_(){
		auto cur = named_image_regions.begin();
		while(cur != named_image_regions.end()){
			auto check = cur->second.check_droppable_and_retire();
			if(check){
				cur = named_image_regions.erase(cur);
			}else{
				++cur;
			}
		}
	}

	void drop(){
		for (auto& region : named_image_regions | std::views::values){
			region.set_protected(false);
		}

		unlocked_clean_unused_();
	}
};


struct image_atlas{
private:
	std::unique_ptr<async_image_loader> async_image_loader_{};
	string_hash_map<image_page> pages{};

public:
	[[nodiscard]] image_atlas() = default;

	[[nodiscard]] image_atlas(
	const vk::context_info& ctx_info,
	std::uint32_t graphic_family_index,
	VkQueue loader_working_queue,

	// vk::resource_descriptor_heap& target_descriptor_heap,
	std::uint32_t target_descriptor_heap_section
	) :
	async_image_loader_{std::make_unique<async_image_loader>(ctx_info, graphic_family_index, loader_working_queue/*, target_descriptor_heap*/, target_descriptor_heap_section)}{
	}

	image_page& create_image_page(
		const std::string_view name,
		const image_page_config& config = {}){
		return pages.try_emplace(name, *async_image_loader_, config).first->second;
	}

	image_page& operator[](const std::string_view name){
		if(auto itr = pages.find(name); itr != pages.end()){
			return itr->second;
		}else{
			return create_image_page(name);
		}

	}

	void request_stop() noexcept{
		async_image_loader_->working_thread.request_stop();
		wait_load();
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
