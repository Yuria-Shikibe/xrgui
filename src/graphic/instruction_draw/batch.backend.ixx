// ReSharper disable CppExpressionWithoutSideEffects
module;

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.graphic.draw.instruction.batch.backend.vulkan;

export import mo_yanxi.graphic.draw.instruction.batch.common;
export import mo_yanxi.graphic.draw.instruction.batch.frontend;
import mo_yanxi.vk;
import mo_yanxi.vk.cmd;
import mo_yanxi.vk.util;
import mo_yanxi.type_register; // For user_data_index_table
import std;

namespace mo_yanxi::graphic::draw::instruction{
/**
 * @brief info per contiguous mesh dispatch
 *
 */
struct alignas(16) dispatch_config{
	std::uint32_t group_offset;
	std::uint32_t _cap[3]; // Padding to maintain alignment if necessary, or reserved
};

//
struct state_transition_command_context{
	vk::cmd::dependency_gen dependency{};
	std::vector<std::uint32_t> timelines{};
	std::vector<VkDeviceSize> buffer_offsets{};

	std::vector<VkBufferCopy> copy_info{};
	std::uint32_t current_submit_group_index{};

	[[nodiscard]] state_transition_command_context() = default;

	[[nodiscard]] explicit state_transition_command_context(std::size_t data_entry_count)
		: timelines(data_entry_count)
		, buffer_offsets(data_entry_count){
		dependency.buffer_memory_barriers.reserve(data_entry_count);
		copy_info.reserve(data_entry_count);
	}

	void submit_copy(VkCommandBuffer cmd, VkBuffer src, VkBuffer dst){
		if(copy_info.empty()) return;
		vkCmdCopyBuffer(cmd, src, dst, static_cast<std::uint32_t>(copy_info.size()), copy_info.data());
		copy_info.clear();
	}
};

//
VkDeviceSize load_data_group_to_buffer(
	const data_entry_group& group,
	const vk::allocator_usage& allocator,
	vk::buffer_cpu_to_gpu& gpu_buffer, VkBufferUsageFlags flags){
	VkDeviceSize required_size{};
	for(const auto& entry : group.entries){
		required_size += entry.get_required_byte_size();
	}

	if(gpu_buffer.get_size() < required_size){
		gpu_buffer = vk::buffer_cpu_to_gpu{
				allocator, required_size, flags
			};
	}

	vk::buffer_mapper mapper{gpu_buffer};
	VkDeviceSize cur_offset{};
	for(const auto& entry : group.entries){
		mapper.load_range(entry.get_data_span(), cur_offset);
		cur_offset += entry.get_required_byte_size();
	}

	return cur_offset;
}

struct data_layout_spec{
	struct entry{
		unsigned offset;
		unsigned unit_size;
	};

	struct subrange{
		unsigned offset;
		unsigned size;
	};

	instruction_buffer data{};
	std::vector<entry> entries{};

	inline void begin_push(){
		entries.resize(1, {});
	}

	inline void push(unsigned size, unsigned count){
		//TODO get required align from device
		auto& cur = entries.back();
		cur.unit_size = size;

		const auto aligned = vk::align_up(size, 64U);
		const auto total_req = aligned * count;

		entries.push_back({cur.offset + total_req});
	}

	inline unsigned size_at(unsigned index) const noexcept{
		return entries[index].unit_size;
	}

	inline unsigned offset_at(unsigned index, unsigned local_index) const noexcept{
		return entries[index].offset + local_index * vk::align_up(entries[index].unit_size, 64U);
	}

	inline VkDeviceAddressRangeEXT to_subrange(VkDeviceAddress pos, unsigned index) const noexcept{
		const auto off0 = offset_at(index, 0);
		const auto off1 = offset_at(index + 1, 0);
		return VkDeviceAddressRangeEXT{pos + off0, off1 - off0};
	}

	inline subrange operator[](unsigned index, unsigned local_index) const noexcept{
		const auto e = entries[index];
		return {e.offset + local_index * vk::align_up(e.unit_size, 64U), e.unit_size};
	}

