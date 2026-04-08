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

namespace mo_yanxi::graphic::draw::instruction {

// 严格对应 Slang 中的 vertex_resolve_info，16 Bytes 布局
struct alignas(16) vertex_resolve_info {
	std::uint32_t packed_type_timeline; // [15:0]: instr_type, [31:16]: relative_timeline
	std::uint32_t payload_offset;       // 绝对 Payload 偏移
	std::uint32_t packed_skips;         // [15:0]: vtx_skip, [31:16]: prm_skip (0xFFFF 为哨兵)
	std::uint32_t payload_size;         // 该指令的真实 Payload 尺寸
};

// 严格对应 Slang 中的 mesh_dispatch_info_v3
struct alignas(16) mesh_dispatch_info_v3 {
	std::uint32_t global_vertex_offset;
	std::uint32_t primitives;
	std::uint32_t base_timeline_index;
	std::uint32_t cap;
};

} // namespace mo_yanxi::graphic::draw::instruction

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
	static constexpr std::uint32_t align = 64U;

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
		cur.offset = vk::align_up(cur.offset, align);
		cur.unit_size = size;

		const auto aligned = vk::align_up(size, align);
		const auto total_req = aligned * count;

		entries.push_back({cur.offset + total_req});
	}

	inline unsigned size_at(unsigned index) const noexcept{
		return entries[index].unit_size;
	}

	inline unsigned offset_at(unsigned index, unsigned local_index) const noexcept{
		return entries[index].offset + local_index * vk::align_up(entries[index].unit_size, align);
	}

	inline VkDeviceAddressRangeEXT to_subrange(VkDeviceAddress pos, unsigned index) const noexcept{
		const auto off0 = offset_at(index, 0);
		const auto off1 = offset_at(index + 1, 0);
		return VkDeviceAddressRangeEXT{pos + off0, off1 - off0};
	}

	inline subrange operator[](unsigned index, unsigned local_index) const noexcept{
		const auto e = entries[index];
		return {e.offset + local_index * vk::align_up(e.unit_size, align), e.unit_size};
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
			std::memcpy(dst + i * vk::align_up(entry.unit_size, align), src + i * entry.unit_size, entry.unit_size);
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
			std::memcpy(dst + i * vk::align_up(sz, align), std::addressof(val), sz);
			++i;
		}
	}

	inline std::span<const std::byte> get_payload() const noexcept{
		return {data};
	}
};


export
std::uint32_t get_required_buffer_descriptor_count_per_frame(const draw_list_context& host_ctx) noexcept{
    // 将原本的 3 个核心缓冲变更为 4 个 (Dispatch, Resolve, Timeline, Payload)
    return 4 + host_ctx.get_data_group_vertex_info().size() + (1 + host_ctx.get_data_group_non_vertex_info().size());
}

struct instruction_resolve_info{
	std::vector<std::uint32_t> timelines;
	std::vector<vertex_resolve_info> thread_resolve_info;
	std::vector<mesh_dispatch_info_v3> group_dispatch_info;

