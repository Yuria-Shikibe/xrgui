module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.graphic.draw.instruction.batch.common;

export import mo_yanxi.graphic.draw.instruction.general;
export import binary_trace; // 引入 binary_trace 模块以使用 binary_diff_trace::tag

import std;

namespace mo_yanxi::graphic::draw::instruction{

/**
 * @brief GPU端 Storage Buffer 中每个 Dispatch Group 的元数据布局
 */
export
struct dispatch_group_info{
	std::uint32_t instruction_head_index;
	std::uint32_t instruction_offset;
	std::uint32_t vertex_offset;
	std::uint32_t primitive_offset;
	std::uint32_t primitive_count;
};

/**
 * @brief 状态切换配置，用于在 Draw Call 之间注入状态变更
 * 现已简化为 Tag + Payload 的扁平结构
 */
export
struct state_transition_config{
	using tag_type = mo_yanxi::binary_diff_trace::tag;

	struct entry{
		tag_type tag;
		std::uint32_t offset;         // 在 payload_storage 中的物理偏移
		std::uint32_t size;           // 数据大小
		std::uint32_t logical_offset; // [新增] 数据在目标资源中的逻辑起始偏移

		constexpr std::span<const std::byte> get_payload(const std::byte* basePtr) const noexcept{
			return {basePtr + offset, size};
		}
	};

	struct exported_entry{
		std::span<const std::byte> payload;
		tag_type tag;
		std::uint32_t logical_offset;

		template <typename T>
		const T& as() const noexcept{
			assert(sizeof(T) <= payload.size());
			return *start_lifetime_as<T>(payload.data());
		}
	};

	/**
	 * @brief determine the LOAD_OP after this call
	 */
	bool clear_draw_after_break{};

private:
	std::vector<entry> entries_{};
	std::vector<std::byte> payload_storage{};

public:

	[[nodiscard]] auto get_entries() const noexcept{
		return entries_ | std::views::transform([base = payload_storage.data()](const entry& e){
			return exported_entry{e.get_payload(base), e.tag, e.logical_offset, };
		});
	}

	// 支持带 offset 的 push
	void push(tag_type tag, std::span<const std::byte> payload, std::uint32_t logical_offset = 0){
		if(payload.empty()) return;

		entry e{tag};
		e.offset = static_cast<std::uint32_t>(payload_storage.size());
		e.size = static_cast<std::uint32_t>(payload.size());
		e.logical_offset = logical_offset; // 记录逻辑偏移

		payload_storage.resize(e.offset + e.size);
		std::memcpy(payload_storage.data() + e.offset, payload.data(), e.size);

		entries_.push_back(e);
	}

	template <typename T>
		requires (std::is_trivially_copyable_v<T>)
	void push(tag_type tag, const T& payload, std::uint32_t logical_offset = 0){
		this->push(tag, std::span{reinterpret_cast<const std::byte*>(std::addressof(payload)), sizeof(T)}, logical_offset);
	}

	void append(const state_transition_config& other){
		clear_draw_after_break = clear_draw_after_break || other.clear_draw_after_break;
		const auto curOff = static_cast<std::uint32_t>(payload_storage.size());
		const auto curEntrySz = entries_.size();

		payload_storage.append_range(other.payload_storage);
		entries_.append_range(other.entries_);

		for(std::size_t i = curEntrySz; i < entries_.size(); ++i){
			entries_[i].offset += curOff;
		}
	}

	void clear() noexcept{
		clear_draw_after_break = false;
		entries_.clear();
		payload_storage.clear();
	}

	bool empty() const noexcept {
		return entries_.empty();
	}
};

export
struct state_transition{
	unsigned break_before_index{};
	std::vector<std::uint32_t> uniform_buffer_marching_indices{};
	state_transition_config config{};

	[[nodiscard]] state_transition() = default;
	[[nodiscard]] explicit state_transition(unsigned break_before_index)
		: break_before_index(break_before_index){
	}
};

export
struct draw_uniform_data_entry{
	std::size_t unit_size{};
	std::vector<std::byte> data_{};
	bool pending{};

