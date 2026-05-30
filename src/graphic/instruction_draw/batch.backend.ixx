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
import mo_yanxi.vk.sync_processor;
import mo_yanxi.vk.util;
import mo_yanxi.type_register;
import std;

namespace mo_yanxi::graphic::draw::instruction{

export struct gpu_stride_config {
    std::size_t vertex_stride;
    std::size_t primitive_stride;
};

struct vertex_resolve_info{
	std::uint32_t packed_type_timeline; // [15:0]: gpu_instr_type, [31:16]: relative_timeline_index
	std::uint32_t payload_offset;       // compact GPU payload 偏移
	std::uint32_t packed_skips;         // [15:0]: vtx_skip, [31:16]: prm_skip (0xFFFF 为不生成图元)
	std::uint32_t packed_dispatch_size; // [15:0]: dispatch_group_index, [31:16]: payload_size
	std::uint32_t global_vertex_index;
};

static_assert(sizeof(vertex_resolve_info) == 20);
static_assert(alignof(vertex_resolve_info) == alignof(std::uint32_t));

struct alignas(16) mesh_dispatch_info_v3{
	std::uint32_t global_vertex_offset;
	std::uint32_t primitives;
	std::uint32_t base_timeline_index;
	std::uint32_t global_primitive_offset;
};

/**
 * @brief info per contiguous mesh dispatch
 */
struct alignas(16) dispatch_config{
	std::uint32_t group_offset;
	std::uint32_t global_primitive_offset;
	std::uint32_t _cap[2];
};

struct resolved_vertex{
	float position[3];
	std::uint32_t timeline_index;
	float color[4];
	float uv[2];
	std::uint32_t _cap[2];
};

struct resolved_primitive{
	std::uint32_t texture_index;
	std::uint32_t draw_mode;
	float sdf_expand;
};

static_assert(sizeof(resolved_vertex) == 48);
static_assert(sizeof(resolved_primitive) == 12);

using resolved_index_triangle = std::array<std::uint32_t, 3>;

constexpr std::uint32_t gpu_resolve_group_size = 64;
constexpr std::uint32_t invalid_primitive_skip = 0xFFFFU;
constexpr std::uint32_t packed_u16_max = 0xFFFFU;

[[nodiscard]] FORCE_INLINE constexpr bool fits_packed_u16(const std::uint32_t value) noexcept{
	return value <= packed_u16_max;
}

[[nodiscard]] FORCE_INLINE std::uint32_t pack_u16_pair(
	const std::uint32_t low,
	const std::uint32_t high) noexcept{
	assert(mo_yanxi::graphic::draw::instruction::fits_packed_u16(low));
	assert(mo_yanxi::graphic::draw::instruction::fits_packed_u16(high));
	return (low & packed_u16_max) | ((high & packed_u16_max) << 16);
}

struct geometry_copy_range{
	std::uint32_t begin;
	std::uint32_t count;
};

FORCE_INLINE void append_geometry_copy_range(
	std::vector<geometry_copy_range>& ranges,
	const std::uint32_t begin,
	const std::uint32_t count) noexcept{
	if(count == 0){
		return;
	}
	if(!ranges.empty()){
		auto& last = ranges.back();
		if(last.begin + last.count == begin){
			last.count += count;
			return;
		}
	}
	ranges.push_back({begin, count});
}

FORCE_INLINE void append_vk_buffer_copy_ranges(
	std::vector<VkBufferCopy>& copies,
	const std::span<const geometry_copy_range> ranges,
	const VkDeviceSize stride) {
	for(const auto& range : ranges){
		copies.push_back({
			.srcOffset = static_cast<VkDeviceSize>(range.begin) * stride,
			.dstOffset = static_cast<VkDeviceSize>(range.begin) * stride,
			.size = static_cast<VkDeviceSize>(range.count) * stride,
		});
	}
}

enum struct gpu_instr_type : std::uint32_t{
	noop,
	poly,
	poly_partial,
	constrained_curve,
	SIZE,
};

static_assert(std::to_underlying(gpu_instr_type::SIZE) < (1U << 16));

[[nodiscard]] FORCE_INLINE constexpr gpu_instr_type to_gpu_instr_type(instr_type type) noexcept{
	switch(type){
	case instr_type::poly : return gpu_instr_type::poly;
	case instr_type::poly_partial : return gpu_instr_type::poly_partial;
	case instr_type::constrained_curve : return gpu_instr_type::constrained_curve;
	default : return gpu_instr_type::noop;
	}
}

[[nodiscard]] FORCE_INLINE constexpr bool is_gpu_resolved_instruction(instr_type type) noexcept{
	return to_gpu_instr_type(type) != gpu_instr_type::noop;
}

template <typename T>
[[nodiscard]] FORCE_INLINE constexpr const T& select_endpoint_by_bit(const math::section<T>& section, const std::uint32_t index) noexcept{
	return (index & 1U) ? section.to : section.from;
}

[[nodiscard]] FORCE_INLINE constexpr const float4& quad_color_at(const quad_vert_color& colors, const std::uint32_t index) noexcept{
	switch(index & 3U){
	case 0 : return colors.v00;
	case 1 : return colors.v10;
	case 2 : return colors.v01;
	default : return colors.v11;
	}
}

[[nodiscard]] FORCE_INLINE constexpr float quad_scalar_at(const quad_group<float>& values, const std::uint32_t index) noexcept{
	switch(index & 3U){
	case 0 : return values.v00;
	case 1 : return values.v10;
	case 2 : return values.v01;
	default : return values.v11;
	}
}

[[nodiscard]] FORCE_INLINE constexpr resolved_primitive make_resolved_primitive(const primitive_generic& generic, float sdf_expand = {}) noexcept{
	return {
		.texture_index = generic.image.index,
		.draw_mode = generic.mode.value,
		.sdf_expand = sdf_expand,
	};
}

[[nodiscard]] FORCE_INLINE constexpr resolved_vertex make_resolved_vertex(
	const math::vec2 position,
	const float depth,
	const float4& color,
	const math::vec2 uv,
	const std::uint32_t timeline_index) noexcept{
	return {
		.position = {position.x, position.y, depth},
		.timeline_index = timeline_index,
		.color = {color.r, color.g, color.b, color.a},
		.uv = {uv.x, uv.y},
	};
}

struct section_upload_command_context{
	vk::cmd::dependency_gen dependency{};
	std::vector<std::uint32_t> timelines{};
	std::vector<VkDeviceSize> buffer_offsets{};

