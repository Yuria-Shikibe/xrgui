module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.graphic.draw.instruction.batch.common;

import mo_yanxi.graphic.draw.instruction.general;
// import mo_yanxi.graphic.draw.instruction;

import std;

namespace mo_yanxi::graphic::draw::instruction{
/**
 * @brief GPU端 Storage Buffer 中每个 Dispatch Group 的元数据布局
 *
 */
export
struct alignas(16) dispatch_group_info{
	std::uint32_t instruction_offset;
	std::uint32_t vertex_offset;
	std::uint32_t primitive_offset;
	std::uint32_t primitive_count;
};

/**
 * @brief GPU端 Uniform Buffer 用于传递当前 Batch 偏移量的布局
 *
 */
export
struct alignas(16) dispatch_config{
	std::uint32_t group_offset;
	std::uint32_t _cap[3]; // Padding to maintain alignment if necessary, or reserved
};

export
struct state_transition_entry{
	std::uint32_t flag;
	std::span<const std::byte> data;

	template <typename T>
		requires (std::is_trivially_copyable_v<T>)
	const T& as() const noexcept{
		assert(sizeof(T) == data.size());
		return *reinterpret_cast<const T*>(data.data());
	}
};

export
struct state_transition_config{
	struct entry{
		std::uint32_t flag;
		std::uint32_t offset;
		std::uint32_t size;

		std::span<const std::byte> get_payload(const std::byte* basePtr) const noexcept{
			return {basePtr + offset, size};
		}
	};

	/**
	 * @brief determine the LOAD_OP after this call
	 *
	 */
	bool clear_draw_after_break{};

	std::vector<entry> user_defined_flags{};
	std::vector<std::byte> payload_storage{};

	//
	[[nodiscard]] auto get_entries() const noexcept{
		return user_defined_flags | std::views::transform([base = payload_storage.data()](const entry& e){
			return state_transition_entry{e.flag, e.get_payload(base)};
		});
	}

	//
	void push(std::uint32_t flag, std::span<const std::byte> payload){
		entry e{flag};
		if(!payload.empty()){
			e.offset = static_cast<std::uint32_t>(payload_storage.size());
			e.size = static_cast<std::uint32_t>(payload.size());
			payload_storage.resize(e.offset + e.size);
			std::memcpy(payload_storage.data() + e.offset, payload.data(), e.size);
		}
		user_defined_flags.push_back(e);
	}

	template <typename T>
		requires (std::is_trivially_copyable_v<T>)
	void push(std::uint32_t flag, const T& payload){
		this->push(flag, std::span{reinterpret_cast<const std::byte*>(std::addressof(payload)), sizeof(T)});
	}

	bool operator==(const state_transition_config&) const noexcept = default;
};

export
struct state_transition{
	/**
	 * @brief before which submit group should this breakpoint occur
	 */
	unsigned break_before_index{};

	//
	std::vector<std::uint32_t> uniform_buffer_marching_indices{};

	state_transition_config config{};

	[[nodiscard]] state_transition() = default;

	//
	[[nodiscard]] explicit state_transition(unsigned break_before_index)
		: break_before_index(break_before_index){
	}

	bool operator==(const state_transition&) const noexcept = default;
};

export
struct draw_uniform_data_entry{
	std::size_t unit_size{};
	std::vector<std::byte> data_{};
	bool pending{};

	std::span<const std::byte> operator[](const std::size_t idx) const noexcept{
		const auto off = idx * unit_size;
		return {data_.begin() + off, data_.begin() + (off + unit_size)};
	}

	void reset() noexcept{
		data_.clear();
		pending = false;
	}

	std::uint32_t get_current_index() const noexcept{
		if(data_.empty()) return 0;
		return get_count() - pending;
	}

	void push_default(const void* data){
		assert(data_.empty());
		data_.resize(unit_size);
		std::memcpy(data_.data(), data, unit_size);
	}

	template <typename T>
		requires (std::is_trivially_copyable_v<T> && !std::is_pointer_v<T> && std::is_class_v<T>)
	void push_default(const T& v){
		assert(unit_size == sizeof(T));
		push_default(static_cast<const void*>(std::addressof(v)));
	}