	void update(const draw_list_context& host_ctx){
		const auto submit_group_subrange = host_ctx.get_valid_submit_groups();

		std::uint32_t current_global_vertex_base = 0;
		std::uint32_t current_global_payload_base = 0;
		constexpr std::uint32_t VERTEX_PER_MESH_LIMIT = 64;

		const std::uint32_t num_timeline_slots = static_cast<std::uint32_t>(host_ctx.get_data_group_vertex_info().size());

		// 1. 预先计算需要的总容量，避免动态扩容
		std::size_t total_dispatch_count = 0;
		for(const auto& group : submit_group_subrange){
			total_dispatch_count += group.get_dispatch_infos().size();
		}

		timelines.clear();
		// 使用 reserve 为不可严格预估大小的容器预分配内存
		group_dispatch_info.clear();
		group_dispatch_info.reserve(total_dispatch_count);

		// 2. 直接 resize 拍平的顶点映射缓冲，避免所有的 push_back 边界检查
		thread_resolve_info.resize(total_dispatch_count * VERTEX_PER_MESH_LIMIT, {});
		std::size_t thread_write_idx = 0; // 记录当前写入的全局索引

		std::vector<std::uint32_t> current_timeline_state;
		bool timeline_dirty = false;

		if(num_timeline_slots > 0){
			timelines.resize(num_timeline_slots, 0);
			current_timeline_state.resize(num_timeline_slots, 0); // 初始化当前状态
		}

		// 通过脏标记记录更新，合并连续的 timeline 变化
		auto mark_timeline_dirty = [&](std::uint32_t slot_idx){
			current_timeline_state[slot_idx]++;
			timeline_dirty = true;
		};

		for(const auto& group : submit_group_subrange){
			const auto dispatches = group.get_dispatch_infos();
			const auto heads = group.get_instruction_heads();
			for(std::size_t i = 0; i < dispatches.size(); ++i){
				const auto& dispatch_info = dispatches[i];

				mesh_dispatch_info_v3 v3_info{};
				v3_info.global_vertex_offset = current_global_vertex_base;
				v3_info.primitives = dispatch_info.primitive_count;
				if(num_timeline_slots > 0){
					v3_info.base_timeline_index = static_cast<std::uint32_t>(timelines.size()) / num_timeline_slots - 1;
				} else{
					v3_info.base_timeline_index = 0;
				}

				// 这里由于使用了 reserve，push_back 也非常快，且数量是 total_dispatch_count，压力不大
				group_dispatch_info.push_back(v3_info);

				const std::uint32_t patch_til_index = dispatch_info.vertex_offset ? 2 : 0;

				std::uint32_t skipped_vertices = 0;
				std::int32_t skipped_primitives = -static_cast<std::int32_t>(dispatch_info.primitive_offset);

				std::uint32_t ptr_to_payload = current_global_payload_base + dispatch_info.instruction_offset;
				std::uint32_t idx_to_head = dispatch_info.instruction_head_index;
				std::uint32_t relative_timeline = 0;

				for(std::uint32_t target_index = 0; target_index < VERTEX_PER_MESH_LIMIT; ++target_index){
					std::uint32_t thread_vtx_skip = target_index + dispatch_info.vertex_offset;

					// 直接通过索引获取当前内存的引用，就地修改，避免构造和拷贝开销
					vertex_resolve_info& resolve_res = thread_resolve_info[thread_write_idx++];

					resolve_res.packed_type_timeline = 0;
					resolve_res.payload_offset = 0;
					resolve_res.packed_skips = 0xFFFF0000;
					resolve_res.payload_size = 0;

					bool is_found = false;
					while(idx_to_head < heads.size()){
						if(skipped_primitives >= static_cast<std::int32_t>(dispatch_info.primitive_count)){
							break;
						}

						const auto& head = heads[idx_to_head];

						if(head.type == instr_type::uniform_update){
							if(num_timeline_slots > 0){
								// 【优化】记录脏状态，不立即追加
								mark_timeline_dirty(head.payload.marching_data.index);
							}
							// 跨越多条 timeline 修改时，relative_timeline 不再无脑暴增
							idx_to_head++;
							continue;
						}

						const std::uint32_t head_vtx_count = head.payload.draw.vertex_count;
						const std::uint32_t local_skip = thread_vtx_skip - skipped_vertices;
						if(local_skip < head_vtx_count){
							is_found = true;
							break;
						}

						ptr_to_payload += head.payload_size;
						skipped_vertices += head_vtx_count;
						skipped_primitives += head.payload.draw.primitive_count;
						idx_to_head++;
					}

					if(is_found){
						const auto& head = heads[idx_to_head];
						const std::uint32_t local_skip = thread_vtx_skip - skipped_vertices;

						std::uint32_t idc_idx = 0xFFFF;
						if(target_index >= patch_til_index && local_skip >= 2){
							idc_idx = static_cast<std::uint32_t>(skipped_primitives) + local_skip - 2;
						}

						// 【新增】如果 timeline 已脏，说明之前有缓存的 uniform_update 变更。
						// 由于当前遇到实际需要发射的顶点了，正式合并并提交状态。
						if(timeline_dirty) {
							timelines.insert(timelines.end(), current_timeline_state.begin(), current_timeline_state.end());
							relative_timeline++;
							timeline_dirty = false;
						}

						resolve_res.packed_type_timeline = static_cast<std::uint32_t>(head.type) | (relative_timeline << 16);
						resolve_res.payload_offset = ptr_to_payload;
						resolve_res.packed_skips = (local_skip & 0xFFFF) | (idc_idx << 16);
						resolve_res.payload_size = head.payload_size;
					}
				}

				// --- 扫尾阶段：专属边界，只为拦截剩余的 uniform_update ---
				const std::uint32_t sweep_end_idx = (i + 1 < dispatches.size())
					                                    ? dispatches[i + 1].instruction_head_index
					                                    : static_cast<std::uint32_t>(heads.size());
				while(idx_to_head < sweep_end_idx){
					const auto& head = heads[idx_to_head];
					if(head.type == instr_type::uniform_update){
						if(num_timeline_slots > 0){
							// 【优化】同样只做脏标记记录
							mark_timeline_dirty(head.payload.marching_data.index);
						}
					}
					idx_to_head++;
				}

				current_global_vertex_base += VERTEX_PER_MESH_LIMIT;
			}

			current_global_payload_base += static_cast<std::uint32_t>(group.get_pushed_instruction_size());
		}
	}
};