	std::vector<VkBufferCopy> copy_info{};
	std::uint32_t current_section_index{};

	[[nodiscard]] section_upload_command_context() = default;

	[[nodiscard]] explicit section_upload_command_context(std::size_t data_entry_count)
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
	return static_cast<std::uint32_t>(6 + host_ctx.get_data_group_vertex_info().size() + (1 + host_ctx.get_data_group_non_vertex_info().size()));
}

struct instruction_resolve_info{
    std::vector<std::uint32_t> timelines;
    std::vector<vertex_resolve_info> thread_resolve_info;
    std::vector<mesh_dispatch_info_v3> group_dispatch_info;
    std::vector<std::byte> gpu_instruction_payload;
    std::vector<geometry_copy_range> vertex_copy_ranges;
    std::vector<geometry_copy_range> primitive_copy_ranges;

    std::uint32_t total_vertices{};
    std::uint32_t total_primitives{};
    std::uint32_t gpu_generated_vertices{};
    std::uint32_t compute_dispatch_threads{};

    struct geometry_output{
       std::span<resolved_vertex> vertices;
       std::span<resolved_index_triangle> indices;
       std::span<resolved_primitive> primitive_data;
    };

    void prepare_allocation(const draw_list_context& host_ctx){
       const auto submit_group_subrange = host_ctx.get_valid_submit_groups();

       std::uint32_t pre_total_vertices = 0;
       std::uint32_t pre_total_primitives = 0;
       std::uint32_t pre_gpu_vertices = 0;

       for(const auto& submit_group : submit_group_subrange){
          for(const auto& head : submit_group.get_instruction_heads()){
             if(head.type == instr_type::uniform_update){
                continue;
             }
             pre_total_vertices += head.payload.draw.vertex_count;
             pre_total_primitives += head.payload.draw.primitive_count;
             if(mo_yanxi::graphic::draw::instruction::is_gpu_resolved_instruction(head.type)){
                pre_gpu_vertices += head.payload.draw.vertex_count;
             }
          }
       }

       total_vertices = pre_total_vertices;
       total_primitives = pre_total_primitives;
       gpu_generated_vertices = pre_gpu_vertices;
       compute_dispatch_threads = pre_gpu_vertices == 0
          ? 0
          : (pre_gpu_vertices + gpu_resolve_group_size - 1U) / gpu_resolve_group_size * gpu_resolve_group_size;
    }