	inline unsigned finalize() noexcept{
		auto sz = entries.back().offset;
		data.set_size(sz);
		return sz;
	}

	inline void load(unsigned index, const std::byte* src, unsigned count) const noexcept{
		const auto entry = entries[index];
		const auto dst = data.data() + entry.offset;
		for(unsigned i = 0; i < count; ++i){
			std::memcpy(dst + i * vk::align_up(entry.unit_size, 64U), src + i * entry.unit_size, entry.unit_size);
		}
	}

	template <std::ranges::input_range Rng>
		requires (std::is_trivially_copyable_v<std::remove_cvref_t<std::ranges::range_const_reference_t<Rng>>>)
	void load(unsigned index, Rng&& rng) const noexcept{
		const auto entry = entries[index];
		constexpr static unsigned sz = sizeof(std::remove_cvref_t<std::ranges::range_const_reference_t<Rng>>);
		assert(entry.unit_size == sz);
		const auto dst = data.data() + entry.offset;
		for(unsigned i = {}; const auto& val : rng){
			std::memcpy(dst + i * vk::align_up(sz, 64U), std::addressof(val), sz);
			++i;
		}
	}

	inline std::span<const std::byte> get_payload() const noexcept{
		return {data};
	}
};


struct frame_resource{
	static constexpr unsigned image_index = 3;

	vk::buffer_cpu_to_gpu buffer_dispatch_info{};
	vk::buffer_cpu_to_gpu buffer_instruction_heads{};
	vk::buffer_cpu_to_gpu buffer_instruction{};
	vk::buffer_cpu_to_gpu buffer_indirect{};
	vk::buffer_cpu_to_gpu buffer_sustained_info{};
	vk::buffer_cpu_to_gpu buffer_state_data{};