struct frame_resource{
    // 纹理绑定索引向后移动 1 位
    static constexpr unsigned image_index = 4;

    vk::buffer_cpu_to_gpu buffer_dispatch_info{};
    vk::buffer_cpu_to_gpu buffer_resolve_infos{}; // 新增：拍平的顶点映射缓冲
    vk::buffer_cpu_to_gpu buffer_timelines{};     // 新增：拍平的 Timeline 快照缓冲
    vk::buffer_cpu_to_gpu buffer_instruction{};   // Payload 缓冲保持不变

    // vk::buffer_cpu_to_gpu buffer_indirect{};
    vk::buffer_cpu_to_gpu buffer_volatile_data{};
    vk::buffer_cpu_to_gpu buffer_per_draw_call_data{};

    std::vector<vk::binding_spec> bindings{};
    vk::dynamic_descriptor_buffer descriptor_buffer{};
    vk::descriptor_buffer descriptor_buffer_per_draw_call{};

    std::vector<VkDeviceAddressRangeEXT> cache_buffer_ranges_{};
    std::vector<std::uint32_t> dispatch_timeline_stamps_{};

    frame_resource(const vk::allocator_usage& allocator,
        const vk::descriptor_layout& layout,
        const vk::descriptor_layout& volatile_layout, const draw_list_context& batch_frontend)
        :
	// buffer_indirect(allocator, sizeof(VkDrawMeshTasksIndirectCommandEXT) * 32,
 //            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT) ,
        bindings{[&]{
            std::vector<vk::binding_spec> bindings{
                {0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}, // V3 Dispatch Info
                {1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}, // Thread Resolve Infos
                {2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}, // Timelines
                {3, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}, // Payload bytes
                {image_index, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER}};
            for(unsigned i = 0; i < batch_frontend.get_data_group_vertex_info().size(); ++i){
                bindings.push_back({image_index + 1 + i, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
            }
            return bindings;
        }()}
        , descriptor_buffer(allocator, layout, layout.binding_count(), bindings)
        , descriptor_buffer_per_draw_call(allocator, volatile_layout, volatile_layout.binding_count()),
        cache_buffer_ranges_(get_required_buffer_descriptor_count_per_frame(batch_frontend)){
    }

	// void upload_indirect(
	// 	const vk::allocator_usage& allocator,
	// 	const std::span<const VkDrawMeshTasksIndirectCommandEXT> submit_info){
	// 	if(const auto reqSize = submit_info.size_bytes(); buffer_indirect.get_size() < reqSize){
	// 		buffer_indirect = vk::buffer_cpu_to_gpu(allocator, reqSize, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
	// 	}
	// 	vk::buffer_mapper{buffer_indirect}.load_range(submit_info);
	// }

	void upload_v3_data(
        const vk::allocator_usage& allocator,
        const instruction_resolve_info& resolve_info,
        const std::span<const contiguous_draw_list> submit_group_subrange,
        std::uint32_t& payloadSize
    ){
        const VkBufferUsageFlags storage_flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

        // 1. Upload group_dispatch_info (V3)
        const auto dispatch_bytes = resolve_info.group_dispatch_info.size() * sizeof(mesh_dispatch_info_v3);
        if(dispatch_bytes > 0){
            if(buffer_dispatch_info.get_size() < dispatch_bytes){
                buffer_dispatch_info = vk::buffer_cpu_to_gpu{allocator, dispatch_bytes, storage_flags};
            }
            vk::buffer_mapper{buffer_dispatch_info}.load_range(resolve_info.group_dispatch_info);
        }

        // 2. Upload thread_resolve_info
        const auto resolve_bytes = resolve_info.thread_resolve_info.size() * sizeof(vertex_resolve_info);
        if(resolve_bytes > 0){
            if(buffer_resolve_infos.get_size() < resolve_bytes){
                buffer_resolve_infos = vk::buffer_cpu_to_gpu{allocator, resolve_bytes, storage_flags};
            }
            vk::buffer_mapper{buffer_resolve_infos}.load_range(resolve_info.thread_resolve_info);
        }

        // 3. Upload timelines
        const auto timeline_bytes = resolve_info.timelines.size() * sizeof(std::uint32_t);
        if(timeline_bytes > 0){
            if(buffer_timelines.get_size() < timeline_bytes){
                buffer_timelines = vk::buffer_cpu_to_gpu{allocator, timeline_bytes, storage_flags};
            }
            vk::buffer_mapper{buffer_timelines}.load_range(resolve_info.timelines);
        }

        // 4. Upload instruction payload
        const auto payload_size_view = submit_group_subrange | std::views::transform(
            [](const contiguous_draw_list& list){ return list.get_pushed_instruction_size(); });
        payloadSize = std::reduce(payload_size_view.begin(), payload_size_view.end(), 0u, std::plus<>{});

        if(payloadSize > 0){
            if(buffer_instruction.get_size() < payloadSize){
                buffer_instruction = vk::buffer_cpu_to_gpu{allocator, payloadSize, storage_flags};
            }
            vk::buffer_mapper mapper{buffer_instruction};
            std::size_t current_offset{};
            for(const auto& submit_group : submit_group_subrange){
                const auto sz = submit_group.get_pushed_instruction_size();
                if(sz > 0){
                    (void)mapper.load_range(std::span{submit_group.get_buffer_data(), sz}, current_offset);
                    current_offset += sz;
                }
            }
        }
    }

	void upload_user_data(
		const vk::allocator_usage& allocator,
		const draw_list_context& host_ctx,
		const std::span<const VkDrawMeshTasksIndirectCommandEXT> submit_info,
		data_layout_spec& state_data_layout_cache_,
		std::vector<std::uint32_t>& cached_volatile_timelines){
		// Sustained
		load_data_group_to_buffer(host_ctx.get_data_group_vertex_info(), allocator, buffer_volatile_data,
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
		if(buffer_per_draw_call_data.get_size() < payload.size()){
			buffer_per_draw_call_data = vk::buffer_cpu_to_gpu{
					allocator, payload.size(),
					VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
				};
		}
		vk::buffer_mapper{buffer_per_draw_call_data}.load_range(payload);

		if(descriptor_buffer_per_draw_call.get_chunk_count() < submit_info.size() + 1){
			descriptor_buffer_per_draw_call.set_chunk_count(submit_info.size() + 1);
		}

		auto breakpoints = host_ctx.get_state_transitions();
		cached_volatile_timelines.resize(vtx_info.size(), 0);
		auto dispatch_timeline_chunk_size = (cached_volatile_timelines.size() + 1);
		dispatch_timeline_stamps_.resize(dispatch_timeline_chunk_size * (breakpoints.size() + 1));

		vk::descriptor_mapper mapper{descriptor_buffer_per_draw_call};

		auto load_timelines = [&](std::size_t current_chunk){
			mapper.set_uniform_buffer(0,
				buffer_per_draw_call_data.get_address() + state_data_layout_cache_.offset_at(0, current_chunk),
				sizeof(dispatch_config), current_chunk);
			for(auto [idx, timeline] : cached_volatile_timelines | std::views::enumerate){
				auto [off, sz] = state_data_layout_cache_[idx + 1, timeline];
				mapper.set_uniform_buffer(idx + 1, buffer_per_draw_call_data.get_address() + off, sz, current_chunk);
			}
		};

		load_timelines(0);
		for(const auto& [chunk_idx, breakpoint] : breakpoints | std::views::enumerate){
			for(const auto i : breakpoint.uniform_buffer_marching_indices){
				++cached_volatile_timelines[i];
			}

			auto where = dispatch_timeline_stamps_.begin() + (chunk_idx + 1) * dispatch_timeline_chunk_size;
			*where = chunk_idx;
			std::ranges::copy(cached_volatile_timelines, std::ranges::next(where));
			load_timelines(chunk_idx + 1);
		}
	}


	bool update_descriptors(
        const draw_list_context& host_ctx,
        const vk::descriptor_layout& descriptor_layout,
        VkSampler sampler,
        const instruction_resolve_info& resolve_info,
        std::uint32_t payloadSize
    ){
        bool requires_command_record = false;
        if(const auto cur_size = host_ctx.get_used_images<void*>().size(); bindings[image_index].count != cur_size){
            bindings[image_index].count = static_cast<std::uint32_t>(cur_size);
            descriptor_buffer.reconfigure(descriptor_layout, descriptor_layout.binding_count(), bindings);
            requires_command_record = true;
        }

        vk::dynamic_descriptor_mapper dbo_mapper{descriptor_buffer};

        // 绑定 0-3 分别为 V3的四大结构
        const auto dispatch_bytes = resolve_info.group_dispatch_info.size() * sizeof(mesh_dispatch_info_v3);
        const auto resolve_bytes  = resolve_info.thread_resolve_info.size() * sizeof(vertex_resolve_info);
        const auto timeline_bytes = resolve_info.timelines.size() * sizeof(std::uint32_t);

        dbo_mapper.set_element_at(0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, buffer_dispatch_info.get_address(), dispatch_bytes);
        dbo_mapper.set_element_at(1, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, buffer_resolve_infos.get_address(), resolve_bytes);
        dbo_mapper.set_element_at(2, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, buffer_timelines.get_address(), timeline_bytes);
        dbo_mapper.set_element_at(3, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, buffer_instruction.get_address(), payloadSize);

        // 绑定 4 为 Textures
        dbo_mapper.set_images_at(image_index, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, sampler,
            host_ctx.get_used_images<VkImageView>());

        VkDeviceSize cur_offset{};
        for(const auto& [i, entry] : host_ctx.get_data_group_vertex_info().entries | std::views::enumerate){
            dbo_mapper.set_element_at(image_index + 1 + i, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                buffer_volatile_data.get_address() + cur_offset, entry.get_required_byte_size());
            cur_offset += entry.get_required_byte_size();
        }
        return requires_command_record;
    }

	void push_back_to_descriptor_heap(
		std::uint32_t buffer_section,
		vk::resource_descriptor_heap& heap,

		const draw_list_context& host_ctx,
		const data_layout_spec& state_data_layout_cache_,
		std::uint32_t totalDispatchCount,
		std::uint32_t instructionHeadSize,
		std::uint32_t instructionSize
	){
		// const auto dispatch_timeline_size = host_ctx.get_data_group_vertex_info().size() * sizeof(std::uint32_t);
		// const auto dispatch_unit_size = sizeof(dispatch_group_info) + dispatch_timeline_size;
		//
		// cache_buffer_ranges_[0] = {buffer_dispatch_info.get_address(), dispatch_unit_size * (1 + totalDispatchCount)};
		// cache_buffer_ranges_[1] = {buffer_instruction_heads.get_address(), instructionHeadSize};
		// cache_buffer_ranges_[2] = {buffer_instruction.get_address(), instructionSize};
		//
		// VkDeviceSize cur_offset{};
		// for(const auto& [i, entry] : host_ctx.get_data_group_vertex_info().entries | std::views::enumerate){
		// 	cache_buffer_ranges_[3 + i] = {
		// 			buffer_sustained_info.get_address() + cur_offset, entry.get_required_byte_size()
		// 		};
		// 	cur_offset += entry.get_required_byte_size();
		// }
		//
		// //UBO begin
		// const auto v1_size = host_ctx.get_data_group_vertex_info().size();
		// const auto ssbo_count = 3 + v1_size; // 正确的总数
		//
		// cache_buffer_ranges_[ssbo_count] = state_data_layout_cache_.
		// 	to_subrange(buffer_state_data.get_address(), 0);
		//
		// for(const auto& [idx, entry] : host_ctx.get_data_group_non_vertex_info().entries | std::views::enumerate){
		// 	cache_buffer_ranges_[ssbo_count + 1 + idx] = state_data_layout_cache_.to_subrange(
		// 		buffer_state_data.get_address(), idx + 1);
		// }
		//
		// heap.clear(buffer_section);
		// heap.push_back_storage_buffers(buffer_section, cache_buffer_ranges_);
		// heap.push_back_storage_buffers(buffer_section, {cache_buffer_ranges_.begin(), 2 + v1_size});
		//
		//
		// heap.push_back_storage_buffers(buffer_section, {
		// 		cache_buffer_ranges_.begin() + 2 + v1_size, cache_buffer_ranges_.end()
		// 	});
	}
};

export class batch_vulkan_executor{
private:
	vk::allocator_usage allocator_{};

	std::vector<VkDrawMeshTasksIndirectCommandEXT> cache_submit_info_{};
	std::vector<frame_resource> frames_{};

	data_layout_spec volatile_data_layout_{};
	std::vector<std::uint32_t> cached_state_timelines_{};
	instruction_resolve_info cached_instruction_resolve_info_{};

	/**
	 * @brief Descriptor maintain the same during all the mesh dispatch.
	 */
	vk::descriptor_layout descriptor_layout_{};
	/**
	 * @brief Descriptor varies during each draw dispatch, used for uniform buffer update mainly.
	 */
	vk::descriptor_layout per_draw_call_descriptor_layout_{};

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
			// 0: group_dispatch_info_v3
			builder.push_seq(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);
			// 1: thread_resolve_info
			builder.push_seq(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);
			// 2: timelines
			builder.push_seq(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);
			// 3: instructions payload
			builder.push_seq(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);

			// 4: Textures (partially bound)
			builder.push_seq(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1,
				VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);

			// 5+: vertex ubos
			for(unsigned i = 0; i < batch_host.get_data_group_vertex_info().size(); ++i){
				builder.push_seq(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);
			}
		})
		, per_draw_call_descriptor_layout_{
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
			frames_.emplace_back(allocator_, descriptor_layout_, per_draw_call_descriptor_layout_, batch_host);
		}
	}