    void update(const draw_list_context& host_ctx, geometry_output output){
       const auto submit_group_subrange = host_ctx.get_valid_submit_groups();
       const std::uint32_t num_timeline_slots = static_cast<std::uint32_t>(host_ctx.get_data_group_vertex_info().size());

       // 第一阶段：进行一次轻量级的预扫描，精确计算各 vector 所需的最终容量
       std::uint32_t pre_total_vertices = 0;
       std::uint32_t pre_total_primitives = 0;
       std::uint32_t pre_gpu_vertices = 0;
       std::uint32_t pre_gpu_payload_size = 0;
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
                pre_total_primitives += head.payload.draw.primitive_count;
                if(is_gpu_resolved_instruction(head.type)){
                   pre_gpu_vertices += head.payload.draw.vertex_count;
                   pre_gpu_payload_size += head.payload_size;
                }
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

       const std::uint32_t padded_gpu_vertices = pre_gpu_vertices == 0
          ? 0
          : (pre_gpu_vertices + gpu_resolve_group_size - 1U) / gpu_resolve_group_size * gpu_resolve_group_size;

       assert(output.vertices.size() >= pre_total_vertices);
       assert(output.indices.size() >= pre_total_primitives);
       assert(output.primitive_data.size() >= pre_total_primitives);

       thread_resolve_info.assign(padded_gpu_vertices, {});
       group_dispatch_info.resize(submit_group_subrange.size());
       gpu_instruction_payload.resize(pre_gpu_payload_size);
       vertex_copy_ranges.clear();
       primitive_copy_ranges.clear();
       output_ = output;

       std::uint32_t current_global_thread = 0;
       std::uint32_t current_global_primitive = 0;
       std::uint32_t current_gpu_payload = 0;
       gpu_generated_vertices = 0;
       compute_dispatch_threads = 0;

       bool timeline_dirty = false;
       std::uint32_t committed_blocks = num_timeline_slots > 0 ? 1 : 0;

       auto mark_timeline_dirty = [&](std::uint32_t slot_idx){
          timelines[committed_blocks * num_timeline_slots + slot_idx]++;
          timeline_dirty = true;
       };

       for(std::size_t i = 0; i < submit_group_subrange.size(); ++i){
          const auto& group = submit_group_subrange[i];
          const auto heads = group.get_instruction_heads();
          const std::byte* group_payload = group.get_buffer_data();

          mesh_dispatch_info_v3 v3_info{};
          v3_info.global_vertex_offset = current_global_thread;
          v3_info.base_timeline_index = num_timeline_slots > 0 ? (committed_blocks - 1) : 0;
          v3_info.global_primitive_offset = current_global_primitive;

          std::uint32_t group_primitives = 0;
          std::uint32_t group_vertices = 0;
          std::uint32_t group_payload_offset = 0;
          std::uint32_t relative_timeline = 0;

          for(const auto& head : heads){
             if(head.type == instr_type::uniform_update){
                if(num_timeline_slots > 0){
                   mark_timeline_dirty(head.payload.marching_data.index);
                }
                group_payload_offset += head.payload_size;
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
             const std::uint32_t global_vertex_begin = current_global_thread + group_vertices;
             const std::uint32_t global_primitive_begin = current_global_primitive + group_primitives;
             const std::uint32_t absolute_timeline = v3_info.base_timeline_index + relative_timeline;
             const std::byte* payload = group_payload + group_payload_offset;

             if(is_gpu_resolved_instruction(head.type)){
                const std::uint32_t gpu_payload_begin = current_gpu_payload;
                std::memcpy(gpu_instruction_payload.data() + gpu_payload_begin, payload, head.payload_size);
                current_gpu_payload += head.payload_size;

                const gpu_instr_type gpu_type = to_gpu_instr_type(head.type);
                const std::uint32_t gpu_resolve_begin = gpu_generated_vertices;
                for(std::uint32_t local_vtx = 0; local_vtx < head_vtx_count; ++local_vtx){
                   vertex_resolve_info resolve_res{};
                   resolve_res.packed_type_timeline = mo_yanxi::graphic::draw::instruction::pack_u16_pair(
                      std::to_underlying(gpu_type), relative_timeline);
                   resolve_res.payload_offset = gpu_payload_begin;

                   std::uint32_t prm_skip = invalid_primitive_skip;
                   if(local_vtx >= 2 && (local_vtx - 2) < head_prm_count){
                      prm_skip = group_primitives + local_vtx - 2;
                   }

                   resolve_res.packed_skips = mo_yanxi::graphic::draw::instruction::pack_u16_pair(local_vtx, prm_skip);
                   resolve_res.packed_dispatch_size = mo_yanxi::graphic::draw::instruction::pack_u16_pair(
                      static_cast<std::uint32_t>(i), head.payload_size);
                   resolve_res.global_vertex_index = global_vertex_begin + local_vtx;
                   thread_resolve_info[gpu_resolve_begin + local_vtx] = resolve_res;
                }
                gpu_generated_vertices += head_vtx_count;
             } else{
                generate_cpu_geometry(head, payload, global_vertex_begin, global_primitive_begin, absolute_timeline);
                mo_yanxi::graphic::draw::instruction::append_geometry_copy_range(vertex_copy_ranges, global_vertex_begin, head_vtx_count);
                mo_yanxi::graphic::draw::instruction::append_geometry_copy_range(primitive_copy_ranges, global_primitive_begin, head_prm_count);
             }

             group_primitives += head_prm_count;
             group_vertices += head_vtx_count;
             group_payload_offset += head.payload_size;
          }

          v3_info.primitives = group_primitives;
          // group 的数量恰好对应 submit_group_subrange 的大小，直接利用外层循环的索引 i
          group_dispatch_info[i] = v3_info;

          current_global_thread += group_vertices;
          current_global_primitive += group_primitives;
       }

       total_vertices = current_global_thread;
       total_primitives = current_global_primitive;
       compute_dispatch_threads = padded_gpu_vertices;
       assert(current_gpu_payload == gpu_instruction_payload.size());
       assert(gpu_generated_vertices == pre_gpu_vertices);
       output_ = {};

       // 截断到实际提交的块大小，丢弃最后为了安全写入而多分配但未生效的缓冲区
       if(num_timeline_slots > 0){
          timelines.resize(committed_blocks * num_timeline_slots);
       }
    }

private:
    geometry_output output_{};

    void write_trivial_primitives(
       const std::uint32_t global_vertex_begin,
       const std::uint32_t global_primitive_begin,
       const std::uint32_t primitive_count,
       const resolved_primitive& primitive) noexcept{
       for(std::uint32_t primitive_idx = 0; primitive_idx < primitive_count; ++primitive_idx){
          const std::uint32_t local_vtx = primitive_idx + 2;
          output_.indices[global_primitive_begin + primitive_idx] = {
             global_vertex_begin + local_vtx - 2,
             global_vertex_begin + local_vtx - 1,
             global_vertex_begin + local_vtx,
          };
          output_.primitive_data[global_primitive_begin + primitive_idx] = primitive;
       }
    }

    void write_vertex(
       const std::uint32_t global_vertex_index,
       const math::vec2 position,
       const float depth,
       const float4& color,
       const math::vec2 uv,
       const std::uint32_t timeline_index) noexcept{
       output_.vertices[global_vertex_index] = mo_yanxi::graphic::draw::instruction::make_resolved_vertex(position, depth, color, uv, timeline_index);
    }

    void generate_cpu_geometry(
       const instruction_head& head,
       const std::byte* payload,
       const std::uint32_t global_vertex_begin,
       const std::uint32_t global_primitive_begin,
       const std::uint32_t timeline_index){
       switch(head.type){
       case instr_type::triangle :{
          const auto& instr = *std::launder(reinterpret_cast<const triangle*>(payload));
          write_vertex(global_vertex_begin + 0, instr.p0, instr.generic.depth, instr.c0, instr.uv0, timeline_index);
          write_vertex(global_vertex_begin + 1, instr.p1, instr.generic.depth, instr.c1, instr.uv1, timeline_index);
          write_vertex(global_vertex_begin + 2, instr.p2, instr.generic.depth, instr.c2, instr.uv2, timeline_index);
          write_trivial_primitives(global_vertex_begin, global_primitive_begin, head.payload.draw.primitive_count,
                                   make_resolved_primitive(instr.generic));
          break;
       }
       case instr_type::quad :{
          const auto& instr = *std::launder(reinterpret_cast<const quad*>(payload));
          for(std::uint32_t local_vtx = 0; local_vtx < head.payload.draw.vertex_count; ++local_vtx){
             write_vertex(global_vertex_begin + local_vtx, instr.vert[local_vtx], instr.generic.depth,
                          quad_color_at(instr.vert_color, local_vtx), instr.uv[local_vtx], timeline_index);
          }
          write_trivial_primitives(global_vertex_begin, global_primitive_begin, head.payload.draw.primitive_count,
                                   make_resolved_primitive(instr.generic));
          break;
       }
       case instr_type::rectangle :{
          const auto& instr = *std::launder(reinterpret_cast<const rectangle*>(payload));
          const math::vec2 rot_x{std::cos(instr.angle) * instr.scale * .5f, std::sin(instr.angle) * instr.scale * .5f};
          const math::vec2 rot_y = rot_x.copy().rotate_rt_counter_clockwise();
          const std::array positions{
             instr.pos - instr.extent.x * rot_x - instr.extent.y * rot_y,
             instr.pos + instr.extent.x * rot_x - instr.extent.y * rot_y,
             instr.pos - instr.extent.x * rot_x + instr.extent.y * rot_y,
             instr.pos + instr.extent.x * rot_x + instr.extent.y * rot_y,
          };
          const std::array uvs{
             instr.uv00,
             math::vec2{instr.uv11.x, instr.uv00.y},
             math::vec2{instr.uv00.x, instr.uv11.y},
             instr.uv11,
          };
          for(std::uint32_t local_vtx = 0; local_vtx < head.payload.draw.vertex_count; ++local_vtx){
             write_vertex(global_vertex_begin + local_vtx, positions[local_vtx], instr.generic.depth,
                          quad_color_at(instr.vert_color, local_vtx), uvs[local_vtx], timeline_index);
          }
          write_trivial_primitives(global_vertex_begin, global_primitive_begin, head.payload.draw.primitive_count,
                                   make_resolved_primitive(instr.generic));
          break;
       }
       case instr_type::line :{
          const auto& instr = *std::launder(reinterpret_cast<const line*>(payload));
          const float half_stroke = instr.stroke * .5f;
          const math::vec2 dir = line_segments::safe_normalize(instr.dst - instr.src);
          const math::vec2 normal{dir.y, -dir.x};
          for(std::uint32_t local_vtx = 0; local_vtx < head.payload.draw.vertex_count; ++local_vtx){
             const bool is_dst_end = (local_vtx & 2U) != 0;
             const bool is_neg_normal = (local_vtx & 1U) != 0;
             const float sign_dir = is_dst_end ? instr.cap_length : -instr.cap_length;
             const float sign_norm = is_neg_normal ? -half_stroke : half_stroke;
             const math::vec2 base_pos = is_dst_end ? instr.dst : instr.src;
             const float4& color = is_dst_end ? instr.color.to : instr.color.from;
             write_vertex(global_vertex_begin + local_vtx, base_pos + dir * sign_dir + normal * sign_norm,
                          instr.generic.depth, color, {}, timeline_index);
          }
          write_trivial_primitives(global_vertex_begin, global_primitive_begin, head.payload.draw.primitive_count,
                                   make_resolved_primitive(instr.generic));
          break;
       }
       case instr_type::line_segments :{
          const auto& instr = *std::launder(reinterpret_cast<const line_segments*>(payload));
          const std::uint32_t node_count = (head.payload_size - sizeof(line_segments)) / sizeof(line_node);
          const auto* nodes_begin = std::launder(reinterpret_cast<const line_node*>(payload + sizeof(line_segments)));
          const std::span nodes{nodes_begin, node_count};
          for(std::uint32_t node_index = 1U; node_index + 1U < node_count; ++node_index){
             const line_node& node_l = nodes[node_index - 1U];
             const line_node& node_c = nodes[node_index];
             const line_node& node_r = nodes[node_index + 1U];
             const math::vec2 vert_nor = line_segments::calculate_miter_vector(node_l.pos, node_c.pos, node_r.pos);
             const std::uint32_t local_vtx = (node_index - 1U) * 2U;
             const float offset_a = math::fma(-.5f, node_c.stroke, node_c.offset);
             const float offset_b = math::fma(.5f, node_c.stroke, node_c.offset);
             write_vertex(global_vertex_begin + local_vtx, math::fma(vert_nor, offset_a, node_c.pos),
                          instr.generic.depth, node_c.color.from, {}, timeline_index);
             write_vertex(global_vertex_begin + local_vtx + 1U, math::fma(vert_nor, offset_b, node_c.pos),
                          instr.generic.depth, node_c.color.to, {}, timeline_index);
          }
          write_trivial_primitives(global_vertex_begin, global_primitive_begin, head.payload.draw.primitive_count,
                                   make_resolved_primitive(instr.generic));
          break;
       }
       case instr_type::line_segments_closed :{
          const auto& instr = *std::launder(reinterpret_cast<const line_segments_closed*>(payload));
          const std::uint32_t node_count = (head.payload_size - sizeof(line_segments_closed)) / sizeof(line_node);
          const auto* nodes_begin = std::launder(reinterpret_cast<const line_node*>(payload + sizeof(line_segments_closed)));
          const std::span nodes{nodes_begin, node_count};
          const std::uint32_t pair_count = head.payload.draw.vertex_count / 2U;
          for(std::uint32_t pair_index = 0; pair_index < pair_count; ++pair_index){
             const std::uint32_t node_index = pair_index % node_count;
             const line_node& node_l = nodes[(node_index + node_count - 1U) % node_count];
             const line_node& node_c = nodes[node_index];
             const line_node& node_r = nodes[(node_index + 1U) % node_count];
             const math::vec2 vert_nor = line_segments::calculate_miter_vector(node_l.pos, node_c.pos, node_r.pos);
             const std::uint32_t local_vtx = pair_index * 2U;
             const float offset_a = math::fma(-.5f, node_c.stroke, node_c.offset);
             const float offset_b = math::fma(.5f, node_c.stroke, node_c.offset);
             write_vertex(global_vertex_begin + local_vtx, math::fma(vert_nor, offset_a, node_c.pos),
                          instr.generic.depth, node_c.color.from, {}, timeline_index);
             write_vertex(global_vertex_begin + local_vtx + 1U, math::fma(vert_nor, offset_b, node_c.pos),
                          instr.generic.depth, node_c.color.to, {}, timeline_index);
          }
          write_trivial_primitives(global_vertex_begin, global_primitive_begin, head.payload.draw.primitive_count,
                                   make_resolved_primitive(instr.generic));
          break;
       }
       case instr_type::rect_ortho :{
          const auto& instr = *std::launder(reinterpret_cast<const rect_aabb*>(payload));
          for(std::uint32_t local_vtx = 0; local_vtx < head.payload.draw.vertex_count; ++local_vtx){
             const bool bit_x = (local_vtx & 1U) != 0;
             const bool bit_y = (local_vtx & 2U) != 0;
             const math::vec2 uv{
                bit_x ? instr.uv11.x : instr.uv00.x,
                bit_y ? instr.uv11.y : instr.uv00.y,
             };
             const float4& color = quad_color_at(instr.vert_color, (bit_y ? 2U : 0U) | (bit_x ? 1U : 0U));
             const float slant_x = bit_y ? -instr.slant_factor_desc : instr.slant_factor_asc;
             const float slant_y = local_vtx == 3U ? -instr.slant_factor_desc : 0.0f;
             const math::vec2 position{
                (bit_x ? instr.v11.x : instr.v00.x) + slant_x,
                (bit_y ? instr.v11.y : instr.v00.y) + slant_y,
             };
             write_vertex(global_vertex_begin + local_vtx, position, instr.generic.depth, color, uv, timeline_index);
          }
          write_trivial_primitives(global_vertex_begin, global_primitive_begin, head.payload.draw.primitive_count,
                                   make_resolved_primitive(instr.generic, instr.sdf_expand));
          break;
       }
       case instr_type::rect_ortho_outline :{
          const auto& instr = *std::launder(reinterpret_cast<const rect_aabb_outline*>(payload));
          constexpr std::array<std::uint32_t, 5> idx{0, 1, 3, 2, 0};
          for(std::uint32_t local_vtx = 0; local_vtx < head.payload.draw.vertex_count; ++local_vtx){
             const std::uint32_t corner = idx[local_vtx / 2U];
             const math::vec2 position{
                corner & 1U ? instr.v11.x : instr.v00.x,
                corner & 2U ? instr.v11.y : instr.v00.y,
             };
             const math::vec2 sign{
                corner & 1U ? -1.0f : 1.0f,
                corner & 2U ? -1.0f : 1.0f,
             };
             const float offset = ((local_vtx & 1U) ? -.5f : .5f) * quad_scalar_at(instr.stroke, corner);
             write_vertex(global_vertex_begin + local_vtx, position + sign * offset, instr.generic.depth,
                          quad_color_at(instr.vert_color, corner), {}, timeline_index);
          }
          write_trivial_primitives(global_vertex_begin, global_primitive_begin, head.payload.draw.primitive_count,
                                   make_resolved_primitive(instr.generic));
          break;
       }
       case instr_type::row_patch :{
          const auto& instr = *std::launder(reinterpret_cast<const row_patch*>(payload));
          const std::uint32_t pos_mask_x = (instr.flags & row_patch_flags::flip_major_pos) != row_patch_flags{} ? 3U : 0U;
          const std::uint32_t pos_mask_y = (instr.flags & row_patch_flags::flip_minor_pos) != row_patch_flags{} ? 1U : 0U;
          const std::uint32_t uv_mask_x = (instr.flags & row_patch_flags::flip_major_uv) != row_patch_flags{} ? 3U : 0U;
          const std::uint32_t uv_mask_y = (instr.flags & row_patch_flags::flip_minor_uv) != row_patch_flags{} ? 1U : 0U;
          const bool transposed = (instr.flags & row_patch_flags::transposed) != row_patch_flags{};
          const float inv_major_span = 1.0f / (instr.coords[3] - instr.coords[0]);
          for(std::uint32_t local_vtx = 0; local_vtx < head.payload.draw.vertex_count; ++local_vtx){
             const std::uint32_t coord_x = local_vtx / 2U;
             const std::uint32_t coord_y = local_vtx & 1U;
             const std::uint32_t pos_x = coord_x ^ pos_mask_x;
             const std::uint32_t pos_y = coord_y ^ pos_mask_y;
             const std::uint32_t uv_x = coord_x ^ uv_mask_x;
             const std::uint32_t uv_y = coord_y ^ uv_mask_y;

             math::vec2 position{instr.coords[pos_x], instr.coords[4U + pos_y]};
             position.x += position.y * instr.skew_major;
             position.y += position.x * instr.skew_minor;
             if(transposed){
                std::swap(position.x, position.y);
             }

             const math::vec2 uv{instr.uvs[2U + uv_x], instr.uvs[uv_y]};
             const float mix_major = (instr.coords[coord_x] - instr.coords[0]) * inv_major_span;
             const float4& color_a = coord_y ? instr.vert_color.v01 : instr.vert_color.v00;
             const float4& color_b = coord_y ? instr.vert_color.v11 : instr.vert_color.v10;
             const float4 color = math::lerp(color_a, color_b, mix_major);
             write_vertex(global_vertex_begin + local_vtx, position, instr.generic.depth + instr._cap,
                          color, uv, timeline_index);
          }
          write_trivial_primitives(global_vertex_begin, global_primitive_begin, head.payload.draw.primitive_count,
                                   make_resolved_primitive(instr.generic));
          break;
       }
       default :
          throw std::runtime_error{"unsupported CPU resolved draw instruction"};
       }
    }
};

struct frame_resource{
	// Graphics set binding 0/1 are buffers; sampled images start at binding 2.
	static constexpr unsigned image_index = 2;

