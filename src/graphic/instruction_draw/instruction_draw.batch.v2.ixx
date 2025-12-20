// ReSharper disable CppExpressionWithoutSideEffects
module;

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

#include "magic_enum/magic_enum.hpp"

export module instruction_draw.batch.v2;

export import mo_yanxi.graphic.draw.instruction.general;
export import mo_yanxi.graphic.draw.instruction.util;
export import mo_yanxi.graphic.draw.instruction;
export import mo_yanxi.user_data_entry;
import mo_yanxi.type_register;
import mo_yanxi.vk;
import mo_yanxi.vk.cmd;
import mo_yanxi.vk.util;
import std;

namespace mo_yanxi::graphic::draw::instruction{
constexpr inline std::uint32_t MaxVerticesPerMesh = 64;

constexpr inline bool is_draw_instr(const instruction_head& head) noexcept{
	return head.type != instr_type::noop && head.type != instr_type::uniform_update;
}

inline void check_size(std::size_t size){
	if(size % 16 != 0){
		throw std::invalid_argument("instruction size must be a multiple of 16");
	}
}

export
struct batch_base_line_config{
	/**
	 * Size in power of two
	 */
	std::uint32_t instruction_baseline_size_pow{15};
	std::uint32_t image_usable_count{4};

	std::size_t get_instruction_size() const noexcept{
		return 1uz << instruction_baseline_size_pow;
	}
};

struct alignas(16) dispatch_group_info{
	std::uint32_t instruction_offset; //offset in 16 Byte TODO remove the scaling?
	std::uint32_t vertex_offset;
	std::uint32_t primitive_offset;
	std::uint32_t primitive_count;
};

struct submit_extend_params{
	std::uint32_t verticesBreakpoint{};
	std::uint32_t nextPrimitiveOffset{};
};

export
struct batch_v2;

struct vertex_check_result{
	std::uint32_t cur_offset;
	bool requires_advance;
};

struct draw_user_data_entry{
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