	void upload(
		std::uint32_t target_section,
		vk::resource_descriptor_heap& resource_descriptor_heap,
		draw_list_context& host_ctx,
		VkSampler sampler,
		std::uint32_t frame_index
	){
		cached_state_timelines_.clear();

		cached_instruction_resolve_info_.update(host_ctx);

		auto& frame = frames_[frame_index];
		const auto submit_group_subrange = host_ctx.get_valid_submit_groups();
		if(submit_group_subrange.empty()){
			return;
		}

		// 1. Prepare Indirect Commands
		const auto dispatchCountGroups = [&]{
			cache_submit_info_.resize(host_ctx.get_state_transitions().size());
			std::uint32_t currentSubmitGroupIndex = 0;
			for(const auto& [idx, submit_breakpoint] : host_ctx.get_state_transitions() | std::views::enumerate){
				const auto section_end = submit_breakpoint.break_before_index;
				std::uint32_t submitCount{};
				for(auto i = currentSubmitGroupIndex; i < section_end; ++i){
					submitCount += static_cast<std::uint32_t>(submit_group_subrange[i].get_dispatch_infos().size());
				}
				cache_submit_info_[idx] = {submitCount, 1, 1};
				currentSubmitGroupIndex = section_end;
			}
			return std::span{std::as_const(cache_submit_info_)};
		}();

		// frame.upload_indirect(allocator_, dispatchCountGroups);

		std::uint32_t payloadSize = 0;
		frame.upload_v3_data(allocator_, cached_instruction_resolve_info_, submit_group_subrange, payloadSize);

		frame.upload_user_data(allocator_, host_ctx, dispatchCountGroups, volatile_data_layout_,
			cached_state_timelines_);

		// 更新描述符，传入 V3 预演缓存和最终计算出的 Payload 尺寸
		frame.update_descriptors(host_ctx, descriptor_layout_, sampler, cached_instruction_resolve_info_, payloadSize);

		// 如果要恢复 push_back_to_descriptor_heap，请注意调整里面的 Cache_Buffer_Ranges 的索引 (0,1,2,3 分配给SSBO)
	}