	// CPU 到 GPU 的输入数据缓冲
	vk::buffer_cpu_to_gpu buffer_dispatch_info{};
	vk::buffer_cpu_to_gpu buffer_resolve_infos{};
	vk::buffer_cpu_to_gpu buffer_timelines{};
	vk::buffer_cpu_to_gpu buffer_instruction{};
	vk::buffer_cpu_to_gpu buffer_per_timeline_data{};
	vk::buffer_cpu_to_gpu buffer_per_draw_call_data{};

    // Device-local outputs consumed by compute and graphics.
    vk::buffer buffer_vbo{};
    vk::buffer buffer_ibo{};
    vk::buffer buffer_primitive_data{};

    // Host-visible staging for CPU-resolved geometry ranges.
    vk::buffer_cpu_to_gpu staging_vbo{};
    vk::buffer_cpu_to_gpu staging_ibo{};
    vk::buffer_cpu_to_gpu staging_primitive_data{};
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
	// CS binding 是纯静态的，固定 6 个 Storage Buffer
	, cs_descriptor_buffer(allocator, cs_layout, cs_layout.binding_count())
	, gfx_descriptor_buffer(allocator, gfx_layout, gfx_layout.binding_count(), gfx_bindings)
	, gfx_descriptor_buffer_per_draw_call(allocator, volatile_layout, volatile_layout.binding_count()){
	}

