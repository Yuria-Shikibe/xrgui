module;

#include <vulkan/vulkan.h>
#include <cassert>

#ifndef MO_YANXI_IMAGE_ATLAS_DESTRUCTOR_LEAK_CHECK
#define MO_YANXI_IMAGE_ATLAS_DESTRUCTOR_LEAK_CHECK 1
#endif

export module mo_yanxi.graphic.image_atlas.util;

export import mo_yanxi.graphic.image_region;
export import mo_yanxi.graphic.image_region.borrow;
export import mo_yanxi.referenced_ptr;
export import mo_yanxi.handle_wrapper;

import mo_yanxi.vk;
import mo_yanxi.allocator2d;

import std;

namespace mo_yanxi::graphic{
export struct sub_page;

export
struct bad_image_allocation : std::exception{
};

export
struct bad_image_asset : public std::exception{
	[[nodiscard]] bad_image_asset() = default;
};

using region_type = combined_image_region<size_awared_uv<uniformed_rect_uv>>;

export
struct borrowed_image_region;

export
struct cached_image_region;

export
struct allocated_image_region :
	region_type,
	referenced_object_atomic_nonpropagation{
protected:
	math::urect region{};
	exclusive_handle_member<sub_page*> page{};

public:
	[[nodiscard]] constexpr allocated_image_region() = default;

	[[nodiscard]] constexpr allocated_image_region(
		sub_page& page,
		VkImageView imageView,
		const math::usize2 image_size,
		const math::urect region
	)
	: combined_image_region{.view = imageView}, region(region), page{&page}{
		uv.fetch_into(image_size, region);
	}

	~allocated_image_region();

	allocated_image_region(const allocated_image_region& other) = delete;
	allocated_image_region(allocated_image_region&& other) noexcept = default;
	allocated_image_region& operator=(const allocated_image_region& other) = delete;
	allocated_image_region& operator=(allocated_image_region&& other) noexcept = default;

	explicit operator bool() const noexcept{
		return view != nullptr;
	}

	[[nodiscard]] math::urect get_region() const noexcept{
		return region;
	}

	allocated_image_region& rotate(int times = 1) noexcept{
		uv.rotate(times);
		return *this;
	}

	[[nodiscard]] sub_page* get_page() const noexcept{
		return page;
	}

	using referenced_object_atomic_nonpropagation::droppable;
	using referenced_object_atomic_nonpropagation::check_droppable_and_retire;
	using referenced_object_atomic_nonpropagation::ref_decr;
	using referenced_object_atomic_nonpropagation::ref_incr;

	template <typename T>
		requires (std::convertible_to<region_type, T>)
	explicit(false) operator universal_borrowed_image_region<T, referenced_object_atomic_nonpropagation>() noexcept{
		return {*this, static_cast<T>(static_cast<const region_type&>(*this))};
	}

	template <typename T>
		requires (std::convertible_to<region_type, T>)
	std::optional<universal_borrowed_image_region<T, referenced_object_atomic_nonpropagation>> make_universal_borrow() noexcept{
		universal_borrowed_image_region<T, referenced_object_atomic_nonpropagation> rst{*this, static_cast<T>(static_cast<const region_type&>(*this))};
		if(rst.get()){
			return rst;
		}
		return std::nullopt;
	}

	std::optional<borrowed_image_region> make_borrow() noexcept;

	std::optional<cached_image_region> make_cached_borrow() noexcept;
};

export
struct borrowed_image_region : referenced_ptr<allocated_image_region, no_deletion_on_ref_count_to_zero>{
	friend allocated_image_region;
	friend cached_image_region;

private:
	[[nodiscard]] constexpr explicit(false) borrowed_image_region(allocated_image_region* object)
	: referenced_ptr(object){
	}

	[[nodiscard]] constexpr explicit(false) borrowed_image_region(allocated_image_region& object)
	: referenced_ptr(std::addressof(object)){
	}

public:
	[[nodiscard]] constexpr borrowed_image_region() = default;

	constexpr operator combined_image_region<uniformed_rect_uv>() const{
		if(*this){
			return *get();
		} else{
			return {};
		}
	}

	template <typename T>
		requires std::convertible_to<region_type, T>
	explicit(false) operator universal_borrowed_image_region<T, referenced_object_atomic_nonpropagation>() noexcept{
		return {**this, static_cast<T>(static_cast<const region_type&>(**this))};
	}
};

export
struct cached_image_region : borrowed_image_region{
private:
	friend allocated_image_region;

	combined_image_region<uniformed_rect_uv> cache_{};

public:
	[[nodiscard]] constexpr cached_image_region() = default;

	[[nodiscard]] constexpr cached_image_region(const borrowed_image_region& region) : borrowed_image_region(region){
		if(*this){
			cache_ = *this->get();
		}
	}
	[[nodiscard]] constexpr cached_image_region(borrowed_image_region&& region) : borrowed_image_region(std::move(region)){
		if(*this){
			cache_ = *this->get();
		}
	}

private:
	[[nodiscard]] constexpr explicit(false) cached_image_region(allocated_image_region* image_region)
	: borrowed_image_region(image_region){
		if(*this){
			cache_ = *this->get();
		}
	}

