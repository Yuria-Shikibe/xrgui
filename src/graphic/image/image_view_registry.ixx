module;

#include <vulkan/vulkan.h>

export module mo_yanxi.graphic.image_view_registry;

import std;

namespace mo_yanxi::graphic{

export using image_descriptor_index = std::uint32_t;
export using sampler_descriptor_index = std::uint32_t;
export using texture_descriptor_index = std::uint32_t;

export constexpr image_descriptor_index invalid_image_descriptor_index = 0U;
export constexpr sampler_descriptor_index auto_sampler_index = ~sampler_descriptor_index{};

export
struct texture_binding{
	image_descriptor_index image_index;
	sampler_descriptor_index sampler_index;

	[[nodiscard]] constexpr explicit operator bool() const noexcept{
		return image_index != invalid_image_descriptor_index;
	}
};

static_assert(std::is_trivial_v<texture_binding>);
static_assert(std::is_trivially_copyable_v<texture_binding>);
static_assert(std::is_standard_layout_v<texture_binding>);
static_assert(sizeof(texture_binding) == sizeof(image_descriptor_index) + sizeof(sampler_descriptor_index));

export
struct image_view_registration{
	VkImageView view{};
	VkImageLayout layout{};
	sampler_descriptor_index default_sampler_index{auto_sampler_index};
};

export
struct registered_image_view{
	image_descriptor_index image_index{invalid_image_descriptor_index};
	sampler_descriptor_index preferred_sampler_index{auto_sampler_index};
	VkImageView view{};
};

export
struct image_view_sampler_record{
	VkSampler sampler{};
};

export
struct image_view_record{
	VkImageView view{};
	VkImageLayout layout{};
	sampler_descriptor_index default_sampler_index{auto_sampler_index};
};

export
struct image_view_texture_record{
	VkImageView view{};
	VkImageLayout layout{};
};

export
struct image_view_texture_dirty_slot{
	std::uint64_t generation{};
	texture_descriptor_index slot{};
};

export
struct image_view_sampler_dirty_slot{
	std::uint64_t generation{};
	sampler_descriptor_index slot{};
};

struct image_view_key{
	VkImageView view{};
	VkImageLayout layout{};

	[[nodiscard]] constexpr bool operator==(const image_view_key& other) const noexcept{
		return view == other.view && layout == other.layout;
	}
};

template <typename Handle>
struct vulkan_handle_less{
	[[nodiscard]] constexpr bool operator()(Handle lhs, Handle rhs) const noexcept{
		return std::less<Handle>{}(lhs, rhs);
	}
};

struct image_view_key_less{
	[[nodiscard]] constexpr bool operator()(const image_view_key& lhs, const image_view_key& rhs) const noexcept{
		const vulkan_handle_less<VkImageView> less{};
		if(less(lhs.view, rhs.view)){
			return true;
		}
		if(less(rhs.view, lhs.view)){
			return false;
		}
		return static_cast<std::uint32_t>(lhs.layout) < static_cast<std::uint32_t>(rhs.layout);
	}
};

export
/**
 * @brief Stable descriptor indices for Vulkan image views and samplers.
 *
 * Draw instructions store compact image/sampler indices instead of raw Vulkan
 * handles. The registry deduplicates image view + layout pairs and samplers,
 * uses ~0U for automatic sampler lookup, and tracks dirty slots so descriptor
 * buffers can be updated incrementally before command recording.
 */
class image_view_registry{
	std::vector<image_view_sampler_record> sampler_records_{};
	std::vector<image_view_record> image_records_{};
	std::vector<image_view_texture_dirty_slot> dirty_texture_slots_{};
	std::vector<image_view_sampler_dirty_slot> dirty_sampler_slots_{};

	std::flat_map<VkSampler, sampler_descriptor_index, vulkan_handle_less<VkSampler>> sampler_indices_{};
	std::flat_map<image_view_key, image_descriptor_index, image_view_key_less> image_indices_{};

	std::uint64_t dirty_generation_{};

public:
	[[nodiscard]] image_view_registry() = default;

	[[nodiscard]] sampler_descriptor_index register_sampler(VkSampler sampler){
		if(sampler == VK_NULL_HANDLE){
			throw std::invalid_argument("cannot register a null VkSampler");
		}
		if(const auto itr = sampler_indices_.find(sampler); itr != sampler_indices_.end()){
			return itr->second;
		}
		if(sampler_records_.size() >= std::numeric_limits<sampler_descriptor_index>::max()){
			throw std::length_error("sampler descriptor registry exhausted");
		}

		const auto index = static_cast<sampler_descriptor_index>(sampler_records_.size());
		sampler_records_.push_back({sampler});
		sampler_indices_.emplace(sampler, index);
		this->mark_sampler_dirty_(index);
		return index;
	}