    static vk::buffer make_device_local_buffer(
        const vk::allocator_usage& allocator,
        const VkDeviceSize size,
        const VkBufferUsageFlags usage){
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage = usage;

        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        return vk::buffer{allocator, info, alloc_info};
    }

    void ensure_resolved_geometry_buffers(const vk::allocator_usage& allocator, const instruction_resolve_info& resolve_info, const gpu_stride_config& strides) {
        assert(strides.vertex_stride == sizeof(resolved_vertex));
        assert(strides.primitive_stride == sizeof(resolved_primitive));

        const VkDeviceSize required_vbo_size = std::max<VkDeviceSize>(16, resolve_info.total_vertices * strides.vertex_stride);
        const VkDeviceSize required_ibo_size = std::max<VkDeviceSize>(16, resolve_info.total_primitives * 3 * sizeof(std::uint32_t));
        const VkDeviceSize required_prm_size = std::max<VkDeviceSize>(16, resolve_info.total_primitives * strides.primitive_stride);

        if (buffer_vbo.get_size() < required_vbo_size) {
            buffer_vbo = frame_resource::make_device_local_buffer(
                allocator,
                required_vbo_size,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
        }

        if (buffer_ibo.get_size() < required_ibo_size) {
            buffer_ibo = frame_resource::make_device_local_buffer(
                allocator,
                required_ibo_size,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
        }

        if (buffer_primitive_data.get_size() < required_prm_size) {
            buffer_primitive_data = frame_resource::make_device_local_buffer(
                allocator,
                required_prm_size,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
        }

        if(resolve_info.total_vertices > 0 && staging_vbo.get_size() < required_vbo_size){
            staging_vbo = vk::buffer_cpu_to_gpu{
                allocator,
                required_vbo_size,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT};
        }
        if(resolve_info.total_primitives > 0 && staging_ibo.get_size() < required_ibo_size){
            staging_ibo = vk::buffer_cpu_to_gpu{
                allocator,
                required_ibo_size,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT};
        }
        if(resolve_info.total_primitives > 0 && staging_primitive_data.get_size() < required_prm_size){
            staging_primitive_data = vk::buffer_cpu_to_gpu{
                allocator,
                required_prm_size,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT};
        }
    }

    instruction_resolve_info::geometry_output get_geometry_output(const instruction_resolve_info& resolve_info) noexcept{
        auto* vertices = reinterpret_cast<resolved_vertex*>(staging_vbo.get_mapped_ptr());
        auto* indices = reinterpret_cast<resolved_index_triangle*>(staging_ibo.get_mapped_ptr());
        auto* primitive_data = reinterpret_cast<resolved_primitive*>(staging_primitive_data.get_mapped_ptr());
        return {
            .vertices = std::span{vertices, static_cast<std::size_t>(resolve_info.total_vertices)},
            .indices = std::span{indices, static_cast<std::size_t>(resolve_info.total_primitives)},
            .primitive_data = std::span{primitive_data, static_cast<std::size_t>(resolve_info.total_primitives)},
        };
    }

    void flush_geometry_staging(const instruction_resolve_info& resolve_info) const{
        if(!resolve_info.vertex_copy_ranges.empty()){
            staging_vbo.flush();
        }
        if(!resolve_info.primitive_copy_ranges.empty()){
            staging_ibo.flush();
            staging_primitive_data.flush();
        }
    }

	void upload_v3_data(
		const vk::allocator_usage& allocator,
		const instruction_resolve_info& resolve_info,
		std::uint32_t& payloadSize,
        const gpu_stride_config& strides
	){
		const VkBufferUsageFlags storage_flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

		if(resolve_info.gpu_generated_vertices > 0){
			const auto dispatch_bytes = resolve_info.group_dispatch_info.size() * sizeof(mesh_dispatch_info_v3);
			if(dispatch_bytes > 0){
				if(buffer_dispatch_info.get_size() < dispatch_bytes){
					buffer_dispatch_info = vk::buffer_cpu_to_gpu{allocator, dispatch_bytes, storage_flags};
				}
				vk::buffer_mapper{buffer_dispatch_info}.load_range(resolve_info.group_dispatch_info);
			}

			const auto resolve_bytes = resolve_info.thread_resolve_info.size() * sizeof(vertex_resolve_info);
			if(resolve_bytes > 0){
				if(buffer_resolve_infos.get_size() < resolve_bytes){
					buffer_resolve_infos = vk::buffer_cpu_to_gpu{allocator, resolve_bytes, storage_flags};
				}
				vk::buffer_mapper{buffer_resolve_infos}.load_range(resolve_info.thread_resolve_info);
			}
		}

		// 3. Upload timelines
		const auto timeline_bytes = resolve_info.timelines.size() * sizeof(std::uint32_t);
		if(timeline_bytes > 0){
			if(buffer_timelines.get_size() < timeline_bytes){
				buffer_timelines = vk::buffer_cpu_to_gpu{allocator, timeline_bytes, storage_flags};
			}
			vk::buffer_mapper{buffer_timelines}.load_range(resolve_info.timelines);
		}

		payloadSize = static_cast<std::uint32_t>(resolve_info.gpu_instruction_payload.size());

		if(payloadSize > 0){
			if(buffer_instruction.get_size() < payloadSize){
				buffer_instruction = vk::buffer_cpu_to_gpu{allocator, payloadSize, storage_flags};
			}
			vk::buffer_mapper{buffer_instruction}.load_range(resolve_info.gpu_instruction_payload);
		}

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

		auto section_events = host_ctx.get_section_events();
		cached_volatile_timelines.resize(vtx_info.size(), 0);
		auto dispatch_timeline_chunk_size = (cached_volatile_timelines.size() + 1);

		dispatch_timeline_stamps_.resize(dispatch_timeline_chunk_size * (section_events.size() + 1));

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

		for(const auto& [chunk_idx, event] : section_events | std::views::enumerate){
			for(const auto i : event.per_draw_uniform_bumps){
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
		if(resolve_info.gpu_generated_vertices > 0){
			vk::descriptor_mapper cs_mapper{cs_descriptor_buffer};
			const auto dispatch_bytes = resolve_info.group_dispatch_info.size() * sizeof(mesh_dispatch_info_v3);
			const auto resolve_bytes = resolve_info.thread_resolve_info.size() * sizeof(vertex_resolve_info);

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

    gpu_stride_config stride_cfg_{};

	std::vector<frame_resource> frames_{};
	data_layout_spec volatile_data_layout_{};
	std::vector<std::uint32_t> cached_state_timelines_{};
	instruction_resolve_info cached_instruction_resolve_info_{};
	std::vector<VkBufferCopy> copy_ranges_cache_{};

	vk::descriptor_layout cs_descriptor_layout_{};
	vk::descriptor_layout gfx_descriptor_layout_{};
	vk::descriptor_layout per_draw_call_descriptor_layout_{};

public:
	[[nodiscard]] batch_vulkan_executor() = default;

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
			                       for(int i = 0; i < 6; ++i){
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

		auto& frame = frames_[frame_index];
		const auto submit_group_subrange = host_ctx.get_valid_submit_groups();
		if(submit_group_subrange.empty()){
			cached_instruction_resolve_info_.prepare_allocation(host_ctx);
			return;
		}

		cached_instruction_resolve_info_.prepare_allocation(host_ctx);
		frame.ensure_resolved_geometry_buffers(allocator_, cached_instruction_resolve_info_, stride_cfg_);
		cached_instruction_resolve_info_.update(host_ctx, frame.get_geometry_output(cached_instruction_resolve_info_));
		frame.flush_geometry_staging(cached_instruction_resolve_info_);

		std::uint32_t payloadSize = 0;
		frame.upload_v3_data(allocator_, cached_instruction_resolve_info_, payloadSize, stride_cfg_);

		frame.upload_user_data(allocator_, host_ctx, cached_instruction_resolve_info_.group_dispatch_info, volatile_data_layout_,
		                       cached_state_timelines_);

		frame.update_descriptors(host_ctx, gfx_descriptor_layout_, sampler, cached_instruction_resolve_info_, payloadSize);
	}

	VkDescriptorSetLayout get_cs_descriptor_set_layout() const noexcept{
		return cs_descriptor_layout_;
	}

	std::array<VkDescriptorSetLayout, 2> get_gfx_descriptor_set_layout() const noexcept{
		return {gfx_descriptor_layout_, per_draw_call_descriptor_layout_};
	}

	template <typename T = std::allocator<descriptor_buffer_usage>>
	void load_cs_descriptors(record_context<T>& record_context, std::uint32_t frame_index){
		auto& frame = frames_[frame_index];
		record_context.push(0, frame.cs_descriptor_buffer);
	}

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

	bool has_gpu_resolve_work() const noexcept{
		return cached_instruction_resolve_info_.compute_dispatch_threads != 0;
	}

	bool has_cpu_geometry_copy_work() const noexcept{
		return !cached_instruction_resolve_info_.vertex_copy_ranges.empty()
			|| !cached_instruction_resolve_info_.primitive_copy_ranges.empty();
	}

	void cmd_copy_cpu_resolved_geometry(VkCommandBuffer cmd, std::uint32_t frame_index) {
		if(!has_cpu_geometry_copy_work()){
			return;
		}

		auto& frame = frames_[frame_index];
		const auto& resolve_info = cached_instruction_resolve_info_;

		vk::sync::sync_barrier_batch barrier{};
		if(!resolve_info.vertex_copy_ranges.empty()){
			barrier.push_buffer(
				frame.staging_vbo.get(),
				VK_PIPELINE_STAGE_2_HOST_BIT,
				VK_ACCESS_2_HOST_WRITE_BIT,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				VK_ACCESS_2_TRANSFER_READ_BIT);
		}
		if(!resolve_info.primitive_copy_ranges.empty()){
			barrier.push_buffer(
				frame.staging_ibo.get(),
				VK_PIPELINE_STAGE_2_HOST_BIT,
				VK_ACCESS_2_HOST_WRITE_BIT,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				VK_ACCESS_2_TRANSFER_READ_BIT);
			barrier.push_buffer(
				frame.staging_primitive_data.get(),
				VK_PIPELINE_STAGE_2_HOST_BIT,
				VK_ACCESS_2_HOST_WRITE_BIT,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				VK_ACCESS_2_TRANSFER_READ_BIT);
		}
		barrier.apply(cmd);

		copy_ranges_cache_.clear();
		mo_yanxi::graphic::draw::instruction::append_vk_buffer_copy_ranges(
			copy_ranges_cache_, resolve_info.vertex_copy_ranges, stride_cfg_.vertex_stride);
		if(!copy_ranges_cache_.empty()){
			vkCmdCopyBuffer(
				cmd,
				frame.staging_vbo.get(),
				frame.buffer_vbo.get(),
				static_cast<std::uint32_t>(copy_ranges_cache_.size()),
				copy_ranges_cache_.data());
		}

		copy_ranges_cache_.clear();
		mo_yanxi::graphic::draw::instruction::append_vk_buffer_copy_ranges(
			copy_ranges_cache_, resolve_info.primitive_copy_ranges, sizeof(resolved_index_triangle));
		if(!copy_ranges_cache_.empty()){
			vkCmdCopyBuffer(
				cmd,
				frame.staging_ibo.get(),
				frame.buffer_ibo.get(),
				static_cast<std::uint32_t>(copy_ranges_cache_.size()),
				copy_ranges_cache_.data());
		}

		copy_ranges_cache_.clear();
		mo_yanxi::graphic::draw::instruction::append_vk_buffer_copy_ranges(
			copy_ranges_cache_, resolve_info.primitive_copy_ranges, stride_cfg_.primitive_stride);
		if(!copy_ranges_cache_.empty()){
			vkCmdCopyBuffer(
				cmd,
				frame.staging_primitive_data.get(),
				frame.buffer_primitive_data.get(),
				static_cast<std::uint32_t>(copy_ranges_cache_.size()),
				copy_ranges_cache_.data());
		}

		constexpr VkPipelineStageFlags2 geometry_read_stages =
			VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
		constexpr VkAccessFlags2 geometry_read_access =
			VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT;

		const VkPipelineStageFlags2 dst_stage = geometry_read_stages
			| (has_gpu_resolve_work() ? VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT : 0);
		const VkAccessFlags2 dst_access = geometry_read_access
			| (has_gpu_resolve_work() ? VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT : 0);

		barrier.clear();
		if(!resolve_info.vertex_copy_ranges.empty()){
			barrier.push_buffer(
				frame.buffer_vbo.get(),
				VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				VK_ACCESS_2_TRANSFER_WRITE_BIT,
				dst_stage,
				dst_access);
		}
		if(!resolve_info.primitive_copy_ranges.empty()){
			barrier.push_buffer(
				frame.buffer_ibo.get(),
				VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				VK_ACCESS_2_TRANSFER_WRITE_BIT,
				dst_stage,
				dst_access);
			barrier.push_buffer(
				frame.buffer_primitive_data.get(),
				VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				VK_ACCESS_2_TRANSFER_WRITE_BIT,
				dst_stage,
				dst_access);
		}
		barrier.apply(cmd);
	}

	void cmd_compute_resolve(VkCommandBuffer cmd, std::uint32_t frame_index) const {
		if (cached_instruction_resolve_info_.compute_dispatch_threads == 0) return;

		const std::uint32_t group_x = cached_instruction_resolve_info_.compute_dispatch_threads / gpu_resolve_group_size;

		vkCmdDispatch(cmd, group_x, 1, 1);
	}

	void cmd_barrier_compute_to_draw(VkCommandBuffer cmd, std::uint32_t frame_index) const {
		if(!has_gpu_resolve_work()){
			return;
		}

		const auto& frame = frames_[frame_index];

		constexpr VkPipelineStageFlags2 geometry_read_stages =
			VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
		constexpr VkAccessFlags2 geometry_read_access =
			VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT;

		vk::sync::sync_barrier_batch barrier{};
		barrier.push_buffer(
			frame.buffer_vbo.get(),
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
			geometry_read_stages,
			geometry_read_access);
		barrier.push_buffer(
			frame.buffer_ibo.get(),
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
			geometry_read_stages,
			geometry_read_access);
		barrier.push_buffer(
			frame.buffer_primitive_data.get(),
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
			geometry_read_stages,
			geometry_read_access);
		barrier.apply(cmd);
	}

	void cmd_draw_direct(VkCommandBuffer cmd, std::uint32_t frame_index, std::uint32_t dispatch_group_index) const{
		auto& frame = frames_[frame_index];
		const auto& group_info = cached_instruction_resolve_info_.group_dispatch_info[dispatch_group_index];

		VkBuffer vertex_buffers[] = {frame.buffer_vbo.get()};
		VkDeviceSize offsets[] = {0};
		vkCmdBindVertexBuffers(cmd, 0, 1, vertex_buffers, offsets);
		vkCmdBindIndexBuffer(cmd, frame.buffer_ibo.get(), 0, VK_INDEX_TYPE_UINT32);

		vkCmdDrawIndexed(cmd, group_info.primitives * 3, 1, group_info.global_primitive_offset * 3, 0, 0);
	}
};

}