	FORCE_INLINE inline std::span<const std::byte> operator[](const std::size_t idx) const noexcept{
		const auto off = idx * unit_size;
		return {data_.begin() + off, data_.begin() + (off + unit_size)};
	}

	FORCE_INLINE inline void reset() noexcept{
		data_.clear();
		pending = false;
	}

	FORCE_INLINE inline std::uint32_t get_current_index() const noexcept{
		if(data_.empty()) return 0;
		return get_count() - pending;
	}

	FORCE_INLINE inline void push_default(const void* data){
		assert(data_.empty());
		data_.resize(unit_size);
		std::memcpy(data_.data(), data, unit_size);
	}

	template <typename T>
		requires (std::is_trivially_copyable_v<T> && !std::is_pointer_v<T> && std::is_class_v<T>)
	FORCE_INLINE void push_default(const T& v){
		assert(unit_size == sizeof(T));
		push_default(static_cast<const void*>(std::addressof(v)));
	}

	[[nodiscard]] FORCE_INLINE inline bool collapse() noexcept{
		if(!pending) return false;
		auto count = get_count();
		auto p = (count - 1) * unit_size + data_.begin();
		assert(count > 0);
		pending = false;
		if(count == 1){
			return true;
		} else{
			auto p_base = (count - 2) * unit_size + data_.data();
			if(std::memcmp(std::to_address(p), p_base, unit_size) == 0){
				data_.erase(p, data_.end());
				return false;
			}
			return true;
		}
	}

	FORCE_INLINE inline void push(const void* data){
		if(pending){
			auto last = get_count();
			assert(last > 0);
			auto p = (last - 1) * unit_size + data_.data();
			std::memcpy(p, data, unit_size);
		} else{
			auto last = get_count();
			data_.resize(data_.size() + unit_size);
			auto p = last * unit_size + data_.data();
			std::memcpy(p, data, unit_size);
			pending = true;
		}
	}

	FORCE_INLINE inline void push(std::span<const std::byte> data){
		assert(unit_size == data.size());
		push(data.data());
	}

	template <typename T>
		requires (std::is_trivially_copyable_v<T> && !std::is_pointer_v<T> && std::is_class_v<T>)
	FORCE_INLINE void push(const T& v){
		assert(unit_size == sizeof(T));
		push(static_cast<const void*>(std::addressof(v)));
	}

	FORCE_INLINE inline bool empty() const noexcept{
		return data_.empty();
	}

	FORCE_INLINE inline std::uint32_t get_count() const noexcept{
		return static_cast<std::uint32_t>(data_.size() / unit_size);
	}

	FORCE_INLINE inline std::size_t get_required_byte_size() const noexcept{
		return data_.size() - pending * unit_size;
	}

	FORCE_INLINE inline std::span<const std::byte> get_data_span() const noexcept{
		return {data_.data(), get_required_byte_size()};
	}

	FORCE_INLINE inline const std::byte* data() const noexcept{
		return data_.data();
	}
};

struct get_primitive_count_trivial_functor{
	CONST_FN FORCE_INLINE std::uint32_t static operator()(instr_type type, const std::byte* ptr_to_payload, std::uint32_t vtx) noexcept{
		return vtx < 3 ? 0 : vtx - 2;
	}
};

struct draw_list_inheritance_param{
	std::uint32_t verticesBreakpoint{};
	std::uint32_t nextPrimitiveOffset{};
};

export
struct contiguous_draw_list{

private:
	instruction_buffer instruction_buffer_{};
	std::vector<instruction_head> instruction_heads_{};

	std::vector<dispatch_group_info> dispatch_config_storage{};
	std::vector<std::uint32_t> group_initial_vertex_data_timestamps_{};

