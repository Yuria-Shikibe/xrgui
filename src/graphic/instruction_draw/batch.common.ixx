module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

#if __has_include(<vulkan/vulkan.h>)
#include <vulkan/vulkan.h>
#define BACKEND_HAS_VULKAN
#endif


export module mo_yanxi.graphic.draw.instruction.batch.common;

import mo_yanxi.graphic.draw.instruction.general;

import std;

namespace mo_yanxi::graphic::draw::instruction{
/**
 * @brief GPU端 Storage Buffer 中每个 Dispatch Group 的元数据布局
 *
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
 * @brief All flag < 0 is reserved for builtin usage.
 */
export
using state_transition_entry_flag_t = std::int32_t;

export
enum struct builtin_transition_flag : state_transition_entry_flag_t{
	push_constant
};

export
constexpr builtin_transition_flag make_builtin_flag_from(state_transition_entry_flag_t flag) noexcept{
	assert(flag < 0);
	return builtin_transition_flag{flag & (~(1 << (sizeof(state_transition_entry_flag_t) * 8 - 1)))};
}


export
constexpr state_transition_entry_flag_t make_builtin_flag_from(builtin_transition_flag flag) noexcept{
	return std::to_underlying(flag) | (1 << (sizeof(state_transition_entry_flag_t) * 8 - 1));
}

#ifdef BACKEND_HAS_VULKAN
export
struct push_constant_config_vulkan{
	VkShaderStageFlags  stage_flags;
	std::uint32_t       device_offset;
};
#endif

export
union push_constant_config{
#ifdef BACKEND_HAS_VULKAN
	push_constant_config_vulkan vk;
#endif

};

export
union entry_config{
	push_constant_config push_constant;
	std::array<std::byte, 8> user_data;
};


export
struct state_transition_entry{
	state_transition_entry_flag_t flag;
	entry_config config;

	std::span<const std::byte> data;

	template <typename T>
		requires (std::is_trivially_copyable_v<T>)
	const T& as() const noexcept{
		assert(sizeof(T) == data.size());
		return *reinterpret_cast<const T*>(data.data());
	}

	constexpr bool is_builtin() const noexcept{
		return flag < 0;
	}

#ifdef BACKEND_HAS_VULKAN
	void cmd_push(VkCommandBuffer cmdbuf, VkPipelineLayout layout) const{
		assert(make_builtin_flag_from(flag) == builtin_transition_flag::push_constant);
		vkCmdPushConstants(cmdbuf, layout, config.push_constant.vk.stage_flags, config.push_constant.vk.device_offset,
			data.size(), data.data());
	}


	constexpr bool process_builtin(VkCommandBuffer cmdbuf, VkPipelineLayout layout) const{
		if(!is_builtin())return false;

		switch(make_builtin_flag_from(flag)){
		case builtin_transition_flag::push_constant:
			cmd_push(cmdbuf, layout);
		default: std::unreachable();
		}

		return true;
	}

#endif

};


export
struct state_transition_config{
	struct entry{
		state_transition_entry_flag_t flag;

		//TODO move config into data section?
		entry_config config;

		std::uint32_t offset;
		std::uint32_t size;

		constexpr std::span<const std::byte> get_payload(const std::byte* basePtr) const noexcept{
			return {basePtr + offset, size};
		}
	};

	/**
	 * @brief determine the LOAD_OP after this call
	 *
	 */
	bool clear_draw_after_break{};

private:
	std::vector<entry> user_defined_flags{};
	std::vector<std::byte> payload_storage{};

public:

	[[nodiscard]] auto get_entries() const noexcept{
		return user_defined_flags | std::views::transform([base = payload_storage.data()](const entry& e){
			return state_transition_entry{e.flag, e.config, e.get_payload(base)};
		});
	}

	void push(state_transition_entry_flag_t flag, entry_config config, std::span<const std::byte> payload){
		entry e{flag, config};
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
	void push(state_transition_entry_flag_t flag, entry_config config, const T& payload){
		this->push(flag, config, std::span{reinterpret_cast<const std::byte*>(std::addressof(payload)), sizeof(T)});
	}

	void push(state_transition_entry_flag_t flag, std::span<const std::byte> payload){
		push(flag, {.user_data = {}}, payload);
	}

	template <typename T>
		requires (std::is_trivially_copyable_v<T>)
	void push(state_transition_entry_flag_t flag, const T& payload){
		this->push(flag, {.user_data = {}}, std::span{reinterpret_cast<const std::byte*>(std::addressof(payload)), sizeof(T)});
	}


	void push_constant(push_constant_config config, std::span<const std::byte> payload){
		push(make_builtin_flag_from(builtin_transition_flag::push_constant), {.push_constant = config}, payload);
	}


	void append(const state_transition_config& other){
		clear_draw_after_break = clear_draw_after_break || other.clear_draw_after_break;
		const auto curOff = payload_storage.size();
		const auto curEntrySz = user_defined_flags.size();

		payload_storage.append_range(other.payload_storage);
		user_defined_flags.append_range(other.user_defined_flags);

		for(std::size_t i = curEntrySz; i < user_defined_flags.size(); ++i){
			user_defined_flags[i].offset += curOff;
		}
	}

	void append_front(const state_transition_config& other){
		clear_draw_after_break = clear_draw_after_break || other.clear_draw_after_break;
		const auto curOff = other.payload_storage.size();
		const auto curEntrySz = other.user_defined_flags.size();

		payload_storage.insert_range(payload_storage.begin(), other.payload_storage);
		user_defined_flags.insert_range(user_defined_flags.begin(), other.user_defined_flags);

		for(std::size_t i = curEntrySz; i < user_defined_flags.size(); ++i){
			user_defined_flags[i].offset += curOff;
		}
	}

	void clear() noexcept{
		clear_draw_after_break = false;
		user_defined_flags.clear();
		payload_storage.clear();
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

	/**
	 * @brief check if the pending ubo object is different from the last one, if so, accept it and return True
	 * @return true if accepted(acquires timeline marching)
	 *
	 */
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
				// Fix: use unit_size instead of data_.size() for comparison
				//equal, do not marching and pop back
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
		ptr_to_head = instruction_buffer_.data(); // Fix: use .data()
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
