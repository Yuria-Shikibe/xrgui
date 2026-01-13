module;

#include <vulkan/vulkan.h>
#include <mo_yanxi/enum_operator_gen.hpp>


#ifndef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
#include <gch/small_vector.hpp>
#endif

#ifndef NDEBUG
#define DEBUG_CHECK 1
#else
#define DEBUG_CHECK 0
#endif

export module mo_yanxi.graphic.compositor.resource;

import mo_yanxi.utility;
import mo_yanxi.vk;
import std;

#ifdef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
import <gch/small_vector.hpp>;
#endif

bool operator==(const VkExtent3D lhs, const VkExtent3D rhs) noexcept{
	return std::memcmp(&lhs, &rhs, sizeof(VkExtent3D)) == 0;
}

namespace mo_yanxi::graphic::compositor{
export using inout_index = unsigned;

template <typename T>
constexpr inline T not_specified = std::numeric_limits<T>::max();

export constexpr inline inout_index no_slot = not_specified<inout_index>;

export
template <typename T>
constexpr T value_or(T val, T alternative) noexcept{
	if(val == not_specified<T>){
		return alternative;
	}
	return val;
}

export
template <typename T>
constexpr T value_or(T val, T alternative, const T& nullopt_value = not_specified<T>) noexcept{
	if(val == nullopt_value){
		return alternative;
	}
	return val;
}

export
template <typename T, std::predicate<const T&, const T&> Pr = std::less<T>>
T get_optional_max(T l, T r, T optional_spec_value = not_specified<T>, Pr pred = {}) noexcept{
	if(l == optional_spec_value && r == optional_spec_value) return optional_spec_value;
	if(l == optional_spec_value) return r;
	if(r == optional_spec_value) return l;
	return std::max(l, r, pred);
}

#pragma region Resource

export
enum struct resource_type : unsigned{
	unknown, image, buffer
};


export
[[nodiscard]] constexpr std::string_view type_to_name(const resource_type type) noexcept{
	switch(type){
	case resource_type::image : return "image";
	case resource_type::buffer : return "buffer";
	default : return "unknown";
	}
}


export
enum struct access_flag{
	unknown,
	read = 1 << 0,
	write = 1 << 1,
};

BITMASK_OPS(export, access_flag);

export
struct image_extent_spec{
	/**
	 * [not specify, scale the context, specified]
	 */
	std::variant<std::monostate, int, VkExtent3D> extent{};

	[[nodiscard]] image_extent_spec() = default;


	[[nodiscard]] explicit image_extent_spec(int scale_times) : extent{scale_times} {}

	friend bool operator==(const image_extent_spec& lhs, const image_extent_spec& rhs) noexcept = default;


	[[nodiscard]] VkExtent3D get_extent(const VkExtent2D context_ext) const noexcept{
		return std::visit<VkExtent3D>(overload{
				[](std::monostate){
					return VkExtent3D{};
				},
				[&](const int scale){
					return VkExtent3D{context_ext.width >> scale, context_ext.height >> scale, 1};
				},
				[](const VkExtent3D ext){return ext;}
			}, extent);
	}
	[[nodiscard]] VkImageType get_type() const noexcept{
		return std::visit<VkImageType>(overload{
				[](std::monostate){
					return VK_IMAGE_TYPE_MAX_ENUM;
				},
				[&](const int){
					return VK_IMAGE_TYPE_2D;
				},
				[](const VkExtent3D){
					return VK_IMAGE_TYPE_3D;
				}
			}, extent);
	}

	bool empty() const noexcept{
		return std::holds_alternative<std::monostate>(extent);
	}

	/**
	 *
	 * @return false if compatible
	 */
	bool try_spec_by(const image_extent_spec& other) noexcept{
		if(other.empty()) return true;
		if(extent == other.extent) return true;
		if(!empty()) return false;
		extent = other.extent;
		return true;
	}
};

export
struct image_requirement{

	VkSampleCountFlags sample_count{};

	VkFormat format{VK_FORMAT_UNDEFINED};

	VkImageUsageFlags usage{};

	VkImageAspectFlags aspect_flags{};

	/**
	 * @brief explicitly specfied expected initial layout of the image
	 */
	VkImageLayout override_layout{VK_IMAGE_LAYOUT_UNDEFINED};

