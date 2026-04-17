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
import mo_yanxi.type_register;
import std;

namespace mo_yanxi::graphic::draw::instruction{

export struct gpu_stride_config {
    std::size_t vertex_stride;
    std::size_t primitive_stride;
};

struct alignas(16) vertex_resolve_info{
	std::uint32_t packed_type_timeline; // [15:0]: instr_type, [31:16]: absolute_timeline_index
	std::uint32_t payload_offset;       // 绝对 Payload 偏移
	std::uint32_t packed_skips;         // [15:0]: vtx_skip, [31:16]: prm_skip (0xFFFF 为不生成图元)
	std::uint32_t packed_dispatch_size; // [修改] [15:0]: dispatch_group_index, [31:16]: payload_size
};

struct alignas(16) mesh_dispatch_info_v3{
	std::uint32_t global_vertex_offset;
	std::uint32_t primitives;
	std::uint32_t base_timeline_index;
	std::uint32_t global_primitive_offset; // [新增] 供 CS 写入 IBO/SSBO 及构建 Indirect Command
};

/**
 * @brief info per contiguous mesh dispatch
 */
struct alignas(16) dispatch_config{
	std::uint32_t group_offset;
	std::uint32_t global_primitive_offset; // [新增] 供 FS 配合 gl_PrimitiveID 恢复 per-primitive 信息
	std::uint32_t _cap[2];                 // [修改] 维持 16 字节对齐
};

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
	std::uint32_t align = 64U;

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
	return 7 + host_ctx.get_data_group_vertex_info().size() + (1 + host_ctx.get_data_group_non_vertex_info().size());
}

struct instruction_resolve_info{
    std::vector<std::uint32_t> timelines;
    std::vector<vertex_resolve_info> thread_resolve_info;
    std::vector<mesh_dispatch_info_v3> group_dispatch_info;

    std::uint32_t total_vertices{};
    std::uint32_t total_primitives{};

