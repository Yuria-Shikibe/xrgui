module;

#include <vulkan/vulkan.h>
#include <cassert>

#ifndef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
#include <spirv_reflect.h>;
#endif

export module mo_yanxi.graphic.compositor.post_process_pass;

import std;
export import mo_yanxi.graphic.compositor.manager;
export import mo_yanxi.graphic.compositor.resource;
export import mo_yanxi.graphic.shader_reflect;

export import mo_yanxi.math.vector2;
export import mo_yanxi.vk;

import mo_yanxi.meta_programming;
import mo_yanxi.utility;

#ifdef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
import <spirv_reflect.h>;
#endif

namespace mo_yanxi::graphic::compositor{
namespace{
access_flag extract_descriptor_access(const SpvReflectDescriptorBinding* resource) noexcept{
	access_flag decr{};


	const bool no_read = (resource->decoration_flags & SPV_REFLECT_DECORATION_NON_READABLE);
	const bool no_write = (resource->decoration_flags & SPV_REFLECT_DECORATION_NON_WRITABLE);

	if(!no_read && !no_write){
		decr = access_flag::read | access_flag::write;
	} else if(no_read){
		decr = access_flag::write;
	} else{
		decr = access_flag::read;
	}
	return decr;
}
}

resource_requirement extract_image_state(const SpvReflectDescriptorBinding* resource){
	access_flag decr = extract_descriptor_access(resource);

	if(resource->image.dim == SpvDim1D || resource->image.dim == SpvDim3D){
		throw std::runtime_error("Unsupported image dimension");
	}

	bool isSampled = resource->image.sampled == 1;

	return resource_requirement{
			.req = image_requirement{
				.sampled = isSampled,
				.storage = !isSampled,

				.format = convertImageFormatToVkFormat(resource->image.image_format),
				.extent = isSampled ? image_extent_spec{} : image_extent_spec{0},
			},
			.access = decr,
			.last_used_stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		};
}

resource_requirement extract_buffer_state(const SpvReflectDescriptorBinding* resource){
	return resource_requirement{
			.req = buffer_requirement{
				{static_cast<VkDeviceSize>(get_buffer_size(resource))},
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			},
			.access = extract_descriptor_access(resource),
			.last_used_stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		};
}

template <typename T>
constexpr T ceil_div(T x, T div) noexcept{
	return (x + div - 1) / div;
}

export struct stage_ubo : binding_info{
	VkDeviceSize size;
};

struct bound_stage_resource : binding_info, resource_requirement{
};

export struct compute_shader_info{
	vk::shader_module shader;
	std::string entry_name{"main"};
	std::optional<VkSpecializationInfo> specialization_info{};
	math::u32size2 thread_group_size{1, 1};

	[[nodiscard]] compute_shader_info() = default;

	[[nodiscard]] compute_shader_info(vk::shader_module&& shader,
		std::string_view entry_name = "main",
		std::optional<VkSpecializationInfo> specialization_info = std::nullopt)
		: shader(std::move(shader)),
		entry_name(entry_name),
		specialization_info(specialization_info){
		const shader_reflection refl{this->shader.get_binary()};
		thread_group_size = refl.get_thread_group_size(this->entry_name);
	}
};

export struct post_process_stage;

export struct post_process_meta{
	friend post_process_stage;

private:
	compute_shader_info shader_info_{};
	pass_binding_socket inout_map_{};

	std::vector<stage_ubo> uniform_buffers_{};
	std::vector<bound_stage_resource> resources_{};

	vk::constant_layout constant_layout_{};
	vk::descriptor_layout_builder descriptor_layout_builder_{};
	std::vector<inout_index> required_transient_buffer_input_slots_{};

public:
	pass_logical_socket sockets{};

	[[nodiscard]] post_process_meta() = default;