	/**
	 * @brief explicitly specfied expected final layout of the image
	 */
	VkImageLayout override_output_layout{VK_IMAGE_LAYOUT_UNDEFINED};

	std::uint32_t mip_level{1};
	image_extent_spec extent{};

	VkSampleCountFlagBits get_sample_count() const noexcept{
		return VK_SAMPLE_COUNT_1_BIT;
	}

	VkImageType get_image_type() const noexcept{
		return extent.get_type();
	}

	//TODO
	VkImageViewType get_image_view_type() const noexcept{
		switch(get_image_type()){
		case VK_IMAGE_TYPE_3D : return VK_IMAGE_VIEW_TYPE_3D;
		default : return VK_IMAGE_VIEW_TYPE_2D;
		}
	}

	[[nodiscard]] VkImageUsageFlags get_required_usage() const noexcept{
		VkImageUsageFlags rst{};
		if(sample_count){
			rst |= VK_IMAGE_USAGE_SAMPLED_BIT;
		} else{
			rst |= VK_IMAGE_USAGE_STORAGE_BIT;
		}

		return rst | usage;
	}

	[[nodiscard]] VkImageAspectFlags get_aspect() const noexcept{
		return aspect_flags ? aspect_flags : VK_IMAGE_ASPECT_COLOR_BIT;
	}

	bool promote(const image_requirement& other) noexcept{
		if(!extent.try_spec_by(other.extent)){
			return false;
		}

		sample_count = std::max(sample_count, other.sample_count);

		mip_level = get_optional_max(mip_level, other.mip_level);
		if(other.format != VK_FORMAT_UNDEFINED && format != VK_FORMAT_UNDEFINED && other.format != format) return false;
		format = other.format;

		usage |= other.get_required_usage();
		aspect_flags |= other.aspect_flags;
		return true;
	}

	[[nodiscard]] bool is_sampled_image() const noexcept{
		return sample_count > 0;
	}

	[[nodiscard]] VkImageLayout get_expected_layout() const noexcept{
		if(override_layout) return override_layout;
		return is_sampled_image()
			       ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			       : VK_IMAGE_LAYOUT_GENERAL;
	}

	[[nodiscard]] VkImageLayout get_expected_layout_on_output() const noexcept{
		if(override_output_layout) return override_output_layout;
		return get_expected_layout();
	}

	[[nodiscard]] VkAccessFlags2 get_image_access(access_flag access, const VkPipelineStageFlags2 pipelineStageFlags2) const noexcept{
		switch(pipelineStageFlags2){
		case VK_PIPELINE_STAGE_2_TRANSFER_BIT : switch(access){
			case access_flag::read : return VK_ACCESS_2_TRANSFER_READ_BIT;
			case access_flag::write : return VK_ACCESS_2_TRANSFER_WRITE_BIT;
			case access_flag::read | access_flag::write : return VK_ACCESS_2_TRANSFER_READ_BIT |
					VK_ACCESS_2_TRANSFER_WRITE_BIT;
			default : return VK_ACCESS_2_NONE;
			}

		default : if(is_sampled_image()) return VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
			switch(access){
			case access_flag::read : return VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
			case access_flag::write : return VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
			case access_flag::read | access_flag::write : return VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
					VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
			default : return VK_ACCESS_2_NONE;
			}
		}
	}

	[[nodiscard]] image_requirement max(const image_requirement& other_in_same_partition) const noexcept{
		image_requirement cpy{*this};
		cpy.promote(other_in_same_partition);
		return cpy;
	}

	VkFormat get_format() const noexcept{
		return format == VK_FORMAT_UNDEFINED ? VK_FORMAT_R8G8B8A8_UNORM : format;
	}

	VkImageCreateInfo get_image_create_info(VkExtent2D context_extent) const noexcept{
		return {
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_ALIAS_BIT,
			.imageType = get_image_type(),
			.format = get_format(),
			.extent = extent.get_extent(context_extent),
			.mipLevels = mip_level,
			.arrayLayers = 1,
			.samples = get_sample_count(),
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = get_required_usage(),
		};
	}
};

export
struct buffer_size_spec{
	using cropper = VkDeviceSize(VkExtent2D);
	/**
	 * [not specify, fixed, deduced]
	 */
	std::variant<std::monostate, VkDeviceSize, cropper*> extent{};