	std::span<draw_uniform_data_entry> vertex_data_entries_{};
	std::byte* ptr_to_head{};
	std::uint32_t index_to_last_chunk_head_{};
	std::uint32_t offset_to_last_chunk_head_{};

	std::uint32_t verticesBreakpoint{};
	std::uint32_t nextPrimitiveOffset{};

	std::uint32_t currentMeshCount{};
	std::uint32_t pushedVertices{};
	std::uint32_t pushedPrimitives{};

	FORCE_INLINE void setup_current_dispatch_group_info(){
		for(const auto& [i, vertex_data_entry] : vertex_data_entries_ | std::views::enumerate){
			const std::uint32_t idx = static_cast<std::uint32_t>(vertex_data_entries_.size() * currentMeshCount + i);
			auto timeline = vertex_data_entry.get_current_index();
			group_initial_vertex_data_timestamps_[idx] = timeline;
		}

		dispatch_config_storage[currentMeshCount].primitive_offset = nextPrimitiveOffset;
		dispatch_config_storage[currentMeshCount].vertex_offset = verticesBreakpoint > 2
			                                                          ? (verticesBreakpoint - 2)
			                                                          : (verticesBreakpoint = 0);
	}

public:
	[[nodiscard]] contiguous_draw_list() = default;

	[[nodiscard]] explicit(false) contiguous_draw_list(
		std::size_t vertex_data_slot_count,
		std::uint32_t dispatch_group_count,
		std::span<draw_uniform_data_entry> entries
	)
		: dispatch_config_storage(dispatch_group_count)
		, group_initial_vertex_data_timestamps_(vertex_data_slot_count * dispatch_group_count),
		vertex_data_entries_(entries){
	}

	FORCE_INLINE std::span<const instruction_head> get_instruction_heads() const noexcept{
		return instruction_heads_;
	}

	FORCE_INLINE std::span<const dispatch_group_info> get_dispatch_infos() const noexcept{
		return std::span{dispatch_config_storage.data(), currentMeshCount};
	}

	FORCE_INLINE std::span<const std::uint32_t> get_timeline_datas() const noexcept{
		return std::span{
				group_initial_vertex_data_timestamps_.begin(),
				group_initial_vertex_data_timestamps_.begin() + currentMeshCount * vertex_data_entries_.size()
			};
	}

	FORCE_INLINE std::size_t get_pushed_instruction_size() const noexcept{
		return ptr_to_head - instruction_buffer_.data();
	}

	FORCE_INLINE const std::byte* get_buffer_data() const noexcept{
		return instruction_buffer_.data();
	}

	FORCE_INLINE draw_list_inheritance_param get_extend_able_params() const noexcept{
		return {verticesBreakpoint, nextPrimitiveOffset};
	}

	FORCE_INLINE void reset(draw_list_inheritance_param param){
		std::memset(dispatch_config_storage.data(), 0, dispatch_config_storage.size() * sizeof(dispatch_group_info));
		ptr_to_head = instruction_buffer_.data();
		offset_to_last_chunk_head_ = 0;
		index_to_last_chunk_head_ = 0;
		verticesBreakpoint = param.verticesBreakpoint;
		nextPrimitiveOffset = param.nextPrimitiveOffset;
		currentMeshCount = 0;
		pushedVertices = 0;
		pushedPrimitives = 0;
		instruction_heads_.clear();

		setup_current_dispatch_group_info();
	}

	FORCE_INLINE void finalize(){
		if(pushedPrimitives){
			save_current_dispatch(static_cast<std::uint32_t>(-1), -1);
		}
	}