	[[nodiscard]] post_process_meta(compute_shader_info&& shader_info, const pass_binding_socket& inout_map)
		: shader_info_(std::move(shader_info)),
		inout_map_(inout_map){

		const shader_reflection refl{shader_info_.shader.get_binary()};


		for(const auto* input : refl.storage_images()){
			const auto binding = refl.binding_info_of(input);
			resources_.push_back({binding, extract_image_state(input)});
			if(binding.set != 0) continue;
			descriptor_layout_builder_.push(binding.binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				VK_SHADER_STAGE_COMPUTE_BIT);
		}



		for(const auto* input : refl.sampled_images()){
			auto binding = refl.binding_info_of(input);
			resources_.push_back({binding, extract_image_state(input)});
			if(binding.set != 0) continue;


			VkDescriptorType desc_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			if(input->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE){
				desc_type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			}

			descriptor_layout_builder_.push(binding.binding, desc_type, VK_SHADER_STAGE_COMPUTE_BIT);
		}


		for(const auto* input : refl.uniform_buffers()){
			auto binding = refl.binding_info_of(input);
			uniform_buffers_.push_back({binding, get_buffer_size(input)});
			if(binding.set != 0) continue;
			descriptor_layout_builder_.push(binding.binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_COMPUTE_BIT);
		}


		for(const auto* input : refl.storage_buffers()){
			auto binding = refl.binding_info_of(input);
			resources_.push_back({binding, extract_buffer_state(input)});
			if(binding.set != 0) continue;
			descriptor_layout_builder_.push(binding.binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				VK_SHADER_STAGE_COMPUTE_BIT);
		}

		for(const auto& pass : inout_map.get_connections()){
			if(!std::ranges::contains(resources_, pass.binding)){
				throw std::invalid_argument(std::format(
					"binding (set={}, binding={}) in inout_map not found in shader '{}'",
					pass.binding.set, pass.binding.binding, shader_info_.shader.get_name()));
			}
		}

		for(const auto& range : refl.push_constant_ranges()){
			constant_layout_.push(range);
		}

		sockets.data.reserve(resources_.size());
		for(const auto& pass : inout_map_.get_connections()){
			if(auto itr = std::ranges::find(resources_, pass.binding); itr != std::ranges::end(resources_)){
				sockets.add(pass.slot, *itr);
			}
		}
	}

	[[nodiscard]] const pass_binding_socket& get_inout_map() const noexcept{
		return inout_map_;
	}

	bound_stage_resource& operator[](binding_info binding) /*noexcept*/{
		if(auto itr = std::ranges::find(resources_, binding); itr != resources_.end()){
			return *itr;
		}
		throw std::out_of_range("pass does not exist");
	}

	const bound_stage_resource& operator[](binding_info binding) const /*noexcept*/{
		if(auto itr = std::ranges::find(resources_, binding); itr != resources_.end()){
			return *itr;
		}
		throw std::out_of_range("pass does not exist");
	}

	[[nodiscard]] std::string_view name() const noexcept{
		return shader_info_.shader.get_name();
	}

	[[nodiscard]] const compute_shader_info& shader_info() const noexcept{
		return shader_info_;
	}


	void set_format_at_in(std::uint32_t slot, VkFormat format){
		sockets.at_in<image_requirement>(slot).format = format;
	}

	void set_format_at_out(std::uint32_t slot, VkFormat format){
		sockets.at_out<image_requirement>(slot).format = format;
	}

	auto get_local_uniform_buffer() const noexcept{
		return uniform_buffers_ | std::views::filter([](const stage_ubo& ubo){
			return ubo.set == 0;
		});
	}

	[[nodiscard]] auto push_constant_ranges() const noexcept{
		return constant_layout_.get_constant_ranges();
	}
};

export
struct ubo_subrange{
	std::uint32_t binding;
	std::uint32_t offset;
	std::uint32_t size;
};

export
struct image_binding_override{
	VkImageView image_view{VK_NULL_HANDLE};
	VkImageLayout image_layout{VK_IMAGE_LAYOUT_UNDEFINED};
};

export
struct post_process_stage : pass_impl{
protected:
	post_process_meta meta_{};

	vk::descriptor_layout descriptor_layout_{};
	vk::pipeline_layout pipeline_layout_{};
	vk::pipeline pipeline_{};


	vk::uniform_buffer uniform_buffer_{};
	std::vector<ubo_subrange> uniform_subranges_{};