	[[nodiscard]] VkDeviceSize get_size(const VkExtent2D context_ext) const noexcept{
		return std::visit<VkDeviceSize>(overload{
				[](std::monostate){
					return VkDeviceSize{};
				},
				[&](const VkDeviceSize size){
					return size;
				},
				[&](cropper* c) -> VkDeviceSize{
					if(!c){
						return {};
					}
					return c(context_ext);
				}
			}, extent);
	}

	bool empty() const noexcept{
		return std::holds_alternative<std::monostate>(extent);
	}

	bool operator==(const buffer_size_spec&) const noexcept = default;

	/**
	 *
	 * @return false if compatible
	 */
	bool try_spec_by(const buffer_size_spec& other) noexcept{
		if(other.empty()) return true;
		if(extent == other.extent) return true;
		if(!empty()) return false;
		extent = other.extent;
		return true;
	}
};

export
struct buffer_requirement{
	buffer_size_spec size;
	VkBufferUsageFlags usage{};

	bool promote(const buffer_requirement& other) noexcept{
		if(!size.try_spec_by(other.size)) return false;
		usage |= other.usage;
		return true;
	}


	VkDeviceSize get_required_memory_size(const VkExtent2D context_extent) const noexcept{
		return size.get_size(context_extent);
	}

	VkBufferCreateInfo get_buffer_create_info(VkExtent2D context_extent) const noexcept{
		return {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.size = get_required_memory_size(context_extent),
			.usage = usage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
			.queueFamilyIndexCount = 0,
			.pQueueFamilyIndices = nullptr
		};
	}
};

export
struct resource_requirement{
	std::variant<std::monostate, image_requirement, buffer_requirement> req{};
	access_flag access;
	VkPipelineStageFlags2 last_used_stage{VK_PIPELINE_STAGE_2_NONE};

	template <typename T>
	auto get_if() const noexcept{
		return std::get_if<T>(&req);
	}

	template <typename T, typename S>
	auto& get(this S& self) {
		return std::get<T>(self.req);
	}

	VkAccessFlags2 get_access_flags(VkPipelineStageFlags2 append_stages) const noexcept{
		return std::visit<VkAccessFlags2>(overload_def_noop{
			std::in_place_type<VkAccessFlags2>,
			[&, this](const image_requirement& l){
				return l.get_image_access(access, append_stages | last_used_stage);
			}
		}, req);
	}

	[[nodiscard("false result should be addressed")]] bool promote(const resource_requirement& other) noexcept{
		access |= other.access;
		return std::visit<bool>(overload_def_noop{
			std::in_place_type<bool>,
			[](image_requirement& l, const image_requirement& r) {
				return l.promote(r);
			},
			[](buffer_requirement& l, const buffer_requirement& r) {
				return l.promote(r);
			}
		}, req, other.req);
	}

	[[nodiscard]] resource_type type() const noexcept{
		return static_cast<resource_type>(req.index());
	}