	std::vector<vk::binding_spec> bindings{
			{0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
			{1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
			{2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
			{image_index, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER}, // Dynamic count
			{4, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}, // Base for vertex UBOs
			{5, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
		};
	vk::dynamic_descriptor_buffer descriptor_buffer{};
	vk::descriptor_buffer volatile_descriptor_buffer{};

	std::vector<VkDeviceAddressRangeEXT> cache_buffer_ranges_{};

	frame_resource(const vk::allocator_usage& allocator,
		const vk::descriptor_layout& layout,
		const vk::descriptor_layout& volatile_layout, std::size_t required_buffer_upload_count)
		: buffer_indirect(allocator, sizeof(VkDrawMeshTasksIndirectCommandEXT) * 32,
			VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
		, descriptor_buffer(allocator, layout, layout.binding_count(), bindings)
		, volatile_descriptor_buffer(allocator, volatile_layout, volatile_layout.binding_count()),
		cache_buffer_ranges_(required_buffer_upload_count){
	}

	void upload_indirect(
		const vk::allocator_usage& allocator,
		const std::span<const VkDrawMeshTasksIndirectCommandEXT> submit_info){
		if(const auto reqSize = submit_info.size_bytes(); buffer_indirect.get_size() < reqSize){
			buffer_indirect = vk::buffer_cpu_to_gpu(allocator, reqSize, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
		}
		vk::buffer_mapper{buffer_indirect}.load_range(submit_info);
	}

	std::uint32_t upload_dispatch(
		const vk::allocator_usage& allocator,
		const draw_list_context& host_ctx,
		const std::span<const contiguous_draw_list> submit_group_subrange){
		std::uint32_t totalDispatchCount{};
		const auto dispatch_timeline_size = host_ctx.get_data_group_vertex_info().size() * sizeof(std::uint32_t);
		const auto dispatch_unit_size = sizeof(dispatch_group_info) + dispatch_timeline_size;

		VkDeviceSize deviceSize{};
		for(const auto& group : submit_group_subrange){
			deviceSize += group.get_dispatch_infos().size_bytes() + group.get_timeline_datas().size_bytes();
		}
		deviceSize += dispatch_unit_size; // Sentinel

		if(buffer_dispatch_info.get_size() < deviceSize){
			buffer_dispatch_info = vk::buffer_cpu_to_gpu{
					allocator, deviceSize,
					VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
				};
		}

		vk::buffer_mapper mapper{buffer_dispatch_info};
		std::vector<std::byte> buffer{};
		VkDeviceSize pushed_size{};
		std::uint32_t current_instr_offset{};
		std::uint32_t current_head_offset{};
		for(const auto& [idx, group] : submit_group_subrange | std::views::enumerate){
			const auto dispatch = group.get_dispatch_infos();
			const auto timeline = group.get_timeline_datas();
			buffer.resize(dispatch.size_bytes() + timeline.size_bytes());

			for(std::size_t i = 0; i < dispatch.size(); ++i){
				auto info = dispatch[i];
				info.instruction_head_index += current_head_offset;
				info.instruction_offset += current_instr_offset;
				std::memcpy(buffer.data() + dispatch_unit_size * i, &info, sizeof(info));
				std::memcpy(buffer.data() + dispatch_unit_size * i + sizeof(info),
					timeline.data() + i * host_ctx.get_data_group_vertex_info().size(), dispatch_timeline_size);
			}

			mapper.load_range(buffer, pushed_size);
			pushed_size += buffer.size();
			totalDispatchCount += static_cast<std::uint32_t>(dispatch.size());

			current_instr_offset += static_cast<std::uint32_t>(group.get_pushed_instruction_size());
			current_head_offset += group.get_instruction_heads().size();
		}

		// Add instruction sentinel
		mapper.load(dispatch_group_info{.instruction_offset = current_instr_offset}, pushed_size);

		return totalDispatchCount;
	}

	void upload_user_data(
		const vk::allocator_usage& allocator,
		const draw_list_context& host_ctx,
		const std::span<const VkDrawMeshTasksIndirectCommandEXT> submit_info,
		data_layout_spec& state_data_layout_cache_,
		std::vector<std::uint32_t>& cached_volatile_timelines){
		// Sustained
		load_data_group_to_buffer(host_ctx.get_data_group_vertex_info(), allocator, buffer_sustained_info,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

		// Volatile
		state_data_layout_cache_.begin_push();
		state_data_layout_cache_.push(sizeof(dispatch_config), submit_info.size());

		for(const auto& entry : host_ctx.get_data_group_non_vertex_info().entries){
			auto total = entry.get_count();
			state_data_layout_cache_.push(entry.unit_size, total);
		}
		state_data_layout_cache_.finalize();

		state_data_layout_cache_.load(0, submit_info | std::views::transform(
			[cur = 0u](const VkDrawMeshTasksIndirectCommandEXT& info) mutable{
				const dispatch_config rst{cur};
				cur += info.groupCountX;
				return rst;
			}));

		const auto& vtx_info = host_ctx.get_data_group_non_vertex_info();

		for(const auto& [idx, entry] : vtx_info.entries | std::views::enumerate){
			const auto total = entry.get_count();
			state_data_layout_cache_.load(1 + idx, entry.data(), total);
		}

		const auto payload = state_data_layout_cache_.get_payload();
		if(buffer_state_data.get_size() < payload.size()){
			buffer_state_data = vk::buffer_cpu_to_gpu{
					allocator, payload.size(),
					VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
				};
		}
		vk::buffer_mapper{buffer_state_data}.load_range(payload);

		if(volatile_descriptor_buffer.get_chunk_count() < submit_info.size() + 1){
			volatile_descriptor_buffer.set_chunk_count(submit_info.size() + 1);
		}

		cached_volatile_timelines.resize(vtx_info.size(), 0);

		vk::descriptor_mapper mapper{volatile_descriptor_buffer};

		auto load_timelines = [&](std::size_t current_chunk){
			mapper.set_uniform_buffer(0,
				buffer_state_data.get_address() + state_data_layout_cache_.offset_at(0, current_chunk),
				sizeof(dispatch_config), current_chunk);
			for(auto [idx, timeline] : cached_volatile_timelines | std::views::enumerate){
				auto [off, sz] = state_data_layout_cache_[idx + 1, timeline];
				mapper.set_uniform_buffer(idx + 1, buffer_state_data.get_address() + off, sz, current_chunk);
			}
		};

		load_timelines(0);
		auto breakpoints = host_ctx.get_state_transitions();
		for(const auto& [chunk_idx, breakpoint] : breakpoints | std::views::enumerate){
			for(const auto i : breakpoint.uniform_buffer_marching_indices){
				++cached_volatile_timelines[i];
			}
			load_timelines(chunk_idx + 1);
		}
	}

	void upload_instructions(
		const vk::allocator_usage& allocator,
		const std::span<const contiguous_draw_list> submit_group_subrange,
		std::uint32_t& instructionHeadSize,
		std::uint32_t& instructionSize){
		const auto head_size_view = submit_group_subrange | std::views::transform([](const contiguous_draw_list& list){
			return list.get_instruction_heads().size_bytes();
		});
		const auto payload_size_view = submit_group_subrange | std::views::transform(
			[](const contiguous_draw_list& list){
				return list.get_pushed_instruction_size();
			});
		instructionHeadSize = std::reduce(head_size_view.begin(), head_size_view.end(), 0u, std::plus<>{});
		instructionSize = std::reduce(payload_size_view.begin(), payload_size_view.end(), 0u, std::plus<>{});

		if(buffer_instruction_heads.get_size() < instructionHeadSize){
			buffer_instruction_heads = vk::buffer_cpu_to_gpu{
					allocator, instructionHeadSize,
					VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
				};
		}

		if(buffer_instruction.get_size() < instructionSize){
			buffer_instruction = vk::buffer_cpu_to_gpu{
					allocator, instructionSize,
					VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
				};
		}

		{
			vk::buffer_mapper mapper{buffer_instruction};
			std::size_t current_offset{};
			for(const auto& [idx, submit_group] : submit_group_subrange | std::views::enumerate){
				(void)mapper.load_range(std::span{
						submit_group.get_buffer_data(), submit_group.get_pushed_instruction_size()
					}, current_offset);
				current_offset += submit_group.get_pushed_instruction_size();
			}
		}

		{
			vk::buffer_mapper mapper{buffer_instruction_heads};
			std::size_t current_offset{};
			for(const auto& [idx, submit_group] : submit_group_subrange | std::views::enumerate){
				const auto spn = submit_group.get_instruction_heads();
				(void)mapper.load_range(spn, current_offset);
				current_offset += spn.size_bytes();
			}
		}
	}

	bool update_descriptors(
		const draw_list_context& host_ctx,
		const vk::descriptor_layout& descriptor_layout,
		VkSampler sampler,
		std::uint32_t totalDispatchCount,
		std::uint32_t instructionHeadSize,
		std::uint32_t instructionSize){
		bool requires_command_record = false;
		// Update Dynamic Descriptor Buffer
		if(const auto cur_size = host_ctx.get_used_images<void*>().size(); bindings[image_index].count != cur_size){
			bindings[image_index].count = static_cast<std::uint32_t>(cur_size);
			descriptor_buffer.reconfigure(descriptor_layout, descriptor_layout.binding_count(), bindings);
			requires_command_record = true;
		}

		const auto dispatch_timeline_size = host_ctx.get_data_group_vertex_info().size() * sizeof(std::uint32_t);
		const auto dispatch_unit_size = sizeof(dispatch_group_info) + dispatch_timeline_size;

		vk::dynamic_descriptor_mapper dbo_mapper{descriptor_buffer};
		dbo_mapper.set_element_at(0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, buffer_dispatch_info.get_address(),
			dispatch_unit_size * (1 + totalDispatchCount));
		dbo_mapper.set_element_at(1, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, buffer_instruction_heads.get_address(),
			instructionHeadSize);
		dbo_mapper.set_element_at(2, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, buffer_instruction.get_address(),
			instructionSize);
		dbo_mapper.set_images_at(image_index, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, sampler,
			host_ctx.get_used_images<VkImageView>());

		VkDeviceSize cur_offset{};
		for(const auto& [i, entry] : host_ctx.get_data_group_vertex_info().entries | std::views::enumerate){
			dbo_mapper.set_element_at(image_index + 1 + i, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				buffer_sustained_info.get_address() + cur_offset, entry.get_required_byte_size());
			cur_offset += entry.get_required_byte_size();
		}
		return requires_command_record;
	}

	void push_back_to_descriptor_heap(
		std::uint32_t buffer_section,
		std::uint32_t image_section,
		vk::resource_descriptor_heap& heap,

		const draw_list_context& host_ctx,
		const data_layout_spec& state_data_layout_cache_,
		std::uint32_t totalDispatchCount,
		std::uint32_t instructionHeadSize,
		std::uint32_t instructionSize
		){
			{
				//load buffers
				const auto dispatch_timeline_size = host_ctx.get_data_group_vertex_info().size() * sizeof(std::uint32_t);
				const auto dispatch_unit_size = sizeof(dispatch_group_info) + dispatch_timeline_size;

				cache_buffer_ranges_[0] = {buffer_dispatch_info.get_address(), dispatch_unit_size * (1 + totalDispatchCount)};
				cache_buffer_ranges_[1] = {buffer_instruction_heads.get_address(), instructionHeadSize};
				cache_buffer_ranges_[2] = {buffer_instruction.get_address(), instructionSize};

				VkDeviceSize cur_offset{};
				for(const auto& [i, entry] : host_ctx.get_data_group_vertex_info().entries | std::views::enumerate){
					cache_buffer_ranges_[2 + 1 + i] = {buffer_sustained_info.get_address() + cur_offset, entry.get_required_byte_size()};
					cur_offset += entry.get_required_byte_size();
				}

				//UBO begin
				auto v1_size = host_ctx.get_data_group_vertex_info().size();
				cache_buffer_ranges_[2 + v1_size + 1] = state_data_layout_cache_.to_subrange(buffer_state_data.get_address(), 0);

				for(const auto& [idx, entry] : host_ctx.get_data_group_non_vertex_info().entries | std::views::enumerate){
					cache_buffer_ranges_[2 + v1_size + 2 + idx] = state_data_layout_cache_.to_subrange(buffer_state_data.get_address(), idx + 1);
				}

				heap.clear(buffer_section);
				heap.push_back_storage_buffers(buffer_section, {cache_buffer_ranges_.begin(), 2 + v1_size});
				heap.push_back_uniform_buffers(buffer_section, {cache_buffer_ranges_.begin() + 2 + v1_size, cache_buffer_ranges_.end()});
			}

			{

			}
	}
};

export class batch_vulkan_executor{
private:
	vk::allocator_usage allocator_{};

	std::vector<VkDrawMeshTasksIndirectCommandEXT> submit_info_{};
	std::vector<frame_resource> frames_{};

	data_layout_spec volatile_data_layout_{};
	std::vector<std::uint32_t> cached_volatile_timelines_{};

	/**
	 * @brief Descriptor maintain the same during all the mesh dispatch.
	 */
	vk::descriptor_layout descriptor_layout_{};
	/**
	 * @brief Descriptor varies during each draw dispatch, used for uniform buffer update mainly.
	 */
	vk::descriptor_layout state_descriptor_layout_{};

	VkDeviceSize offset_ceil(std::size_t size) const noexcept{
		//TODO check device real limit
		return vk::align_up(size, 64uz);
	}

public:
	[[nodiscard]] batch_vulkan_executor() = default;

	[[nodiscard]] explicit batch_vulkan_executor(
		const vk::allocator_usage& a,
		const draw_list_context& batch_host,
		std::size_t frames_in_flight = 3
	)
		: allocator_{a}
		, descriptor_layout_(allocator_.get_device(), VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
			[&](vk::descriptor_layout_builder& builder){
				// dispatch group info
				builder.push_seq(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);
				// instructions
				builder.push_seq(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);
				builder.push_seq(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);
				// Textures (partially bound)
				builder.push_seq(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1,
					VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);
				// vertex ubos
				for(unsigned i = 0; i < batch_host.get_data_group_vertex_info().size(); ++i){
					builder.push_seq(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);
				}
			})
		, state_descriptor_layout_{
			a.get_device(),
			VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
			[&](vk::descriptor_layout_builder& builder){
				builder.push_seq(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);
				for(std::size_t i = 0; i < batch_host.get_data_group_non_vertex_info().size(); ++i){
					builder.push_seq(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT);
				}
			}
		}{
		frames_.reserve(frames_in_flight);
		for(std::size_t i = 0; i < frames_in_flight; ++i){
			frames_.emplace_back(allocator_, descriptor_layout_, state_descriptor_layout_, get_required_buffer_descriptor_count_per_frame(batch_host));
		}
	}

	bool upload(draw_list_context& host_ctx,
		VkSampler sampler,
		std::uint32_t frame_index
	){
		auto& frame = frames_[frame_index];
		const auto submit_group_subrange = host_ctx.get_valid_submit_groups();
		if(submit_group_subrange.empty()){
			return false;
		}

		// 1. Prepare Indirect Commands
		const auto dispatchCountGroups = [&]{
			submit_info_.resize(host_ctx.get_state_transitions().size());
			std::uint32_t currentSubmitGroupIndex = 0;
			for(const auto& [idx, submit_breakpoint] : host_ctx.get_state_transitions() | std::views::enumerate){
				const auto section_end = submit_breakpoint.break_before_index;
				std::uint32_t submitCount{};
				for(auto i = currentSubmitGroupIndex; i < section_end; ++i){
					submitCount += static_cast<std::uint32_t>(submit_group_subrange[i].get_dispatch_infos().
						size());
				}
				submit_info_[idx] = {submitCount, 1, 1};
				currentSubmitGroupIndex = section_end;
			}
			return std::span{std::as_const(submit_info_)};
		}();

		frame.upload_indirect(allocator_, dispatchCountGroups);

		std::uint32_t totalDispatchCount = frame.upload_dispatch(allocator_, host_ctx, submit_group_subrange);

		frame.upload_user_data(allocator_, host_ctx, dispatchCountGroups, volatile_data_layout_,
			cached_volatile_timelines_);

		std::uint32_t instructionHeadSize = 0;
		std::uint32_t instructionSize = 0;
		frame.upload_instructions(allocator_, submit_group_subrange, instructionHeadSize, instructionSize);

		return frame.update_descriptors(host_ctx, descriptor_layout_, sampler, totalDispatchCount, instructionHeadSize,
			instructionSize);
	}

	std::array<VkDescriptorSetLayout, 2> get_descriptor_set_layout() const noexcept{
		return {descriptor_layout_, state_descriptor_layout_};
	}

	template <typename T = std::allocator<descriptor_buffer_usage>>
	void load_descriptors(record_context<T>& record_context, std::uint32_t frame_index){
		auto& frame = frames_[frame_index];
		record_context.push(0, frame.descriptor_buffer);
		record_context.push(1, frame.volatile_descriptor_buffer, frame.volatile_descriptor_buffer.get_chunk_size());
	}

	bool is_section_empty(std::uint32_t frame_index) const noexcept{
		return submit_info_[frame_index].groupCountX == 0;
	}

	void cmd_draw(VkCommandBuffer cmd, std::uint32_t dispatch_group_index, std::uint32_t frame_index) const{
		vk::cmd::drawMeshTasksIndirect(cmd, frames_[frame_index].buffer_indirect,
			dispatch_group_index * sizeof(VkDrawMeshTasksIndirectCommandEXT), 1);
	}

	std::uint32_t get_required_buffer_descriptor_count_per_frame(const draw_list_context& host_ctx) const noexcept{
		return 3 + host_ctx.get_data_group_vertex_info().size() + 1 + host_ctx.get_data_group_non_vertex_info().size();
	}
};
}