    void update(const draw_list_context& host_ctx){
       const auto submit_group_subrange = host_ctx.get_valid_submit_groups();
       const std::uint32_t num_timeline_slots = static_cast<std::uint32_t>(host_ctx.get_data_group_vertex_info().size());

       // 第一阶段：进行一次轻量级的预扫描，精确计算各 vector 所需的最终容量
       std::uint32_t pre_total_vertices = 0;
       std::uint32_t pre_committed_blocks = (num_timeline_slots > 0) ? 1 : 0;
       bool pre_timeline_dirty = false;

       for(std::size_t i = 0; i < submit_group_subrange.size(); ++i){
          const auto heads = submit_group_subrange[i].get_instruction_heads();
          for(const auto& head : heads){
             if(head.type == instr_type::uniform_update){
                if(num_timeline_slots > 0) {
                   pre_timeline_dirty = true;
                }
             } else {
                if(pre_timeline_dirty){
                   pre_committed_blocks++;
                   pre_timeline_dirty = false;
                }
                pre_total_vertices += head.payload.draw.vertex_count;
             }
          }
       }

       // 第二阶段：一次性分配所需的所有内存，彻底消除后续的动态扩容
       if (num_timeline_slots > 0) {
           // pre_committed_blocks 是最终有效块数，加 1 是为了给最后一次尚未提交的脏写入提供空间
           timelines.assign((pre_committed_blocks + 1) * num_timeline_slots, 0);
       } else {
           timelines.clear();
       }

       thread_resolve_info.assign(pre_total_vertices, {});
       group_dispatch_info.resize(submit_group_subrange.size());

       std::uint32_t current_global_thread = 0;
       std::uint32_t current_global_primitive = 0;
       std::uint32_t current_global_payload = 0;
       std::uint32_t thread_idx = 0; // 用于替换 push_back 的直接索引

       bool timeline_dirty = false;
       std::uint32_t committed_blocks = num_timeline_slots > 0 ? 1 : 0;

       auto mark_timeline_dirty = [&](std::uint32_t slot_idx){
          timelines[committed_blocks * num_timeline_slots + slot_idx]++;
          timeline_dirty = true;
       };

       for(std::size_t i = 0; i < submit_group_subrange.size(); ++i){
          const auto& group = submit_group_subrange[i];
          const auto heads = group.get_instruction_heads();

          mesh_dispatch_info_v3 v3_info{};
          v3_info.global_vertex_offset = current_global_thread;
          v3_info.base_timeline_index = num_timeline_slots > 0 ? (committed_blocks - 1) : 0;
          v3_info.global_primitive_offset = current_global_primitive;

          std::uint32_t group_primitives = 0;
          std::uint32_t group_vertices = 0;
          std::uint32_t ptr_to_payload = current_global_payload;
          std::uint32_t relative_timeline = 0;

          for(const auto& head : heads){
             if(head.type == instr_type::uniform_update){
                if(num_timeline_slots > 0){
                   mark_timeline_dirty(head.payload.marching_data.index);
                }
                continue;
             }

             if(timeline_dirty){
                // 内存已在 Pass 1 预分配完毕，将当前的 timeline 拷贝到紧接着的下一个预分配块中
                std::memcpy(
                   timelines.data() + (committed_blocks + 1) * num_timeline_slots,
                   timelines.data() + committed_blocks * num_timeline_slots,
                   num_timeline_slots * sizeof(std::uint32_t));
                committed_blocks++;
                relative_timeline++;
                timeline_dirty = false;
             }

             const std::uint32_t head_vtx_count = head.payload.draw.vertex_count;
             const std::uint32_t head_prm_count = head.payload.draw.primitive_count;

             // 展开单条指令产生的所有顶点，进行 1:1 的线程映射
             for(std::uint32_t local_vtx = 0; local_vtx < head_vtx_count; ++local_vtx){
                vertex_resolve_info resolve_res{};
                resolve_res.packed_type_timeline = static_cast<std::uint32_t>(head.type) | (relative_timeline << 16);
                resolve_res.payload_offset = ptr_to_payload;

                std::uint32_t prm_skip = 0xFFFF;
                if(local_vtx >= 2 && (local_vtx - 2) < head_prm_count){
                   prm_skip = group_primitives + local_vtx - 2;
                }

                resolve_res.packed_skips = (local_vtx & 0xFFFF) | (prm_skip << 16);
                resolve_res.packed_dispatch_size = static_cast<std::uint32_t>(i) | (head.payload_size << 16);
                // 使用数组索引赋值完全替代 push_back
                thread_resolve_info[thread_idx++] = resolve_res;
             }

             group_primitives += head_prm_count;
             group_vertices += head_vtx_count;
             ptr_to_payload += head.payload_size;
          }

          v3_info.primitives = group_primitives;
          // group 的数量恰好对应 submit_group_subrange 的大小，直接利用外层循环的索引 i
          group_dispatch_info[i] = v3_info;

          current_global_thread += group_vertices;
          current_global_primitive += group_primitives;
          current_global_payload = ptr_to_payload;
       }

       total_vertices = current_global_thread;
       total_primitives = current_global_primitive;

       // 截断到实际提交的块大小，丢弃最后为了安全写入而多分配但未生效的缓冲区
       if(num_timeline_slots > 0){
          timelines.resize(committed_blocks * num_timeline_slots);
       }
    }
};

struct frame_resource{
	// 纹理绑定索引延后，为 Compute Shader 新增的 Storage Buffer 让位
	static constexpr unsigned image_index = 2;

	// CPU 到 GPU 的输入数据缓冲
	vk::buffer_cpu_to_gpu buffer_dispatch_info{};
	vk::buffer_cpu_to_gpu buffer_resolve_infos{};
	vk::buffer_cpu_to_gpu buffer_timelines{};
	vk::buffer_cpu_to_gpu buffer_instruction{};
	vk::buffer_cpu_to_gpu buffer_per_timeline_data{};
	vk::buffer_cpu_to_gpu buffer_per_draw_call_data{};