	[[nodiscard]] bool empty() const noexcept{
		return std::holds_alternative<std::monostate>(req);
	}

};
#pragma endregion

#pragma region Entity


export
struct image_entity{
	vk::image_handle handle{};
};

export
struct buffer_entity{
	vk::buffer_range handle{};
};

export
struct resource_handle;

export
struct
resource_entity{
	using variant_t = std::variant<std::monostate, image_entity, buffer_entity>;
	resource_requirement overall_requirement{};
	variant_t resource{};

	[[nodiscard]] resource_entity() = default;

	[[nodiscard]] explicit resource_entity(const resource_requirement& requirement)
		: overall_requirement(requirement){
		switch(requirement.type()){
		case resource_type::image : resource.emplace<image_entity>();
			break;
		case resource_type::buffer : resource.emplace<buffer_entity>();
			break;
		default : break;
		}
	}

	[[nodiscard]] resource_entity(const resource_requirement& overall_requirement, const variant_t& resource)
		: overall_requirement(overall_requirement),
		  resource(resource){
	}

	const resource_handle* get_identity() const noexcept{
		return static_cast<const resource_handle*>(std::visit<const void*>(overload_def_noop{
			                                                   std::in_place_type<const void*>,
			                                                   [](const image_entity& l) -> const void* {return l.handle.image;},
			                                                   [](const buffer_entity& l) -> const void* {return l.handle.buffer;},
		                                                   }, resource));
	}

	bool operator==(std::nullptr_t) const noexcept{
		return !static_cast<bool>(*this);
	}

	explicit operator bool() const noexcept{
		return std::visit<bool>(overload_def_noop{
			std::in_place_type<bool>,
			[](const image_entity& l) {return l.handle.image != nullptr;},
			[](const buffer_entity& l) {return l.handle.buffer != nullptr;},
		}, resource);
	}

	bool empty() const noexcept{
		return !static_cast<bool>(*this);
	}

	void set_resource(const variant_t& res){
		if(res.index() != overall_requirement.req.index()){
			throw std::bad_variant_access{};
		}
		resource = res;
	}

	[[nodiscard]] resource_type type() const noexcept{
		return overall_requirement.type();
	}

	[[nodiscard]] image_entity& as_image() noexcept{
		return std::get<image_entity>(resource);
	}

	[[nodiscard]] const image_entity& as_image() const noexcept{
		return std::get<image_entity>(resource);
	}

	[[nodiscard]] buffer_entity& as_buffer() noexcept{
		return std::get<buffer_entity>(resource);
	}

	[[nodiscard]] const buffer_entity& as_buffer() const noexcept{
		return std::get<buffer_entity>(resource);
	}
};


export
struct resource_dependency{
	VkPipelineStageFlags2    src_stage{VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT};
	VkAccessFlags2           src_access{VK_ACCESS_2_NONE};
	VkPipelineStageFlags2    dst_stage{VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT};
	VkAccessFlags2           dst_access{VK_ACCESS_2_NONE};
	VkImageLayout src_layout{VK_IMAGE_LAYOUT_GENERAL};
	VkImageLayout dst_layout{VK_IMAGE_LAYOUT_GENERAL};
};

export
struct resource_entity_external{
	using variant_t = resource_entity::variant_t;

	variant_t resource{};
	resource_dependency dependency{};

	[[nodiscard]] resource_entity_external() = default;

	[[nodiscard]] explicit(false) resource_entity_external(const variant_t& desc)
		: resource(desc){
	}

	[[nodiscard]] resource_entity_external(const variant_t& desc, const resource_dependency& dependency)
		: resource(desc),
		  dependency(dependency){

	}

	[[nodiscard]] resource_type type() const noexcept{
		return resource_type{static_cast<std::underlying_type_t<resource_type>>(resource.index())};
	}

	[[nodiscard]] image_entity& as_image() noexcept{
		return std::get<image_entity>(resource);
	}

	[[nodiscard]] const image_entity& as_image() const noexcept{
		return std::get<image_entity>(resource);
	}

	[[nodiscard]] buffer_entity& as_buffer() noexcept{
		return std::get<buffer_entity>(resource);
	}

	[[nodiscard]] const buffer_entity& as_buffer() const noexcept{
		return std::get<buffer_entity>(resource);
	}

	void load_entity(const resource_entity& entity){
		std::visit(overload_narrow{[](image_entity& i, const image_entity& e){
			i.handle = e.handle;
		}, [](buffer_entity& b, const buffer_entity& e){
			b.handle = e.handle;
		}}, resource, entity.resource);
	}
};

export
struct external_resource_usage{
	resource_entity_external* resource{};
	inout_index slot{no_slot};

	[[nodiscard]] external_resource_usage() = default;

	[[nodiscard]] external_resource_usage(resource_entity_external& resource, const inout_index slot)
		: resource(&resource),
		  slot(slot){
	}
};

#pragma endregion

#pragma region Slot


export
struct slot_pair{
	inout_index in{no_slot};
	inout_index out{no_slot};


	[[nodiscard]] bool is_invalid() const noexcept{
		return in == no_slot && out == no_slot;
	}

	[[nodiscard]] bool is_inout() const noexcept{
		return in != no_slot && out != no_slot;
	}