	/**
	 * @brief push a draw instruction.
	 * @return false if this submission group is full
	 */
	template <typename PrimitiveRemainFn = get_primitive_count_trivial_functor>
		requires (std::is_invocable_r_v<std::uint32_t, PrimitiveRemainFn, instr_type, const std::byte*, const std::uint32_t>)
	FORCE_INLINE bool push(
		const instruction_head& head, const std::byte* data,
		const std::size_t max_vertices_per_mesh_group,
		PrimitiveRemainFn fn_get_primitive_count = {}
		){
		assert(std::to_underlying(head.type) < std::to_underlying(instruction::instr_type::SIZE));
		auto instruction_offset = ptr_to_head - instruction_buffer_.data();
		if(head.payload_size){
			assert(data != nullptr);
			if(instruction_buffer_.size() < instruction_offset + head.payload_size){
				instruction_buffer_.resize((instruction_buffer_.size() + head.payload_size) * 2);
				ptr_to_head = instruction_buffer_.data() + instruction_offset;
			}

			std::memcpy(ptr_to_head, data, head.payload_size);
			ptr_to_head += head.payload_size;
		}else{
			assert(data == nullptr);
		}

		instruction_heads_.push_back(head);
		switch(head.type){
		case instr_type::noop :
		case instr_type::uniform_update : return true;
		default : break;
		}

		const auto get_remain_vertices = [&] FORCE_INLINE{
			return max_vertices_per_mesh_group - pushedVertices;
		};
		const auto instructionVertices = head.payload.draw.vertex_count;

		while(currentMeshCount != dispatch_config_storage.size()){
			auto nextVertices = instructionVertices;

			if(verticesBreakpoint){
				assert(verticesBreakpoint >= 3);
				assert(verticesBreakpoint < nextVertices);
				nextVertices -= (verticesBreakpoint -= 2); //make sure a complete primitive is draw
			}

			//check if this instruction is fully consumed
			if(pushedVertices + nextVertices <= max_vertices_per_mesh_group){
				pushedVertices += nextVertices;
				pushedPrimitives += fn_get_primitive_count(head.type, data, nextVertices);
				verticesBreakpoint = 0;
				nextPrimitiveOffset = 0;

				if(pushedVertices == max_vertices_per_mesh_group){
					instruction_offset += head.payload_size;
					save_current_dispatch(static_cast<std::uint32_t>(instruction_offset), instruction_heads_.size());
					pushedPrimitives = 0;
					pushedVertices = 0;
				}
				return true;
			}

			//or save remain to
			const auto remains = get_remain_vertices();
			const auto primits = fn_get_primitive_count(head.type, data, remains);
			nextPrimitiveOffset += primits;
			pushedPrimitives += primits;

			verticesBreakpoint += remains;

			save_current_dispatch(static_cast<std::uint32_t>(instruction_offset), instruction_heads_.size() - 1);
			pushedPrimitives = 0;
			pushedVertices = 0;
		}

		return false;
	}

	/**
	 *
	 * @tparam Fn func(head, payload ptr)
	 */
	template <std::invocable<const instruction_head&, std::byte*> Fn>
	FORCE_INLINE void for_each_instruction(Fn fn) const noexcept(std::is_nothrow_invocable_v<Fn, const instruction_head&, std::byte*>){
		auto cur = instruction_buffer_.data();
		for (const auto & generic_instruction_head : instruction_heads_){
			fn(generic_instruction_head, cur);
			cur += generic_instruction_head.payload_size;
		}
	}

	FORCE_INLINE bool empty() const noexcept{
		return ptr_to_head == instruction_buffer_.data();
	}

private:
	FORCE_INLINE void save_current_dispatch(std::uint32_t next_instr_begin, std::uint32_t next_head_begin){
		assert(pushedPrimitives != 0);
		assert(offset_to_last_chunk_head_ % 16 == 0);
		dispatch_config_storage[currentMeshCount].instruction_offset = offset_to_last_chunk_head_;
		dispatch_config_storage[currentMeshCount].instruction_head_index = index_to_last_chunk_head_;
		dispatch_config_storage[currentMeshCount].primitive_count = pushedPrimitives;


		++currentMeshCount;
		offset_to_last_chunk_head_ = next_instr_begin;
		index_to_last_chunk_head_ = next_head_begin;
		if(currentMeshCount != dispatch_config_storage.size()){
			setup_current_dispatch_group_info();
		}
	}

};

}