    // [新增] 由 Compute Shader 生成，供 Graphics Pipeline 消费的纯 GPU 缓冲
    vk::buffer buffer_vbo{};
    vk::buffer buffer_ibo{};
    vk::buffer buffer_primitive_data{};
    // vk::buffer buffer_indirect_cmds{};

	std::vector<vk::binding_spec> gfx_bindings{};

	vk::descriptor_buffer cs_descriptor_buffer{}; // 静态：CS阶段专用

	//TODO merge them requires dynamic_descriptor_buffer chunkable
	vk::dynamic_descriptor_buffer gfx_descriptor_buffer{}; // 动态：VS/PS阶段专用，包含可变 Image 数组
	vk::descriptor_buffer gfx_descriptor_buffer_per_draw_call{};

	std::vector<std::uint32_t> dispatch_timeline_stamps_{};

	frame_resource(const vk::allocator_usage& allocator,
				   const vk::descriptor_layout& cs_layout,
				   const vk::descriptor_layout& gfx_layout,
				   const vk::descriptor_layout& volatile_layout,
				   const draw_list_context& batch_frontend)
		:
		gfx_bindings{
			[&]{
				std::vector<vk::binding_spec> bindings{
							{0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}, // Primitive Data Out (供 gl_PrimitiveID 读取)
							{1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}, // Timelines (供 VS 读取)
							{image_index, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER} // Textures
				};
				for(unsigned i = 0; i < batch_frontend.get_data_group_vertex_info().size(); ++i){
					// Per-timeline user data
					bindings.push_back({image_index + 1 + i, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
				}
				return bindings;
			}()
		}
	// CS binding 是纯静态的，固定 8 个 Storage Buffer
	, cs_descriptor_buffer(allocator, cs_layout, cs_layout.binding_count())
	, gfx_descriptor_buffer(allocator, gfx_layout, gfx_layout.binding_count(), gfx_bindings)
	, gfx_descriptor_buffer_per_draw_call(allocator, volatile_layout, volatile_layout.binding_count()){
	}

    // [修改] 接收由外部传入的 strides 结构体
    void allocate_pure_gpu_buffers(const vk::allocator_usage& allocator, const instruction_resolve_info& resolve_info, const gpu_stride_config& strides) {
        const VkDeviceSize required_vbo_size = std::max<VkDeviceSize>(16, resolve_info.total_vertices * strides.vertex_stride);
        const VkDeviceSize required_ibo_size = std::max<VkDeviceSize>(16, resolve_info.total_primitives * 3 * sizeof(std::uint32_t));
        const VkDeviceSize required_prm_size = std::max<VkDeviceSize>(16, resolve_info.total_primitives * strides.primitive_stride);
        const VkDeviceSize required_ind_size = std::max<VkDeviceSize>(16, resolve_info.group_dispatch_info.size() * sizeof(VkDrawIndexedIndirectCommand));

        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        if (buffer_vbo.get_size() < required_vbo_size) {
            VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            info.size = required_vbo_size;
            info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            buffer_vbo = vk::buffer(allocator, info, alloc_info);
        }

        if (buffer_ibo.get_size() < required_ibo_size) {
            VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            info.size = required_ibo_size;
            info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            buffer_ibo = vk::buffer(allocator, info, alloc_info);
        }

        if (buffer_primitive_data.get_size() < required_prm_size) {
            VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            info.size = required_prm_size;
            info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            buffer_primitive_data = vk::buffer(allocator, info, alloc_info);
        }
    }

	void upload_v3_data(
		const vk::allocator_usage& allocator,
		const instruction_resolve_info& resolve_info,
		const std::span<const contiguous_draw_list> submit_group_subrange,
		std::uint32_t& payloadSize,
        const gpu_stride_config& strides // [修改] 透传 strides
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

		// 2. Upload thread_resolve_info (1D Flat Array)
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
		const auto payload_size_view = submit_group_subrange |
			std::views::transform([](const contiguous_draw_list& list){ return list.get_pushed_instruction_size(); });
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

        // [修改] 传入 strides
        allocate_pure_gpu_buffers(allocator, resolve_info, strides);
	}

	void upload_user_data(
		const vk::allocator_usage& allocator,
		const draw_list_context& host_ctx,
		const std::span<const mesh_dispatch_info_v3> dispatch_infos,
		data_layout_spec& state_data_layout_cache_,
		std::vector<std::uint32_t>& cached_volatile_timelines){

		load_data_group_to_buffer(host_ctx.get_data_group_vertex_info(), allocator, buffer_per_timeline_data,
		                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

		state_data_layout_cache_.begin_push();
		state_data_layout_cache_.push(sizeof(dispatch_config), dispatch_infos.size());

		for(const auto& entry : host_ctx.get_data_group_non_vertex_info().entries){
			auto total = entry.get_count();
			state_data_layout_cache_.push(entry.unit_size, total);
		}
		state_data_layout_cache_.finalize();

		// [修改] 将 global_primitive_offset 包装进 UBO 结构体中
		state_data_layout_cache_.load(0, dispatch_infos | std::views::transform(
			                              [cur = 0u](const mesh_dispatch_info_v3& info) mutable{
				                              const dispatch_config rst{cur, info.global_primitive_offset};
				                              cur += 1; // 仅表示逻辑段偏移
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

		if(gfx_descriptor_buffer_per_draw_call.get_chunk_count() < dispatch_infos.size() + 1){
			gfx_descriptor_buffer_per_draw_call.set_chunk_count(dispatch_infos.size() + 1);
		}

		auto breakpoints = host_ctx.get_state_transitions();
		cached_volatile_timelines.resize(vtx_info.size(), 0);
		auto dispatch_timeline_chunk_size = (cached_volatile_timelines.size() + 1);

		dispatch_timeline_stamps_.resize(dispatch_timeline_chunk_size * (breakpoints.size() + 1));

		vk::descriptor_mapper mapper{gfx_descriptor_buffer_per_draw_call};

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
		const vk::descriptor_layout& gfx_descriptor_layout,
		VkSampler sampler,
		const instruction_resolve_info& resolve_info,
		std::uint32_t payloadSize
	){
		bool requires_command_record = false;

		// 1. 更新 Compute Shader 静态描述符
		{
			vk::descriptor_mapper cs_mapper{cs_descriptor_buffer};
			const auto dispatch_bytes = resolve_info.group_dispatch_info.size() * sizeof(mesh_dispatch_info_v3);
			const auto resolve_bytes = resolve_info.thread_resolve_info.size() * sizeof(vertex_resolve_info);
			const auto timeline_bytes = resolve_info.timelines.size() * sizeof(std::uint32_t);

			cs_mapper.set_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 0, buffer_dispatch_info.get_address(), dispatch_bytes);
			cs_mapper.set_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, buffer_resolve_infos.get_address(), resolve_bytes);
			cs_mapper.set_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2, buffer_instruction.get_address(), payloadSize);
			cs_mapper.set_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3, buffer_vbo.get_address(), buffer_vbo.get_size());
			cs_mapper.set_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4, buffer_ibo.get_address(), buffer_ibo.get_size());
			cs_mapper.set_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5, buffer_primitive_data.get_address(), buffer_primitive_data.get_size());
		}

		// 2. 更新 Graphics (VS-PS) 动态描述符
		{
			if(const auto cur_size = host_ctx.get_used_images<void*>().size(); gfx_bindings[image_index].count != cur_size){
				gfx_bindings[image_index].count = static_cast<std::uint32_t>(cur_size);
				gfx_descriptor_buffer.reconfigure(gfx_descriptor_layout, gfx_descriptor_layout.binding_count(), gfx_bindings);
				requires_command_record = true;
			}

			vk::dynamic_descriptor_mapper gfx_mapper{gfx_descriptor_buffer};

			// VS/PS 真正需要的 Buffer
			gfx_mapper.set_element_at(0, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, buffer_primitive_data.get_address(), buffer_primitive_data.get_size());
			gfx_mapper.set_element_at(1, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, buffer_timelines.get_address(), resolve_info.timelines.size() * sizeof(std::uint32_t));
			gfx_mapper.set_images_at(image_index, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, sampler,
			                         host_ctx.get_used_images<VkImageView>());

			VkDeviceSize cur_offset{};
			for(const auto& [i, entry] : host_ctx.get_data_group_vertex_info().entries | std::views::enumerate){
				gfx_mapper.set_element_at(image_index + 1 + i, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				                          buffer_per_timeline_data.get_address() + cur_offset, entry.get_required_byte_size());
				cur_offset += entry.get_required_byte_size();
			}
		}

		return requires_command_record;
	}
};