	[[nodiscard]] bool has_in() const noexcept{
		return in != no_slot;
	}

	[[nodiscard]] bool has_out() const noexcept{
		return out != no_slot;
	}

	friend constexpr bool operator==(const slot_pair& lhs, const slot_pair& rhs) noexcept = default;
};

export
struct binding_info{
	std::uint32_t binding;
	std::uint32_t set;

	friend constexpr bool operator==(const binding_info&, const binding_info&) noexcept = default;
};

export
struct resource_map_entry{
	binding_info binding{};
	slot_pair slot{};

	[[nodiscard]] bool is_inout() const noexcept{
		return slot.is_inout();
	}
};


export
struct inout_map{
private:
	gch::small_vector<resource_map_entry> connection_{};

public:
	[[nodiscard]] inout_map() = default;

	[[nodiscard]] inout_map(const std::initializer_list<resource_map_entry> map)
		: connection_(map){
		compact_check();
	}

	std::optional<slot_pair> operator[](const binding_info binding) const noexcept{
		if(auto itr = std::ranges::find(connection_, binding, &resource_map_entry::binding); itr != connection_.
			end()){
			return itr->slot;
		}

		return std::nullopt;
	}

private:
	void compact_check(){
		if(connection_.empty()){
			return;
		}


		auto checker = [this](auto transform){
			std::ranges::sort(connection_, {}, transform);
			if(transform(connection_.front()) != no_slot && transform(connection_.front()) != 0) return false;

			for(auto [l, r] : connection_ | std::views::transform(transform) | std::views::take_while(
				    [](const inout_index v){ return v != no_slot; }) | std::views::adjacent<2>){
				if(r - l > 1){
					return false;
				}
			}

			return true;
		};

		if(!checker([](const resource_map_entry& e){ return e.slot.in; })){
			throw std::invalid_argument("Invalid resource map entry");
		}

		if(!checker([](const resource_map_entry& e){ return e.slot.out; })){
			throw std::invalid_argument("Invalid resource map entry");
		}

		std::ranges::sort(connection_, std::ranges::greater{}, &resource_map_entry::is_inout);

		//TODO check no inout overlap?
	}

public:

	[[nodiscard]] std::span<const resource_map_entry> get_connections() const noexcept{
		return {connection_.data(), connection_.size()};
	}
};

//TODO make the access control more strict
export
struct pass_inout_connection{
	static constexpr std::size_t sso = 4;
	using data_index = unsigned;
	using slot_to_data_index = gch::small_vector<data_index, sso>;

	gch::small_vector<resource_requirement, sso> data{};
	slot_to_data_index input_slots{};
	slot_to_data_index output_slots{};

private:
	void resize_and_set_in(const inout_index idx, const std::size_t desc_index){
		input_slots.resize(std::max<std::size_t>(input_slots.size(), idx + 1), no_slot);
		input_slots[idx] = desc_index;
	}

	void resize_and_set_out(const inout_index idx, const std::size_t desc_index){
		output_slots.resize(std::max<std::size_t>(output_slots.size(), idx + 1), no_slot);
		output_slots[idx] = desc_index;
	}

	[[nodiscard]] bool has_index(const inout_index idx, slot_to_data_index pass_inout_connection::* mptr) const noexcept{
		if((this->*mptr).size() <= idx) return false;
		return (this->*mptr)[idx] != no_slot;
	}

public:
	template <typename T, typename S>
	auto& at_in(this S&& self, inout_index index) noexcept{
		return self.data.at(self.input_slots.at(index)).template get<T>();
	}

	template <typename T, typename S>
	auto& at_out(this S&& self, inout_index index) noexcept{
		return self.data.at(self.output_slots.at(index)).template get<T>();
	}


	template <typename S>
	auto& at_in(this S&& self, inout_index index) noexcept{
		return self.data.at(self.input_slots.at(index));
	}

	template <typename S>
	auto& at_out(this S&& self, inout_index index) noexcept{
		return self.data.at(self.output_slots.at(index));
	}

	[[nodiscard]] std::optional<resource_requirement> get_out(const inout_index outIdx) const noexcept{
		if(outIdx >= output_slots.size()){
			return std::nullopt;
		}
		auto sidx = output_slots[outIdx];
		if(sidx >= data.size()) return std::nullopt;
		return data[sidx];
	}