	std::array<VkDescriptorSetLayout, 2> get_descriptor_set_layout() const noexcept{
		return {descriptor_layout_, per_draw_call_descriptor_layout_};
	}

	template <typename T = std::allocator<descriptor_buffer_usage>>
	void load_descriptors(record_context<T>& record_context, std::uint32_t frame_index){
		auto& frame = frames_[frame_index];
		record_context.push(0, frame.descriptor_buffer);
		record_context.push(1, frame.descriptor_buffer_per_draw_call, frame.descriptor_buffer_per_draw_call.get_chunk_size());
	}

	std::span<const std::uint32_t> get_state_buffer_indices(const draw_list_context& host_ctx, std::uint32_t frame_index, std::uint32_t drawlist_index) const noexcept{
		auto& frame = frames_[frame_index];
		auto chunksize = (1 + host_ctx.get_data_group_non_vertex_info().size());
		return {frame.dispatch_timeline_stamps_.begin() + chunksize * drawlist_index, chunksize};
	}

	bool is_section_empty(std::uint32_t frame_index) const noexcept{
		return cache_submit_info_[frame_index].groupCountX == 0;
	}

	void cmd_draw(VkCommandBuffer cmd, std::uint32_t dispatch_group_index) const{
		const auto& submit_info = cache_submit_info_[dispatch_group_index];
		vk::cmd::drawMeshTasks(cmd, submit_info.groupCountX, submit_info.groupCountY, submit_info.groupCountZ);
	}

};
}