export class batch_vulkan_executor{
private:
	vk::allocator_usage allocator_{};

    gpu_stride_config stride_cfg_{}; // [新增] 保存 stride 配置

	std::vector<frame_resource> frames_{};
	data_layout_spec volatile_data_layout_{};
	std::vector<std::uint32_t> cached_state_timelines_{};
	instruction_resolve_info cached_instruction_resolve_info_{};

	// [修改] 解耦出两套主要 Layout
	vk::descriptor_layout cs_descriptor_layout_{};
	vk::descriptor_layout gfx_descriptor_layout_{};
	vk::descriptor_layout per_draw_call_descriptor_layout_{};

public:
	[[nodiscard]] batch_vulkan_executor() = default;

    // [修改] 构造函数中增加 strides 参数
	[[nodiscard]] explicit batch_vulkan_executor(
		const vk::allocator_usage& a,
		const draw_list_context& batch_host,
        const gpu_stride_config& strides,
		std::size_t frames_in_flight = 3
	)
		: allocator_{a}
          , stride_cfg_{strides}
		  // CS Layout: 仅用于 Compute Shader
		  , cs_descriptor_layout_(allocator_.get_device(), VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
		                       [&](vk::descriptor_layout_builder& builder){
			                       for(int i = 0; i < 7; ++i){
				                       builder.push_seq(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
			                       }
		                       })
		  // Gfx Layout: 用于 Vertex 和 Fragment Shader
		  , gfx_descriptor_layout_(allocator_.get_device(), VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
		                       [&](vk::descriptor_layout_builder& builder){
			                       builder.push_seq(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT); // 0: Primitive Data Out
			                       builder.push_seq(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT); // 1: Timelines
			                       builder.push_seq(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT); // 2: Textures

			                       for(unsigned i = 0; i < batch_host.get_data_group_vertex_info().size(); ++i){
				                       builder.push_seq(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
			                       }
		                       })
		  , per_draw_call_descriptor_layout_{
			  a.get_device(),
			  VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
			  [&](vk::descriptor_layout_builder& builder){
				  builder.push_seq(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT);
				  for(std::size_t i = 0; i < batch_host.get_data_group_non_vertex_info().size(); ++i){
					  builder.push_seq(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT);
				  }
			  }
		  }{

		{
			VkPhysicalDeviceProperties device_props;
			vkGetPhysicalDeviceProperties(allocator_.get_physical_device(), &device_props);
			volatile_data_layout_.align = static_cast<std::uint32_t>(
				std::max(device_props.limits.minUniformBufferOffsetAlignment,
						 device_props.limits.minStorageBufferOffsetAlignment)
			);
		}

		frames_.reserve(frames_in_flight);
		for(std::size_t i = 0; i < frames_in_flight; ++i){
			frames_.emplace_back(allocator_, cs_descriptor_layout_, gfx_descriptor_layout_, per_draw_call_descriptor_layout_, batch_host);
		}
	}

	void upload(
		// std::uint32_t target_section,
		// vk::resource_descriptor_heap& resource_descriptor_heap,
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

		std::uint32_t payloadSize = 0;
        // [修改] 传入 stride 参数
		frame.upload_v3_data(allocator_, cached_instruction_resolve_info_, submit_group_subrange, payloadSize, stride_cfg_);

		frame.upload_user_data(allocator_, host_ctx, cached_instruction_resolve_info_.group_dispatch_info, volatile_data_layout_,
		                       cached_state_timelines_);

		frame.update_descriptors(host_ctx, gfx_descriptor_layout_, sampler, cached_instruction_resolve_info_, payloadSize);
	}

	// [修改] 解耦获取 Layout 的接口
	VkDescriptorSetLayout get_cs_descriptor_set_layout() const noexcept{
		return cs_descriptor_layout_;
	}

	std::array<VkDescriptorSetLayout, 2> get_gfx_descriptor_set_layout() const noexcept{
		return {gfx_descriptor_layout_, per_draw_call_descriptor_layout_};
	}

	// [新增] 专门给 Compute Pipeline 绑定的资源
	template <typename T = std::allocator<descriptor_buffer_usage>>
	void load_cs_descriptors(record_context<T>& record_context, std::uint32_t frame_index){
		auto& frame = frames_[frame_index];
		record_context.push(0, frame.cs_descriptor_buffer);
	}

	// [修改] 给 Graphic Pipeline 绑定的资源
	template <typename T = std::allocator<descriptor_buffer_usage>>
	void load_gfx_descriptors(record_context<T>& record_context, std::uint32_t frame_index){
		auto& frame = frames_[frame_index];
		record_context.push(0, frame.gfx_descriptor_buffer);
		record_context.push(1, frame.gfx_descriptor_buffer_per_draw_call, frame.gfx_descriptor_buffer_per_draw_call.get_chunk_size());
	}

	std::span<const std::uint32_t> get_state_buffer_indices(const draw_list_context& host_ctx,
	                                                        std::uint32_t frame_index,
	                                                        std::uint32_t drawlist_index) const noexcept{
		auto& frame = frames_[frame_index];
		auto chunksize = (1 + host_ctx.get_data_group_non_vertex_info().size());
		return {frame.dispatch_timeline_stamps_.begin() + chunksize * drawlist_index, chunksize};
	}

	bool is_frame_empty() const noexcept{
		return cached_instruction_resolve_info_.total_primitives == 0;
	}

	bool is_section_empty(std::uint32_t section_index) const noexcept{
		return cached_instruction_resolve_info_.group_dispatch_info[section_index].primitives == 0;
	}

	void cmd_compute_resolve(VkCommandBuffer cmd, std::uint32_t frame_index) const {
		if (cached_instruction_resolve_info_.total_vertices == 0) return;

		constexpr std::uint32_t THREADS_PER_GROUP = 128;
		std::uint32_t group_x = (cached_instruction_resolve_info_.total_vertices + THREADS_PER_GROUP - 1) / THREADS_PER_GROUP;

		vkCmdDispatch(cmd, group_x, 1, 1);
	}

	void cmd_draw_direct(VkCommandBuffer cmd, std::uint32_t frame_index, std::uint32_t dispatch_group_index) const{
		auto& frame = frames_[frame_index];
		// CPU 端读取当前 Draw Call 对应的分发信息
		const auto& group_info = cached_instruction_resolve_info_.group_dispatch_info[dispatch_group_index];

		VkBuffer vertex_buffers[] = {frame.buffer_vbo.get()};
		VkDeviceSize offsets[] = {0};
		vkCmdBindVertexBuffers(cmd, 0, 1, vertex_buffers, offsets);
		vkCmdBindIndexBuffer(cmd, frame.buffer_ibo.get(), 0, VK_INDEX_TYPE_UINT32);

		// 使用 vkCmdDrawIndexed 进行直接绘制
		// indexCount = 图元数 * 3
		// firstIndex = 全局图元偏移 * 3
		vkCmdDrawIndexed(cmd, group_info.primitives * 3, 1, group_info.global_primitive_offset * 3, 0, 0);
	}
};

}