	std::unordered_map<binding_info, VkSampler> samplers_{};
	vk::descriptor_buffer descriptor_buffer_{};


	std::vector<external_descriptor_usage> external_descriptor_usages_{};
	std::vector<std::byte> push_constants_{};

public:
	[[nodiscard]] post_process_stage() = default;

	[[nodiscard]] explicit(false) post_process_stage(post_process_meta&& meta)
		: meta_(std::move(meta)){
	}

	void set_sampler_at(binding_info binding_info, VkSampler sampler){
		samplers_.insert_or_assign(binding_info, sampler);
	}

	void set_sampler_at_binding(std::uint32_t binding_at_set_0, VkSampler sampler){
		samplers_.insert_or_assign({binding_at_set_0, 0}, sampler);
	}

	[[nodiscard]] VkSampler get_sampler_at(binding_info binding_info) const{
		return samplers_.at(binding_info);
	}

	vk::uniform_buffer& ubo(){
		return uniform_buffer_;
	}

	[[nodiscard]] const vk::uniform_buffer& ubo() const{
		return uniform_buffer_;
	}

	void add_external_descriptor(const external_descriptor& entry, std::uint32_t setIdx, VkDeviceSize offset = 0){
		if(setIdx == 0) throw std::invalid_argument{"[0] is reserved for local descriptors"};
		auto itr = std::ranges::lower_bound(external_descriptor_usages_, setIdx, {},
			&external_descriptor_usage::set_index);
		if(itr == external_descriptor_usages_.end()){
			external_descriptor_usages_.emplace_back(&entry, setIdx, offset);
		} else{
			if(itr->set_index == setIdx){
				throw std::invalid_argument{"Duplicate descriptor set index"};
			}

			external_descriptor_usages_.emplace(itr, &entry, setIdx, offset);
		}
	}

	void set_push_constants(std::vector<std::byte> data){
		push_constants_ = std::move(data);
	}

protected:
	template <std::ranges::contiguous_range R = std::initializer_list<VkPushConstantRange>>
		requires std::convertible_to<std::ranges::range_value_t<R>, VkPushConstantRange>
	void init_pipeline(const vk::allocator_usage& ctx, const R& pushConstantRanges = {}) noexcept{
		descriptor_layout_ = {
				ctx.get_device(), VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
				meta_.descriptor_layout_builder_
			};
		if(external_descriptor_usages_.empty()){
			pipeline_layout_ = vk::pipeline_layout{ctx.get_device(), 0, {descriptor_layout_}, pushConstantRanges};
		} else{
			std::vector<VkDescriptorSetLayout> layouts{};
			layouts.reserve(1 + external_descriptor_usages_.size());
			layouts.push_back(descriptor_layout_);
			layouts.append_range(external_descriptor_usages_ | std::views::transform(
				[](const external_descriptor_usage& u){
					return u.entry->layout;
				}));
			pipeline_layout_ = vk::pipeline_layout{ctx.get_device(), 0, layouts, pushConstantRanges};
		}

		pipeline_ = vk::pipeline{
				ctx.get_device(), pipeline_layout_, VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
				meta_.shader_info_.shader.get_create_info(VK_SHADER_STAGE_COMPUTE_BIT,
					meta_.shader_info_.entry_name,
					meta().shader_info_.specialization_info ? &meta().shader_info_.specialization_info.value() : nullptr)
			};
	}

	template <std::ranges::contiguous_range R = std::initializer_list<VkPushConstantRange>>
		requires std::convertible_to<std::ranges::range_value_t<R>, VkPushConstantRange>
	void init_pipeline_and_ubo(const vk::allocator_usage& ctx, const R& push_constant_ranges = {}){
		init_pipeline(ctx, push_constant_ranges);
		init_uniform_buffer(ctx);
	}

	void reset_descriptor_buffer(const vk::allocator_usage& ctx, std::uint32_t chunk_count = 1){
		descriptor_buffer_ = vk::descriptor_buffer{
				ctx, descriptor_layout_, descriptor_layout_.binding_count(), chunk_count
			};
	}