	[[nodiscard]] std::optional<resource_requirement> get_in(const inout_index inIdx) const noexcept{
		if(inIdx >= input_slots.size()){
			return std::nullopt;
		}
		auto sidx = input_slots[inIdx];
		if(sidx >= data.size()) return std::nullopt;
		return data[sidx];
	}
	[[nodiscard]] resource_requirement& at_out(const inout_index outIdx) noexcept{
		if(outIdx >= output_slots.size()){
			throw std::out_of_range("Invalid resource map entry");
		}
		auto sidx = output_slots[outIdx];
		return data[sidx];
	}

	[[nodiscard]] resource_requirement& at_in(const inout_index inIdx) noexcept{
		if(inIdx >= input_slots.size()){
			throw std::out_of_range("Invalid resource map entry");
		}
		auto sidx = input_slots[inIdx];
		return data[sidx];
	}

private:
	auto get_valid_of(slot_to_data_index pass_inout_connection::* which) const noexcept{
		return (this->*which) | std::views::enumerate | std::views::filter([](auto&& t){
			auto&& [idx, v] = t;
			return v != no_slot;
		});
	}

public:
	auto get_valid_in() const noexcept{
		return get_valid_of(&pass_inout_connection::input_slots);
	}

	auto get_valid_out() const noexcept{
		return get_valid_of(&pass_inout_connection::output_slots);
	}

	template <typename T>
		requires std::constructible_from<resource_requirement, T&&>
	void add(const bool in, const bool out, T&& val){
		const auto sz = data.size();
		data.push_back(std::forward<T>(val));
		if(in){
			input_slots.push_back(sz);
		}
		if(out){
			output_slots.push_back(sz);
		}
	}

	template <typename T>
		requires std::constructible_from<resource_requirement, T&&>
	void add(const bool in, const bool out, const inout_index index, T&& val){
		const auto sz = data.size();
		data.push_back(std::forward<T>(val));
		if(in){
			resize_and_set_in(index, sz);
		}
		if(out){
			resize_and_set_out(index, sz);
		}
	}

	template <typename T>
		requires std::constructible_from<resource_requirement, T&&>
	void add(const slot_pair slots, T&& val){
		if(has_index(slots.in, &pass_inout_connection::input_slots) || has_index(
			slots.out, &pass_inout_connection::output_slots)){
			return;
		}

		const auto sz = data.size();
		data.push_back(std::forward<T>(val));

		if(slots.in != no_slot){
			resize_and_set_in(slots.in, sz);
		}

		if(slots.out != no_slot){
			resize_and_set_out(slots.out, sz);
		}
	}


	[[nodiscard]] gch::small_vector<slot_pair> get_inout_indices() const{
		gch::small_vector<slot_pair> rst{};

		for(auto&& [idx, data_idx] : output_slots | std::views::enumerate){
			if(auto itr = std::ranges::find(input_slots, data_idx); itr != input_slots.end()){
				rst.push_back({
						static_cast<unsigned>(itr - input_slots.begin()),
						static_cast<unsigned>(idx)
					});
			}
		}

		return rst;
	}
};

#pragma endregion

#pragma region Descriptors

export
struct external_descriptor{
	VkDescriptorSetLayout layout;
	VkDescriptorBufferBindingInfoEXT dbo_info;
};

export
struct external_descriptor_usage{
	const external_descriptor* entry;
	std::uint32_t set_index;
	VkDeviceSize offset;
};

#pragma endregion

}


export
template<>
struct std::hash<mo_yanxi::graphic::compositor::binding_info>{
	static std::size_t operator()(const mo_yanxi::graphic::compositor::binding_info info) noexcept{
		static constexpr std::hash<unsigned long long> hasher;
		return hasher(std::bit_cast<unsigned long long>(info));
	}
};

template <>
struct std::hash<mo_yanxi::graphic::compositor::slot_pair>{
	static std::size_t operator()(const mo_yanxi::graphic::compositor::slot_pair pair) noexcept{
		return std::hash<unsigned long long>{}(std::bit_cast<unsigned long long>(pair));
	}
};