	[[nodiscard]] registered_image_view register_image_view(image_view_registration registration){
		if(registration.view == VK_NULL_HANDLE){
			throw std::invalid_argument("cannot register a null VkImageView");
		}
		if(registration.layout == VK_IMAGE_LAYOUT_UNDEFINED){
			throw std::invalid_argument("cannot register an image view with VK_IMAGE_LAYOUT_UNDEFINED");
		}
		this->validate_default_sampler_index_(registration.default_sampler_index);

		const image_view_key key{registration.view, registration.layout};
		if(const auto itr = image_indices_.find(key); itr != image_indices_.end()){
			const auto image_index = itr->second;
			const auto texture_slot = this->texture_slot_for_image(image_index);
			auto& record = image_records_[texture_slot];
			if(record.default_sampler_index != registration.default_sampler_index){
				this->set_default_sampler(image_index, registration.default_sampler_index);
			}
			return {
				.image_index = image_index,
				.preferred_sampler_index = image_records_[texture_slot].default_sampler_index,
				.view = registration.view
			};
		}
		if(image_records_.size() >= std::numeric_limits<image_descriptor_index>::max()){
			throw std::length_error("image descriptor registry exhausted");
		}

		const auto texture_slot = static_cast<texture_descriptor_index>(image_records_.size());
		image_records_.push_back({
			.view = registration.view,
			.layout = registration.layout,
			.default_sampler_index = registration.default_sampler_index
		});
		const auto image_index = static_cast<image_descriptor_index>(texture_slot + 1U);
		image_indices_.emplace(key, image_index);
		this->mark_texture_dirty_(texture_slot);
		return {
			.image_index = image_index,
			.preferred_sampler_index = registration.default_sampler_index,
			.view = registration.view
		};
	}

	void set_default_sampler(image_descriptor_index image_index, sampler_descriptor_index sampler_index){
		const auto texture_slot = this->texture_slot_for_image(image_index);
		this->validate_default_sampler_index_(sampler_index);
		image_records_[texture_slot].default_sampler_index = sampler_index;
	}

	[[nodiscard]] sampler_descriptor_index resolve_sampler(
		image_descriptor_index image_index,
		sampler_descriptor_index sampler_index) const{
		const auto texture_slot = this->texture_slot_for_image(image_index);
		if(sampler_index == auto_sampler_index){
			sampler_index = image_records_[texture_slot].default_sampler_index;
		}
		if(sampler_index == auto_sampler_index){
			return sampler_index;
		}
		this->validate_sampler_index_(sampler_index);
		return sampler_index;
	}

	[[nodiscard]] texture_binding resolve_binding(texture_binding binding) const{
		if(!binding){
			return binding;
		}
		binding.sampler_index = this->resolve_sampler(binding.image_index, binding.sampler_index);
		return binding;
	}

	[[nodiscard]] bool contains_sampler(sampler_descriptor_index sampler_index) const noexcept{
		return sampler_index != auto_sampler_index && sampler_index < sampler_records_.size();
	}

	[[nodiscard]] bool contains_image(image_descriptor_index image_index) const noexcept{
		return image_index != invalid_image_descriptor_index
			&& static_cast<std::uint64_t>(image_index) <= image_records_.size();
	}

	[[nodiscard]] texture_descriptor_index texture_slot_for_image(image_descriptor_index image_index) const{
		this->validate_image_index_(image_index);
		return static_cast<texture_descriptor_index>(image_index - 1U);
	}

	[[nodiscard]] image_descriptor_index image_index_for_texture_slot(texture_descriptor_index texture_slot) const{
		if(texture_slot >= image_records_.size()){
			throw std::out_of_range("texture descriptor slot is not registered");
		}
		return static_cast<image_descriptor_index>(texture_slot + 1U);
	}

	[[nodiscard]] std::span<const image_view_sampler_record> sampler_records() const noexcept{
		return sampler_records_;
	}

	[[nodiscard]] const image_view_sampler_record& sampler_record_at(sampler_descriptor_index index) const{
		if(index >= sampler_records_.size()){
			throw std::out_of_range("sampler descriptor index is not registered");
		}
		return sampler_records_[index];
	}

	[[nodiscard]] std::span<const image_view_record> image_records() const noexcept{
		return image_records_;
	}

	[[nodiscard]] const image_view_record& image_record_at(image_descriptor_index image_index) const{
		return image_records_[this->texture_slot_for_image(image_index)];
	}

	[[nodiscard]] std::span<const image_view_record> texture_records() const noexcept{
		return image_records_;
	}

	[[nodiscard]] const image_view_record& texture_record_at(texture_descriptor_index index) const{
		if(index >= image_records_.size()){
			throw std::out_of_range("texture descriptor index is not registered");
		}
		return image_records_[index];
	}

	[[nodiscard]] std::span<const image_view_texture_dirty_slot> dirty_texture_slots() const noexcept{
		return dirty_texture_slots_;
	}

	[[nodiscard]] std::span<const image_view_sampler_dirty_slot> dirty_sampler_slots() const noexcept{
		return dirty_sampler_slots_;
	}

	[[nodiscard]] std::uint64_t dirty_generation() const noexcept{
		return dirty_generation_;
	}

private:
	void validate_sampler_index_(sampler_descriptor_index sampler_index) const{
		if(!this->contains_sampler(sampler_index)){
			throw std::out_of_range("sampler descriptor index is not registered");
		}
	}

	void validate_default_sampler_index_(sampler_descriptor_index sampler_index) const{
		if(sampler_index != auto_sampler_index){
			this->validate_sampler_index_(sampler_index);
		}
	}

	void validate_image_index_(image_descriptor_index image_index) const{
		if(!this->contains_image(image_index)){
			throw std::out_of_range("image descriptor index is not registered");
		}
	}

	void mark_texture_dirty_(texture_descriptor_index slot){
		++dirty_generation_;
		dirty_texture_slots_.push_back({
			.generation = dirty_generation_,
			.slot = slot
		});
	}

	void mark_sampler_dirty_(sampler_descriptor_index slot){
		++dirty_generation_;
		dirty_sampler_slots_.push_back({
			.generation = dirty_generation_,
			.slot = slot
		});
	}
};

}