	void init_uniform_buffer(const vk::allocator_usage& ctx){
		std::uint32_t ubo_size{};
		uniform_subranges_.clear();
		for(const auto& desc : meta_.get_local_uniform_buffer()){
			ubo_size = vk::align_up<std::uint32_t>(ubo_size,
				vk::get_device_requirement(ctx.get_device())->min_uniform_buffer_offset_alignment);
			uniform_subranges_.push_back(ubo_subrange{desc.binding, ubo_size, static_cast<std::uint32_t>(desc.size)});
			ubo_size += desc.size;
		}

		if(ubo_size == 0) return;
		uniform_buffer_ = vk::uniform_buffer{ctx, ubo_size};
	}

	void default_bind_uniform_buffer(){
		default_bind_uniform_buffer(0);
	}

	void default_bind_uniform_buffer(const std::uint32_t chunk_index){
		vk::descriptor_mapper mapper{descriptor_buffer_};

		for(const auto& uniform_offset : uniform_subranges_){
			(void)mapper.set_uniform_buffer(uniform_offset.binding,
				uniform_buffer_.get_address() + uniform_offset.offset, uniform_offset.size, chunk_index);
		}
	}

	void default_bind_uniform_buffers(const std::uint32_t chunk_count){
		for(std::uint32_t chunk_idx = 0; chunk_idx < chunk_count; ++chunk_idx){
			default_bind_uniform_buffer(chunk_idx);
		}
	}

	[[nodiscard]] resource_entity get_resource_for_connection(const pass_data& pass,
		const resource_map_entry& connection) const{
		if(connection.slot.has_in()){
			if(auto res = pass.get_used_resources().get_in(connection.slot.in)){
				return res;
			}
		}

		if(connection.slot.has_out()){
			if(auto res = pass.get_used_resources().get_out(connection.slot.out)){
				return res;
			}
		}

		throw std::out_of_range("failed to find resource");
	}

	[[nodiscard]] const resource_requirement& get_requirement_for_binding(const binding_info binding) const{
		return meta_[binding];
	}

	[[nodiscard]] VkImageLayout get_default_image_layout(const image_requirement& req) const noexcept{
		if(req.uses_sampled_descriptor() && !req.uses_storage_descriptor()){
			return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}

		return VK_IMAGE_LAYOUT_GENERAL;
	}

	void bind_resource(vk::descriptor_mapper& mapper,
		const std::uint32_t chunk_index,
		const resource_map_entry& connection,
		const resource_entity& res,
		const resource_requirement& requirement,
		const image_binding_override* image_override = nullptr) const{
		std::visit(overload_narrow{
				[&](const image_entity& entity){
					auto& req = std::get<image_requirement>(requirement.req);
					const auto image_view = image_override && image_override->image_view != VK_NULL_HANDLE
						? image_override->image_view
						: entity.handle.image_view;
					auto image_layout = get_default_image_layout(req);
					if(image_override && image_override->image_layout != VK_IMAGE_LAYOUT_UNDEFINED){
						image_layout = image_override->image_layout;
					}

					if(req.uses_sampled_descriptor() && !req.uses_storage_descriptor()){
						VkSampler sampler = VK_NULL_HANDLE;

						VkDescriptorType desc_type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;


						if(samplers_.contains(connection.binding)){
							sampler = get_sampler_at(connection.binding);
							if(sampler != VK_NULL_HANDLE){
								desc_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
							}
						}

						mapper.set_image(
							connection.binding.binding,
							image_view,
							chunk_index,
							image_layout,
							sampler,
							desc_type
						);
					} else{
						mapper.set_storage_image(connection.binding.binding, image_view, image_layout, chunk_index);
					}
				},
				[&](const buffer_entity& entity){
					(void)mapper.set_storage_buffer(connection.binding.binding, entity.handle.begin(),
						entity.handle.size, chunk_index);
				}
			}, res.resource);
	}

	void default_bind_resources(const pass_data& pass, const std::uint32_t chunk_index = 0){
		vk::descriptor_mapper mapper{descriptor_buffer_};
		for(const auto& connection : meta_.inout_map_.get_connections()){
			if(connection.binding.set != 0) continue;
			const auto res = get_resource_for_connection(pass, connection);
			bind_resource(mapper, chunk_index, connection, res, get_requirement_for_binding(connection.binding));
		}
	}