	/**
	 * @brief check if the pending ubo object is different from the last one, if so, accept it and return True
	 * @return true if accepted(acquires timeline marching)
	 *
	 */
	[[nodiscard]] bool collapse() noexcept{
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
				// Fix: use unit_size instead of data_.size() for comparison
				//equal, do not marching and pop back
				data_.erase(p, data_.end());
				return false;
			}
			return true;
		}
	}

	void push(const void* data){
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

	void push(std::span<const std::byte> data){
		assert(unit_size == data.size());
		push(data.data());
	}

	template <typename T>
		requires (std::is_trivially_copyable_v<T> && !std::is_pointer_v<T> && std::is_class_v<T>)
	void push(const T& v){
		assert(unit_size == sizeof(T));
		push(static_cast<const void*>(std::addressof(v)));
	}

	bool empty() const noexcept{
		return data_.empty();
	}

	std::uint32_t get_count() const noexcept{
		return static_cast<std::uint32_t>(data_.size() / unit_size);
	}

	std::size_t get_required_byte_size() const noexcept{
		return data_.size() - pending * unit_size;
	}

	std::span<const std::byte> get_data_span() const noexcept{
		return {data_.data(), get_required_byte_size()};
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
	std::vector<dispatch_group_info> dispatch_config_storage{};
	std::vector<std::uint32_t> group_initial_vertex_data_timestamps_{};

	std::span<draw_uniform_data_entry> vertex_data_entries_{};
	std::byte* ptr_to_head{};
	std::uint32_t index_to_last_chunk_head_{};

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

	FORCE_INLINE auto get_used_dispatch_groups() const noexcept{
		return std::span{dispatch_config_storage.data(), currentMeshCount};
	}

	FORCE_INLINE auto get_used_time_line_datas() const noexcept{
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
		ptr_to_head = instruction_buffer_.data(); // Fix: use .data()
		index_to_last_chunk_head_ = 0;
		verticesBreakpoint = param.verticesBreakpoint;
		nextPrimitiveOffset = param.nextPrimitiveOffset;
		currentMeshCount = 0;
		pushedVertices = 0;
		pushedPrimitives = 0;

		setup_current_dispatch_group_info();
	}

	FORCE_INLINE void finalize(){
		if(pushedPrimitives){
			save_current_dispatch(static_cast<std::uint32_t>(-1));
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

		auto instruction_index = ptr_to_head - instruction_buffer_.data();
		if(instruction_buffer_.size() < instruction_index + head.size){
			instruction_buffer_.resize((instruction_buffer_.size() + head.size) * 2);
			ptr_to_head = instruction_buffer_.data() + instruction_index;
		}

		std::memcpy(ptr_to_head, data, head.size);
		ptr_to_head += head.size;

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
					instruction_index += head.size;
					save_current_dispatch(static_cast<std::uint32_t>(instruction_index));
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

			save_current_dispatch(static_cast<std::uint32_t>(instruction_index));
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
		while(cur != ptr_to_head){
			assert(cur < ptr_to_head);
			const instruction_head& head = get_instr_head(cur);
			fn(head, cur + sizeof(instruction_head));
			cur += head.size;
		}
	}

	FORCE_INLINE bool empty() const noexcept{
		return ptr_to_head == instruction_buffer_.data();
	}

private:
	FORCE_INLINE void save_current_dispatch(std::uint32_t next_instr_begin){
		assert(pushedPrimitives != 0);
		assert(index_to_last_chunk_head_ % 16 == 0);
		dispatch_config_storage[currentMeshCount].instruction_offset = index_to_last_chunk_head_;
		dispatch_config_storage[currentMeshCount].primitive_count = pushedPrimitives;


		++currentMeshCount;
		index_to_last_chunk_head_ = next_instr_begin;
		if(currentMeshCount != dispatch_config_storage.size()){
			setup_current_dispatch_group_info();
		}
	}

};

}