	std::uint32_t get_current_index() noexcept{
		if(data_.empty())return 0;
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
	 */
	[[nodiscard]] bool collapse() noexcept{
		if(!pending)return false;
		auto count = get_count();
		auto p = (count - 1) * unit_size + data_.begin();
		assert(count > 0);
		pending = false;
		if(count == 1){
			return true;
		}else{
			auto p_base = (count - 2) * unit_size + data_.data();
			if(std::memcmp(std::to_address(p), p_base, data_.size()) == 0){
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
		}else{
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
		return data_.size() / unit_size;
	}

	std::size_t get_required_byte_size() const noexcept{
		return data_.size() - pending * unit_size;
	}

	std::span<const std::byte> get_data_span() const noexcept{
		return {data_.data(), get_required_byte_size()};
	}
};

struct submit_group{
	friend batch_v2;
private:
	instruction_buffer instruction_buffer_{};
	std::vector<dispatch_group_info> dispatch_config_storage{};
	std::vector<std::uint32_t> group_initial_vertex_data_timestamps_{};

	std::span<draw_user_data_entry> vertex_data_entries_{};

	std::byte* ptr_to_head{};
	std::uint32_t index_to_last_chunk_head_{};

	std::uint32_t verticesBreakpoint{};
	std::uint32_t nextPrimitiveOffset{};

	std::uint32_t currentMeshCount{};
	std::uint32_t pushedVertices{};
	std::uint32_t pushedPrimitives{};

	void setup_current_dispatch_group_info(){
		for (const auto & [i, vertex_data_entry] : vertex_data_entries_ | std::views::enumerate){
			const std::uint32_t idx = vertex_data_entries_.size() * currentMeshCount + i;
			auto timeline = vertex_data_entry.get_current_index();
			group_initial_vertex_data_timestamps_[idx] = timeline;

		}

		dispatch_config_storage[currentMeshCount].primitive_offset = nextPrimitiveOffset;
		dispatch_config_storage[currentMeshCount].vertex_offset = verticesBreakpoint > 2
													  ? (verticesBreakpoint - 2)
													  : (verticesBreakpoint = 0);
	}
public:
	[[nodiscard]] submit_group() = default;
	[[nodiscard]] explicit(false) submit_group(
		std::size_t vertex_data_slot_count,
		std::uint32_t dispatch_group_count,
		std::span<draw_user_data_entry> entries
		)
	: dispatch_config_storage(dispatch_group_count)
	, group_initial_vertex_data_timestamps_(vertex_data_slot_count * dispatch_group_count), vertex_data_entries_(entries){

	}

	auto get_used_dispatch_groups() const noexcept{
		return std::span{dispatch_config_storage.data(), currentMeshCount};
	}

	auto get_used_time_line_datas() const noexcept{
		return std::span{group_initial_vertex_data_timestamps_.begin(), group_initial_vertex_data_timestamps_.begin() + currentMeshCount * vertex_data_entries_.size()};
	}

	std::size_t get_total_instruction_size() const noexcept{
		return ptr_to_head - instruction_buffer_.begin();
	}

	const std::byte* get_buffer_data() const noexcept{
		return instruction_buffer_.data();
	}

	submit_extend_params get_extend_able_params() const noexcept{
		return {verticesBreakpoint, nextPrimitiveOffset};
	}

	void reset(submit_extend_params param){
		//TODO is the memset necessary?

		std::memset(dispatch_config_storage.data(), 0, dispatch_config_storage.size() * sizeof(dispatch_group_info));
		ptr_to_head = instruction_buffer_.begin();
		index_to_last_chunk_head_ = 0;
		verticesBreakpoint = param.verticesBreakpoint;
		nextPrimitiveOffset = param.nextPrimitiveOffset;
		currentMeshCount = 0;
		pushedVertices = 0;
		pushedPrimitives = 0;

		setup_current_dispatch_group_info();
	}

	void finalize(){
		if(pushedPrimitives){
			save_current_dispatch(-1);
		}
	}

	void save_current_dispatch(std::uint32_t next_instr_begin){
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

	bool push(const instruction_head& head, const std::byte* data) noexcept {
		assert(std::to_underlying(head.type) < std::to_underlying(instruction::instr_type::SIZE));

		auto instruction_index = ptr_to_head - instruction_buffer_.begin();
		if(instruction_buffer_.size() < instruction_index + head.size){
			instruction_buffer_.resize((instruction_buffer_.size() + head.size) * 2);
			ptr_to_head = instruction_buffer_.begin() + instruction_index;
		}

		for(int i = 0; i < head.size; ++i){
			assert(ptr_to_head[i] == std::byte{});
		}

		std::memcpy(ptr_to_head, data, head.size);
		ptr_to_head += head.size;

		switch(head.type){
		case instr_type::noop :
		case instr_type::uniform_update :
			return true;
		default : break;
		}

		const auto get_remain_vertices = [&] FORCE_INLINE{
			return MaxVerticesPerMesh - pushedVertices;
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
			if(pushedVertices + nextVertices <= MaxVerticesPerMesh){
				pushedVertices += nextVertices;
				pushedPrimitives += get_primitive_count(head.type, data, nextVertices);
				verticesBreakpoint = 0;
				nextPrimitiveOffset = 0;

				if(pushedVertices == MaxVerticesPerMesh){
					instruction_index += head.size;
					save_current_dispatch(instruction_index);
					pushedPrimitives = 0;
					pushedVertices = 0;
				}
				return true;
			}

			//or save remain to
			const auto remains = get_remain_vertices();
			const auto primits = get_primitive_count(head.type, data, remains);
			nextPrimitiveOffset += primits;
			pushedPrimitives += primits;

			verticesBreakpoint += remains;

			save_current_dispatch(instruction_index);
			pushedPrimitives = 0;
			pushedVertices = 0;
		}

		return false;
	}

	void resolve_image(image_view_history_dynamic& history_dynamic) const{
		auto cur = instruction_buffer_.data();
		while(cur != ptr_to_head){
			assert(cur < ptr_to_head);
			const instruction_head& head = get_instr_head(cur);
			switch(head.type){
				case instr_type::noop : break;
				case instr_type::uniform_update : break;
			default : auto& gen = *instruction::start_lifetime_as<primitive_generic>(cur + sizeof(instruction_head));
				gen.image.index = history_dynamic.try_push(gen.image.get_image_view());
				break;
			}
			cur += head.size;
		}
	}

	bool empty() const noexcept{
		return ptr_to_head == instruction_buffer_.data();
	}
};

export
struct breakpoint_entry{
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
struct breakpoint_config{
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
	 */
	bool clear_draw_after_break{};

	std::vector<entry> user_defined_flags{};
	std::vector<std::byte> payload_storage{};

	[[nodiscard]] auto get_entries() const noexcept{
		return user_defined_flags | std::views::transform([base = payload_storage.data()](const entry& e){
			return breakpoint_entry{e.flag, e.get_payload(base)};
		});
	}

	void push(std::uint32_t flag, std::span<const std::byte> payload){
		entry e{flag};
		if(!payload.empty()){
			e.offset = payload_storage.size();
			e.size = payload.size();
			payload_storage.resize(e.offset + e.size);
			std::memcpy(payload_storage.data() + e.offset, payload.data(), e.size);
		}
		user_defined_flags.push_back(e);
;	}

	template <typename T>
		requires (std::is_trivially_copyable_v<T>)
	void push(std::uint32_t flag, const T& payload){
		this->push(flag, std::span{reinterpret_cast<const std::byte*>(std::addressof(payload)), sizeof(T)});
	}

	bool operator==(const breakpoint_config&) const noexcept = default;
};

struct submit_breakpoint{
	/**
	 * @brief before which submit group should this breakpoint occur
	 */
	unsigned break_before_index{};

	std::vector<std::uint32_t> uniform_buffer_marching_indices{};


	//TODO how to pass user breakpoint data??
	breakpoint_config config{};

	[[nodiscard]] submit_breakpoint() = default;

	[[nodiscard]] explicit submit_breakpoint(unsigned break_before_index)
		: break_before_index(break_before_index){
	}

	bool operator==(const submit_breakpoint&) const noexcept = default;
};

struct data_entry_group{
private:
	template <typename T>
	unsigned get_index_of() const{
		static constexpr auto idx = unstable_type_identity_of<T>();
		const auto* ientry = table[idx];
		const auto i = ientry - table.begin();
		if(i >= table.size()){
			throw std::out_of_range{std::format("Unknown type: {}", idx->name())};
		}
		return i;
	}
public:
	user_data_index_table<> table{};
	std::vector<draw_user_data_entry> entries{};

	vk::buffer gpu_buffer{};


	[[nodiscard]] data_entry_group() = default;

	[[nodiscard]] explicit data_entry_group(const user_data_index_table<>& table)
		: table(table), entries(table.size()){

		for (auto&& [idx, data_entry] : table | std::views::enumerate){
			entries[idx].unit_size = data_entry.entry.size;
			entries[idx].data_.reserve(data_entry.entry.size * 8);
		}
	}

	void reset() noexcept {
		for (auto& vertex_data_entry : entries){
			vertex_data_entry.reset();
		}
	}

	std::size_t size() const noexcept{
		return entries.size();
	}

	template <typename T>
	void push(const T& data){
		const unsigned i = get_index_of<T>();
		entries[i].push(data);
	}

	template <typename T>
	void push_default(const T& data){
		const unsigned i = get_index_of<T>();
		entries[i].push_default(data);
	}

	void push(std::uint32_t index, std::span<const std::byte> data){
		entries[index].push(data);
	}

	VkDeviceAddress get_buffer_address() const noexcept{
		return gpu_buffer.get_address();
	}

	VkDeviceSize load_to_gpu(const vk::allocator_usage& allocator, VkBufferUsageFlags flags){
		VkDeviceSize required_size{};
		for (const auto& entry : entries){
			required_size += entry.get_required_byte_size();
		}

		if(gpu_buffer.get_size() < required_size){
			gpu_buffer = vk::buffer{
					allocator, {
						.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
						.size = required_size,
						.usage = flags,
					}, {.usage = VMA_MEMORY_USAGE_CPU_TO_GPU}
				};
		}

		vk::buffer_mapper mapper{gpu_buffer};
		VkDeviceSize cur_offset{};
		for (const auto& entry : entries){
			mapper.load_range(entry.get_data_span(), cur_offset);
			cur_offset += entry.get_required_byte_size();
		}

		return cur_offset;
	}
};

export
struct breakpoint_command_context{
	vk::cmd::dependency_gen dependency{};
	std::vector<std::uint32_t> timelines{};
	std::vector<VkDeviceSize> buffer_offsets{};
	std::vector<VkBufferCopy> copy_info{};
	std::uint32_t current_submit_group_index{};

	[[nodiscard]] breakpoint_command_context() = default;

	[[nodiscard]] explicit breakpoint_command_context(std::size_t data_entry_count)
	: timelines(data_entry_count)
	, buffer_offsets(data_entry_count){
		dependency.buffer_memory_barriers.reserve(data_entry_count);
		copy_info.reserve(data_entry_count);
	}

	void submit_copy(VkCommandBuffer cmd, VkBuffer src, VkBuffer dst) {
		if(copy_info.empty())return;
		vkCmdCopyBuffer(cmd, src, dst, copy_info.size(), copy_info.data());
		copy_info.clear();
	}
};

struct dispatch_config{
	std::uint32_t group_offset;
	std::uint32_t _cap[3];
};

struct command_update_flags{

};


struct batch_v2{
private:
	batch_base_line_config config_{};
	vk::allocator_usage allocator_{};

	std::vector<submit_group> submit_groups_{};
	std::vector<submit_breakpoint> submit_breakpoints_{};

	submit_group* current_group{};


	std::size_t get_baseline_size() const noexcept{
		return config_.get_instruction_size();
	}

	std::vector<VkDrawMeshTasksIndirectCommandEXT> submit_info_{};
	vk::buffer_cpu_to_gpu buffer_dispatch_info_{};
	vk::buffer_cpu_to_gpu buffer_instruction_{};
	vk::buffer_cpu_to_gpu buffer_indirect_{};


	data_entry_group data_group_vertex_info_{};
	data_entry_group data_group_non_vertex_info_{};

	vk::buffer buffer_non_vertex_info_uniform_buffer_{};

	image_view_history_dynamic dynamic_image_view_history_{};

	std::vector<vk::binding_spec> bindings{};
	vk::descriptor_layout descriptor_layout_{};
	vk::dynamic_descriptor_buffer descriptor_buffer_{};

	vk::descriptor_layout non_vertex_descriptor_layout_{};
	vk::descriptor_buffer non_vertex_descriptor_buffer_{};

	VkSampler sampler_{};
	std::uint32_t hardware_mesh_maximum_dispatch_count_{};

	std::uint32_t get_mesh_dispatch_limit() const noexcept{
		return 32;
	}

public:
	[[nodiscard]] batch_v2() = default;

	//TODO basic descriptor usage binding?
	[[nodiscard]] explicit batch_v2(
		const vk::allocator_usage& a,
		const batch_base_line_config& config,
		const user_data_index_table<>& vertex_data_table,
		const user_data_index_table<>& non_vertex_data_table,
		VkSampler sampler
	)
	: config_(config)
	, allocator_{a}
	, buffer_indirect_(allocator_, sizeof(VkDrawMeshTasksIndirectCommandEXT) * 32, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
	, data_group_vertex_info_{vertex_data_table}
	, data_group_non_vertex_info_{non_vertex_data_table}
	, buffer_non_vertex_info_uniform_buffer_(allocator_, {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = sizeof(dispatch_config) + non_vertex_data_table.required_capacity(),
			.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
		}, {.usage = VMA_MEMORY_USAGE_GPU_ONLY})
	, dynamic_image_view_history_(config.image_usable_count)
	, bindings({
			{0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
			{1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
			{2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},

			{3, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER},

			{4, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
			{5, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},

		})
	, descriptor_layout_(allocator_.get_device(), VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
		[&](vk::descriptor_layout_builder& builder){
			//dispatch info {instruction range, vtx ubo index initialize}
			builder.push_seq(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);

			builder.push_seq(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);

			//instructions
			builder.push_seq(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);

			//Textures
			builder.push_seq(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);

			//vertex ubos
			for(unsigned i = 0; i < vertex_data_table.size(); ++i){
				builder.push_seq(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);
			}
			//TODO uniform buffer
		})
	, descriptor_buffer_(allocator_, descriptor_layout_, descriptor_layout_.binding_count(), {})
	, non_vertex_descriptor_layout_{
		a.get_device(),
		VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
		[&](vk::descriptor_layout_builder& builder){
			for (const auto & data_table : non_vertex_data_table){
				builder.push_seq(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT);
			}
		}}
	, non_vertex_descriptor_buffer_(a, non_vertex_descriptor_layout_, non_vertex_descriptor_layout_.binding_count())
	, sampler_(sampler){

		for(auto&& [group, chunk] :
		    this->data_group_non_vertex_info_.table
		    | std::views::chunk_by([](const user_data_identity_entry& l, const user_data_identity_entry& r){
			    return l.entry.group_index == r.entry.group_index;
		    })
		    | std::views::enumerate){
			assert(group == 0);

			vk::descriptor_mapper mapper{non_vertex_descriptor_buffer_};
			for(auto&& [binding, entry] : chunk | std::views::enumerate){
				(void)mapper.set_uniform_buffer(
					binding,
					buffer_non_vertex_info_uniform_buffer_.get_address() + sizeof(dispatch_config) + entry.entry.global_offset, entry.entry.size
				);
			}
		}
	}

	void begin_rendering() noexcept{
		if(submit_groups_.empty()){
			submit_groups_.push_back({data_group_vertex_info_.size(), get_mesh_dispatch_limit(), data_group_vertex_info_.entries});
		}

		for (auto && submit_group : submit_groups_){
			submit_group.instruction_buffer_.clear();
		}

		data_group_non_vertex_info_.reset();
		data_group_vertex_info_.reset();


		submit_groups_.front().reset({});
		current_group = submit_groups_.data();
		submit_breakpoints_.clear();
	}

	void end_rendering() noexcept{
		current_group->finalize();
		dynamic_image_view_history_.optimize_and_reset();

		if(current_group->empty()){

		}else{
			current_group++;
			submit_breakpoints_.emplace_back(get_current_submit_group_index());
		}

		const auto submit_group_subrange = get_valid_submit_groups();

		for (const auto & group_subrange : submit_group_subrange){
			group_subrange.resolve_image(dynamic_image_view_history_);
		}


	}

	std::span<submit_group> get_valid_submit_groups(){
		return std::span{submit_groups_.data(), current_group};
	}

	void push_state(std::uint32_t flag, std::span<const std::byte> payload){
		submit_breakpoint* breakpoint;
		if(current_group->get_total_instruction_size() != 0 || submit_breakpoints_.empty()){
			current_group->finalize();
			advance_current_group();
			breakpoint = &submit_breakpoints_.emplace_back(get_current_submit_group_index());
		}else{
			breakpoint = &submit_breakpoints_.back();
		}

		breakpoint->config.push(flag, payload);
	}

	void push_instr(std::span<const std::byte> instr){
		assert(current_group);

		check_size(instr.size());
		const auto& instr_head = get_instr_head(instr.data());
		assert(std::to_underlying(instr_head.type) < std::to_underlying(instruction::instr_type::SIZE));


		if(instr_head.type == instr_type::uniform_update){
			const auto payload = get_payload_data_span(instr.data());
			const auto targetIndex = instr_head.payload.ubo.global_index;
			//TODO use other name to replace group index
			if(instr_head.payload.ubo.group_index){
				data_group_non_vertex_info_.push(targetIndex, payload);
			}else{
				data_group_vertex_info_.push(targetIndex, payload);
			}

			return;
		}

		for (auto&& [idx, vertex_data_entry] : data_group_vertex_info_.entries | std::views::enumerate){
			if(vertex_data_entry.collapse()){
				const instruction_head instruction_head{
					.type = instr_type::uniform_update,
					.size = sizeof(instruction::instruction_head),
					.payload = {.marching_data = {static_cast<std::uint32_t>(idx)}}
				};
				try_push_(instruction_head);
			}
		}

		submit_breakpoint* breakpoint{};
		for (auto&& [idx, vertex_data_entry] : data_group_non_vertex_info_.entries | std::views::enumerate){
			if(vertex_data_entry.collapse()){
				if(!breakpoint){
					const auto cur_idx = get_current_submit_group_index() + 1;
					breakpoint = &submit_breakpoints_.emplace_back(cur_idx);
				}

				breakpoint->uniform_buffer_marching_indices.push_back(idx);
			}
		}
		if(breakpoint){
			current_group->finalize();
			advance_current_group();
		}

		try_push_(instr_head, instr.data());
	}

	std::size_t get_current_submit_group_index() const noexcept{
		assert(current_group != nullptr);
		return current_group - submit_groups_.data();
	}


	bool load_to_gpu(){
		assert(current_group != nullptr);
		assert(!submit_groups_.empty());
		const auto submit_group_subrange = get_valid_submit_groups();
		if(submit_group_subrange.empty()){
			return false;
		}

		const auto dispatchCountGroups = [&]{
			submit_info_.resize(submit_breakpoints_.size());
			std::uint32_t currentSubmitGroupIndex = 0;
			for (const auto & [idx, submit_breakpoint] : submit_breakpoints_ | std::views::enumerate){
				const auto section_end = submit_breakpoint.break_before_index;
				std::uint32_t submitCount{};
				for(auto i = currentSubmitGroupIndex; i < section_end; ++i){
					submitCount += submit_group_subrange[i].get_used_dispatch_groups().size();
				}
				submit_info_[idx] = {submitCount, 1, 1};
				currentSubmitGroupIndex = section_end;
			}
			return std::span{std::as_const(submit_info_)};
		}();


		bool requires_command_record = false;

		if(const auto reqSize = dispatchCountGroups.size() * sizeof(VkDrawMeshTasksIndirectCommandEXT); buffer_indirect_.get_size() < reqSize){
			buffer_indirect_ = vk::buffer_cpu_to_gpu(allocator_, reqSize, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
		}

		vk::buffer_mapper{buffer_indirect_}.load_range(dispatchCountGroups);

		std::uint32_t totalDispatchCount{};
		const auto dispatch_timeline_size = data_group_vertex_info_.size() * sizeof(std::uint32_t);
		const auto dispatch_unit_size = sizeof(dispatch_group_info) + dispatch_timeline_size;
		{
			//setup dispatch config buffer;
			VkDeviceSize deviceSize{};
			for (const auto & group : submit_group_subrange){
				deviceSize += group.get_used_dispatch_groups().size_bytes() + group.get_used_time_line_datas().size_bytes();
			}
			deviceSize += dispatch_unit_size; //patch sentinel instruction offset

			if(buffer_dispatch_info_.get_size() < deviceSize){
				buffer_dispatch_info_ = vk::buffer_cpu_to_gpu{
					allocator_, deviceSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
				};
			}

			vk::buffer_mapper mapper{buffer_dispatch_info_};
			std::vector<std::byte> buffer{};
			VkDeviceSize pushed_size{};
			std::uint32_t current_instr_offset{};

			for (const auto & [idx, group] : submit_group_subrange | std::views::enumerate){
				const auto dispatch = group.get_used_dispatch_groups();
				const auto timeline = group.get_used_time_line_datas();
				buffer.resize(dispatch.size_bytes() + timeline.size_bytes());

				for(std::size_t i = 0; i < dispatch.size(); ++i){
					auto info = dispatch[i];
					info.instruction_offset += current_instr_offset;
					std::memcpy(buffer.data() + dispatch_unit_size * i, &info, sizeof(info));
					std::memcpy(buffer.data() + dispatch_unit_size * i + sizeof(info), timeline.data() + i * data_group_vertex_info_.size(), dispatch_timeline_size);
				}

				mapper.load_range(buffer, pushed_size);
				pushed_size += buffer.size();
				totalDispatchCount += dispatch.size();

				current_instr_offset += group.get_total_instruction_size();
			}

			//Add instruction sentinel
			mapper.load(dispatch_group_info{current_instr_offset}, pushed_size);
		}

		data_group_vertex_info_.load_to_gpu(allocator_, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
		data_group_non_vertex_info_.load_to_gpu(allocator_, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

		//update buffers
		{
			struct submit_group_meta_data{
				std::uint32_t instruction_begin{};
				std::uint32_t _align;
			};

			std::uint32_t instructionSize{};
			for (const auto & [idx, submit_group] : submit_group_subrange | std::views::enumerate){
				instructionSize += submit_group.get_total_instruction_size();
			}

			if(buffer_instruction_.get_size() < instructionSize){
				buffer_instruction_ = vk::buffer_cpu_to_gpu{
					allocator_, instructionSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
				};
			}


			{
				vk::buffer_mapper mapper{buffer_instruction_};

				std::size_t current_offset{};
				for (const auto & [idx, submit_group] : submit_group_subrange | std::views::enumerate){
					(void)mapper.load_range(std::span{submit_group.get_buffer_data(), submit_group.get_total_instruction_size()}, current_offset);
					current_offset += submit_group.get_total_instruction_size();
				}
			}

			{
				//Setup descriptor buffer

				if(const auto cur_size = dynamic_image_view_history_.get().size(); bindings[3].count != cur_size){
					bindings[3].count = cur_size;
					descriptor_buffer_.reconfigure(descriptor_layout_, descriptor_layout_.binding_count(), bindings);
					requires_command_record = true;
				}

				vk::dynamic_descriptor_mapper dbo_mapper{descriptor_buffer_};
				dbo_mapper.set_element_at(0, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, buffer_non_vertex_info_uniform_buffer_.get_address(), sizeof(dispatch_config));

				dbo_mapper.set_element_at(1, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, buffer_dispatch_info_.get_address(), dispatch_unit_size * (1 + totalDispatchCount));

				dbo_mapper.set_element_at(2, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, buffer_instruction_.get_address(), instructionSize);

				dbo_mapper.set_images_at(3, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, sampler_, dynamic_image_view_history_.get());

				VkDeviceSize cur_offset{};
				for (const auto& [i, entry] : data_group_vertex_info_.entries | std::views::enumerate){
					dbo_mapper.set_element_at(4 + i, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, data_group_vertex_info_.get_buffer_address() + cur_offset, entry.get_required_byte_size());
					cur_offset += entry.get_required_byte_size();
				}
			}
		}

		return requires_command_record;
	}


	std::array<VkDescriptorSetLayout, 2> get_descriptor_set_layout() const noexcept{
		return {descriptor_layout_, non_vertex_descriptor_layout_};
	}

	breakpoint_command_context record_command_set_up_non_vertex_buffer(VkCommandBuffer cmd) const {
		breakpoint_command_context ctx{data_group_non_vertex_info_.size()};
		VkDeviceSize currentBufferOffset{};
		for (const auto & [idx, table] : data_group_non_vertex_info_.table | std::views::enumerate){
			const auto& entry = data_group_non_vertex_info_.entries[idx];
			if(entry.empty())continue;

			ctx.copy_info.push_back({
				.srcOffset = currentBufferOffset,
				.dstOffset = sizeof(dispatch_config) + table.entry.global_offset,
				.size = table.entry.size
			});
			ctx.buffer_offsets[idx] = currentBufferOffset;
			currentBufferOffset += entry.get_required_byte_size();
		}

		constexpr dispatch_config cfg{};
		vkCmdUpdateBuffer(cmd, buffer_non_vertex_info_uniform_buffer_, 0, sizeof(dispatch_config), &cfg);

		ctx.submit_copy(cmd, data_group_non_vertex_info_.gpu_buffer, buffer_non_vertex_info_uniform_buffer_);
		ctx.dependency.push(buffer_non_vertex_info_uniform_buffer_,
			VK_PIPELINE_STAGE_2_COPY_BIT,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
			VK_ACCESS_2_UNIFORM_READ_BIT,
				VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, sizeof(dispatch_config)
			);
		ctx.dependency.push(buffer_non_vertex_info_uniform_buffer_,
			VK_PIPELINE_STAGE_2_COPY_BIT,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT,
			VK_ACCESS_2_UNIFORM_READ_BIT,
				VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, 0, sizeof(dispatch_config)
			);
		ctx.dependency.apply(cmd);
		return ctx;
	}

	void record_command_advance_non_vertex_buffer(breakpoint_command_context& ctx, VkCommandBuffer cmd, std::size_t current_breakpoint) const{
		const auto& breakpoint = submit_breakpoints_[current_breakpoint];
		// if(breakpoint.uniform_buffer_marching_indices.empty())return;

		const bool insertFullBuffer = breakpoint.uniform_buffer_marching_indices.size() > data_group_non_vertex_info_.size() / 2;
		for (const auto idx : breakpoint.uniform_buffer_marching_indices){
			const auto timestamp = ++ctx.timelines[idx];
			const auto unitSize = data_group_non_vertex_info_.table[idx].size;
			const auto dst_offset = sizeof(dispatch_config) + data_group_non_vertex_info_.table[idx].global_offset;
			const auto src_offset = ctx.buffer_offsets[idx] + timestamp * unitSize;

			ctx.copy_info.push_back({
				.srcOffset = src_offset,
				.dstOffset = dst_offset,
				.size = unitSize
			});

			if(!insertFullBuffer){
				ctx.dependency.push(buffer_non_vertex_info_uniform_buffer_,
					VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
					VK_ACCESS_2_UNIFORM_READ_BIT,
					VK_PIPELINE_STAGE_2_COPY_BIT,
					VK_ACCESS_2_TRANSFER_WRITE_BIT,
						VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, dst_offset, unitSize
					);
			}
		}

		if(insertFullBuffer){
			ctx.dependency.push(buffer_non_vertex_info_uniform_buffer_,
				VK_PIPELINE_STAGE_2_COPY_BIT,
				VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
				VK_ACCESS_2_UNIFORM_READ_BIT,
				VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, sizeof(dispatch_config)
			);
		}

		ctx.dependency.push(buffer_non_vertex_info_uniform_buffer_,
			VK_PIPELINE_STAGE_2_COPY_BIT,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT,
			VK_ACCESS_2_UNIFORM_READ_BIT,
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, 0, sizeof(dispatch_config)
		);

		ctx.current_submit_group_index += submit_info_[current_breakpoint].groupCountX;
		const dispatch_config cfg{ctx.current_submit_group_index};

		ctx.dependency.apply(cmd, true);
		vkCmdUpdateBuffer(cmd, buffer_non_vertex_info_uniform_buffer_, 0, sizeof(dispatch_config), &cfg);
		ctx.submit_copy(cmd, data_group_non_vertex_info_.gpu_buffer, buffer_non_vertex_info_uniform_buffer_);
		ctx.dependency.swap_stages();
		ctx.dependency.apply(cmd);

	}

	template <typename T = std::allocator<descriptor_buffer_usage>>
	void record_load_to_record_context(record_context<T>& record_context){
		record_context.get_bindings().push_back(descriptor_buffer_usage{
			.info = descriptor_buffer_,
			.target_set = 0
		});
		record_context.get_bindings().push_back(descriptor_buffer_usage{
			.info = non_vertex_descriptor_buffer_,
			.target_set = 1
		});
	}

	void record_command_draw(VkCommandBuffer cmd, std::uint32_t dispatch_group_index) const {
		vk::cmd::drawMeshTasksIndirect(cmd, buffer_indirect_, dispatch_group_index * sizeof(VkDrawMeshTasksIndirectCommandEXT), 1);
	}

	std::uint32_t get_submit_sections_count() const noexcept{
		return submit_breakpoints_.size();
	}

	const breakpoint_config& get_break_config_at(std::size_t index) const noexcept{
		return submit_breakpoints_[index].config;
	}

	//TODO replace the getter
	[[nodiscard]] data_entry_group& get_data_group_vertex_info() noexcept{
		return data_group_vertex_info_;
	}

	[[nodiscard]] data_entry_group& get_data_group_non_vertex_info() noexcept{
		return data_group_non_vertex_info_;
	}

private:
	void advance_current_group(){
		auto last_param = current_group->get_extend_able_params();
		if(current_group == std::to_address(submit_groups_.rbegin())){
			current_group = &submit_groups_.emplace_back(data_group_vertex_info_.size(), get_mesh_dispatch_limit(), data_group_vertex_info_.entries);
		}else{
			++current_group;
		}

		current_group->reset(last_param);
	}

	bool try_push_(const instruction_head& instr_head, const std::byte* instr){
		while(!current_group->push(instr_head, instr)){
			advance_current_group();
		}
		return false;
	}

	bool try_push_(const instruction_head& instr_head){
		return try_push_(instr_head, reinterpret_cast<const std::byte*>(std::addressof(instr_head)));
	}
};

}