	void bind_descriptor_sets(VkCommandBuffer buffer, const std::uint32_t chunk_index = 0) const{
		if(external_descriptor_usages_.empty()){
			descriptor_buffer_.bind_chunk_to(buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout_, 0, chunk_index);
		} else{
			std::array<std::byte, 1024> buf;
			std::pmr::monotonic_buffer_resource pool{buf.data(), buf.size(), std::pmr::new_delete_resource()};
			std::pmr::vector<VkDescriptorBufferBindingInfoEXT> infos(external_descriptor_usages_.size() + 1, &pool);

			infos[0] = descriptor_buffer_;

			std::pmr::vector<VkDeviceSize> offsets(infos.size(), &pool);
			offsets[0] = descriptor_buffer_.get_chunk_offset(chunk_index);
			std::pmr::vector<std::uint32_t> indices(std::from_range,
				std::views::iota(0U) | std::views::take(infos.size()), &pool);
			for(std::uint32_t i = 0; i < external_descriptor_usages_.size(); ++i){
				const auto setIdx = i + 1;
				const auto itr = std::ranges::lower_bound(external_descriptor_usages_, setIdx, {},
					&external_descriptor_usage::set_index);
				if(itr == external_descriptor_usages_.end() || itr->set_index != setIdx){
					throw std::out_of_range(
						"failed to find external descriptor usage, set index must be contiguous from 1");
				}
				infos[setIdx] = itr->entry->dbo_info;
				offsets[setIdx] = itr->offset;
			}

			vk::cmd::bindDescriptorBuffersEXT(buffer, infos.size(), infos.data());
			vk::cmd::setDescriptorBufferOffsetsEXT(buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout_, 0,
				infos.size(), indices.data(), offsets.data());
		}
	}

public:
	void prepare(const vk::allocator_usage& alloc, const pass_data& pass, const math::u32size2 extent) override{
		init_pipeline(alloc, meta_.push_constant_ranges());
		init_uniform_buffer(alloc);
		reset_descriptor_buffer(alloc);
		default_bind_uniform_buffer();
		default_bind_resources(pass);
	}

	[[nodiscard]] const pass_logical_socket& sockets() const noexcept final{
		return meta_.sockets;
	}

	[[nodiscard]] pass_logical_socket& sockets() noexcept{
		return meta_.sockets;
	}

	[[nodiscard]] std::string_view get_name() const noexcept override{
		return meta_.name();
	}


	[[nodiscard]] static math::u32size2 get_work_group_size(math::u32size2 image_size, math::u32size2 unit_size) noexcept{
		assert(std::has_single_bit(unit_size.x));
		assert(std::has_single_bit(unit_size.y));
		return image_size.add(unit_size.copy().sub(1, 1)).div(unit_size);
	}

	void record_command(
		const vk::allocator_usage& allocator,
		const pass_data& pass,
		math::u32size2 extent,
		VkCommandBuffer buffer) override{
		pipeline_.bind(buffer, VK_PIPELINE_BIND_POINT_COMPUTE);
		bind_descriptor_sets(buffer);

		const auto pc_ranges = meta_.push_constant_ranges();
		if(!pc_ranges.empty() && !push_constants_.empty()){
			vkCmdPushConstants(buffer, pipeline_layout_, pc_ranges[0].stageFlags,
				pc_ranges[0].offset, static_cast<std::uint32_t>(push_constants_.size()),
				push_constants_.data());
		}

		const auto& group_size = meta_.shader_info_.thread_group_size;
		auto groups = get_work_group_size(extent, group_size);
		vkCmdDispatch(buffer, groups.x, groups.y, 1);
	}


	[[nodiscard]] const vk::shader_module& shader() const{
		return meta_.shader_info_.shader;
	}

	[[nodiscard]] post_process_meta& meta(){
		return meta_;
	}

	[[nodiscard]] const post_process_meta& meta() const{
		return meta_;
	}
};
}