	[[nodiscard]] constexpr explicit(false) cached_image_region(allocated_image_region& image_region)
	: borrowed_image_region(image_region){
		if(*this){
			cache_ = *this->get();
		}
	}

public:
	operator const combined_image_region<uniformed_rect_uv>&() const{
		return cache_;
	}

	[[nodiscard]] const combined_image_region<uniformed_rect_uv>& get_cache() const{
		return cache_;
	}

	template <typename T>
		requires std::convertible_to<region_type, T>
	explicit(false) operator universal_borrowed_image_region<T, referenced_object_atomic_nonpropagation>() noexcept{
		return {**this, static_cast<T>(static_cast<const region_type&>(**this))};
	}
};

export
struct page_acquire_result{
	allocated_image_region region;
	vk::texture* handle;
};

export
struct sub_page{
	friend allocated_image_region;

private:
	std::binary_semaphore lock{1};
public:

	vk::texture texture{};
	allocator2d<> allocator{};

	auto allocate(const math::usize2 size) try {
		lock.acquire();
		auto rst = allocator.allocate(size);
		lock.release();
		return rst;
	} catch(...){
		lock.release();
		throw;
	}

	void deallocate(const math::usize2 point) noexcept{
		lock.acquire();
		allocator.deallocate(point);
		lock.release();
	}

public:
	~sub_page(){
#ifdef MO_YANXI_IMAGE_ATLAS_DESTRUCTOR_LEAK_CHECK
		if(allocator.remain_area() != allocator.extent().area()){
			std::println(std::cerr, "LEAK detected on image subpage");
			std::terminate();
		}
#endif
	}

	[[nodiscard]] sub_page() = default;

	[[nodiscard]] explicit sub_page(
		vk::allocator& allocator,
		const VkExtent2D extent_2d
	)
	: texture(allocator, extent_2d), allocator({extent_2d.width, extent_2d.height}){
	}
	[[nodiscard]] explicit sub_page(
		const math::usize2 extent_2d
	) : allocator(extent_2d){
	}

	//TODO merge similar code
	//TODO dont use bitcast of rects here?

	std::optional<page_acquire_result> acquire(
		const math::usize2 extent,
		const std::uint32_t margin
	){
		if(const auto pos = allocate(extent.copy().add(margin))){
			const math::urect rst{tags::unchecked, tags::from_extent, pos.value(), extent};

			return page_acquire_result{
					allocated_image_region{
						*this, texture.get_image_view(),
						std::bit_cast<math::usize2>(texture.get_image().get_extent2()), rst
					},
					&texture
				};
		}

		return std::nullopt;
	}

	std::optional<allocated_image_region> push(
		VkCommandBuffer commandBuffer,
		VkBuffer buffer,
		const math::usize2 extent,
		const std::uint32_t margin
	){
		if(const auto pos = allocate(extent.copy().add(margin))){
			const math::urect rst{tags::from_extent, pos.value(), extent};

			texture.write(commandBuffer,
				{
					vk::texture_buffer_write{
						buffer,
						{std::bit_cast<VkOffset2D>(pos->as<std::int32_t>()), std::bit_cast<VkExtent2D>(extent)}
					}
				});

			return allocated_image_region{
					*this, texture.get_image_view(),
					std::bit_cast<math::usize2>(texture.get_image().get_extent2()), rst
				};
		}


		return std::nullopt;
	}

	std::optional<std::pair<allocated_image_region, vk::buffer>> push(
		VkCommandBuffer commandBuffer,
		vk::texture_source_prov auto&& bitmaps_prov,
		const math::usize2 extent,
		const std::uint32_t max_gen_depth,
		const std::uint32_t margin
	){
		if(const auto pos = allocate(extent.copy().add(margin))){
			const math::urect rst{tags::from_extent, pos.value(), extent};

			VkRect2D rect{std::bit_cast<VkOffset2D>(pos->as<std::int32_t>()), std::bit_cast<VkExtent2D>(extent)};
			vk::buffer buf = texture.write(commandBuffer, rect, std::ref(bitmaps_prov), max_gen_depth);

			return std::optional<std::pair<allocated_image_region, vk::buffer>>{
					std::in_place,
					allocated_image_region{
						*this, texture.get_image_view(),
						std::bit_cast<math::usize2>(texture.get_image().get_extent2()), rst
					},
					std::move(buf)
				};
		}

		return std::nullopt;
	}
};


allocated_image_region::~allocated_image_region(){
	if(page) page->deallocate(region.src);
}

std::optional<borrowed_image_region> allocated_image_region::make_borrow() noexcept{
	borrowed_image_region rst{*this};
	if(rst){
		return rst;
	}else{
		return std::nullopt;
	}
}

std::optional<cached_image_region> allocated_image_region::make_cached_borrow() noexcept{
	cached_image_region rst{*this};
	if(rst){
		return rst;
	}else{
		return std::nullopt;
	}
}

}
