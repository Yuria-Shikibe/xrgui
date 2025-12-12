// ReSharper disable CppExpressionWithoutSideEffects
module;

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module instruction_draw.batch.v2;

export import mo_yanxi.graphic.draw.instruction.general;
export import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.vk;
import mo_yanxi.vk.util;
import std;

namespace mo_yanxi::graphic::draw::instruction{
constexpr inline std::uint32_t MaxVerticesPerMesh = 64;

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
export
struct batch_v2;

struct submit_group{
	friend batch_v2;
private:
	instruction_buffer instruction_buffer_{};
	std::vector<dispatch_group_info> dispatch_config_storage{};
	std::vector<std::uint32_t> group_initial_vertex_data_timestamps_{};


	std::byte* ptr_to_head{};
	std::uint32_t index_to_last_chunk_head_{};

	std::uint32_t verticesBreakpoint{};
	std::uint32_t nextPrimitiveOffset{};

	std::uint32_t currentMeshCount{};
	std::uint32_t pushedVertices{};
	std::uint32_t pushedPrimitives{};

	void setup_current_dispatch_group_info(){
		dispatch_config_storage[currentMeshCount].primitive_offset = nextPrimitiveOffset;
		dispatch_config_storage[currentMeshCount].vertex_offset = verticesBreakpoint > 2
													  ? (verticesBreakpoint - 2)
													  : (verticesBreakpoint = 0);
	}
public:
	[[nodiscard]] submit_group() = default;
	[[nodiscard]] explicit(false) submit_group(
		std::size_t vertex_data_slot_count,
		std::uint32_t dispatch_group_count
		)
	: dispatch_config_storage(dispatch_group_count)
	, group_initial_vertex_data_timestamps_(vertex_data_slot_count){

	}

	auto get_used_dispatch_groups() const noexcept{
		return std::span{dispatch_config_storage.data(), currentMeshCount};
	}

	std::size_t get_used_size() const noexcept{
		return ptr_to_head - instruction_buffer_.begin();
	}

	const std::byte* get_buffer_data() const noexcept{
		return instruction_buffer_.data();
	}

	submit_extend_params get_extend_able_params() const noexcept{
		return {verticesBreakpoint, nextPrimitiveOffset};
	}

	void reset(submit_extend_params param){
		std::memset(dispatch_config_storage.data(), 0, dispatch_config_storage.size() * sizeof(dispatch_group_info));
		ptr_to_head = instruction_buffer_.begin();
		index_to_last_chunk_head_ = 0;
		verticesBreakpoint = param.verticesBreakpoint;
		nextPrimitiveOffset = param.nextPrimitiveOffset;
		currentMeshCount = 0;
		pushedVertices = 0;
		pushedPrimitives = 0;
	}

	bool push(const instruction_head& head, const std::byte* data){
		assert(std::to_underlying(head.type) < std::to_underlying(instruction::instr_type::SIZE));

		const auto instruction_index = ptr_to_head - instruction_buffer_.begin();
		if(instruction_buffer_.size() < instruction_index + head.size){
			instruction_buffer_.resize((instruction_buffer_.size() + head.size) * 2);
			ptr_to_head = instruction_buffer_.begin() + instruction_index;
		}

		std::memcpy(ptr_to_head, data, head.size);
		ptr_to_head += head.size;

		switch(head.type){
		case instr_type::noop :
		case instr_type::uniform_update :
			return false;
		default : break;
		}

		const auto get_remain_vertices = [&] FORCE_INLINE{
			return MaxVerticesPerMesh - pushedVertices;
		};

		const auto save_chunk_head_and_incr = [&] FORCE_INLINE{
			assert(pushedPrimitives != 0);
			assert(index_to_last_chunk_head_ % 16 == 0);
			dispatch_config_storage[currentMeshCount].instruction_offset = index_to_last_chunk_head_;
			dispatch_config_storage[currentMeshCount].primitive_count = pushedPrimitives;
			++currentMeshCount;
			index_to_last_chunk_head_ = ptr_to_head - instruction_buffer_.begin();
			if(currentMeshCount != dispatch_config_storage.size()){
				setup_current_dispatch_group_info();
			}
		};

		const auto instructionVertices = get_vertex_count(head.type, data);

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
					save_chunk_head_and_incr();
				}
				return false;
			}

			//or save remain to
			const auto remains = get_remain_vertices();
			const auto primits = get_primitive_count(head.type, ptr_to_head, remains);
			nextPrimitiveOffset += primits;
			pushedPrimitives += primits;

			verticesBreakpoint += remains;

			save_chunk_head_and_incr();
		}

		return true;
	}
};


}

namespace mo_yanxi::graphic::draw::instruction{

bool is_draw_instr(const instruction_head& head) noexcept{
	return head.type != instr_type::noop && head.type != instr_type::uniform_update;
}

void check_size(std::size_t size){
	if(size % 16 != 0){
		throw std::invalid_argument("instruction size must be a multiple of 16");
	}
}

#pragma region Legacy
//vertex only ubo: 使用timeline标记当前最新ubo数据的下标，在draw中扫描指令获取最新顶点全局数据
//other ubo: 直接插入draw断点，存储对应下标，需要时拷贝到ubo

//
// struct submit_group{
// 	instruction_buffer instr_buf_{};
// 	image_view_history_dynamic image_view_history_dynamic_{};
// 	std::byte* head_{};
//
// 	std::size_t pushed_vertices_{};
// 	static constexpr std::size_t max_vertices_per_group = 1024 * 32;
// 	//TODO limit instruction vertex draw count (should be 1024 * 64)
// 	std::size_t instr_size_limit_{};
//
// 	std::vector<std::uint32_t> group_initial_vertex_data_indices_{};
//
// 	//TODO group blend state
// 	//TODO group non vertex ubo
//
//
// 	[[nodiscard]] submit_group() = default;
//
// 	[[nodiscard]] submit_group(
// 		batch_base_line_config config,
// 		std::size_t vertex_timeline_capacity)
// 		: instr_buf_(config.get_instruction_size()),
// 		  image_view_history_dynamic_(config.instruction_baseline_size_pow),
// 		  group_initial_vertex_data_indices_(vertex_timeline_capacity){
// 	}
//
// 	void reset(){
// 		image_view_history_dynamic_.reset();
// 		head_ = instr_buf_.begin();
// 		pushed_vertices_ = 0;
// 		group_initial_vertex_data_indices_.assign(group_initial_vertex_data_indices_.size(), 0);
// 	}
//
// 	std::size_t get_used_size() const noexcept{
// 		return head_ - instr_buf_.begin();
// 	}
//
// 	std::span<const std::byte> get_buffer_data() const noexcept{
// 		return {instr_buf_.begin(), head_};
// 	}
//
// 	[[nodiscard]] std::byte* acquire(std::size_t size){
// 		assert(head_ != nullptr);
//
// 		while(true){
// 			auto cur = head_ - instr_buf_.begin();
// 			auto sz = instr_buf_.size();
// 			if(sz - cur < size){
// 				return nullptr;
// 			}else{
// 				auto rst = head_;
// 				head_ += size;
// 				return rst;
// 			}
// 		}
// 	}
//
// 	[[nodiscard]] bool try_push(const instruction_head& instr_head, std::span<const std::byte> data){
// 		if(instr_head.type == instr_type::noop)return true;
// 		if(instr_head.type != instr_type::uniform_update){
// 			if(pushed_vertices_ + instr_head.payload.draw.primitive_count > max_vertices_per_group){
// 				return false;
// 			}
// 			const auto& primitive_gen = *instruction::start_lifetime_as<primitive_generic>(data.data() + sizeof(instruction_head));
// 			auto idx = image_view_history_dynamic_.try_push(primitive_gen.image.get_image_view());
// 			if(idx == image_view_history_dynamic_.size()){
// 				return false;
// 			}
//
// 			auto* p_data = acquire(data.size());
// 			if(!p_data)return false;
// 			std::memcpy(p_data, data.data(), data.size());
// 			auto& primitive_gen_mut = *instruction::start_lifetime_as<primitive_generic>(p_data + sizeof(instruction_head));
// 			primitive_gen_mut.image.index = idx;
// 			pushed_vertices_ += instr_head.payload.draw.primitive_count;
// 			return true;
// 		}else{
// 			auto* p_data = acquire(data.size());
// 			if(!p_data)return false;
// 			std::memcpy(p_data, data.data(), data.size());
// 			return true;
// 		}
// 	}
// };
#pragma endregion

struct vertex_data_entry{
	std::size_t unit_size{};
	std::vector<std::byte> data_{};
	bool pending{};

	void reset() noexcept{
		data_.clear();
		pending = false;
	}

	std::size_t get_current_index() const noexcept{
		assert(!data_.empty());
		assert(!pending);
		return get_count();
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
		if(count == 1){
			pending = false;
			return true;
		}else{
			auto p_base = (count - 2) * unit_size + data_.data();
			if(std::memcmp(std::to_address(p), p_base, data_.size()) == 0){
				//equal, do not marching and pop back
				pending = false;
				data_.erase(p, data_.end());
				return false;
			}
			return true;
		}
	}

	void push(std::span<const std::byte> data){
		assert(unit_size == data.size());

		if(pending){
			auto last = get_count();
			assert(last > 0);
			auto p = (last - 1) * unit_size + data_.data();
			std::memcpy(p, data.data(), data.size());
		}else{
			auto last = get_count();
			data_.resize(data_.size() + data.size());
			auto p = last * unit_size + data_.data();
			std::memcpy(p, data.data(), data.size());
			pending = true;
		}

	}

	std::size_t get_count() const noexcept{
		return data_.size() / unit_size;
	}

	std::size_t get_required_byte_size() const noexcept{
		return data_.size() - pending * unit_size;
	}

	std::span<const std::byte> get_data_span() const noexcept{
		return {data_.data(), get_required_byte_size()};
	}
};

struct batch_v2{
private:
	batch_base_line_config config_{};
	vk::allocator_usage allocator_{};

	std::vector<submit_group> submit_groups_{};
	submit_group* current_group{};

	std::byte* head_{nullptr};

	std::size_t get_baseline_size() const noexcept{
		return config_.get_instruction_size();
	}

	vk::buffer gpu_instruction_buffer_{};
	vk::buffer vertex_data_buffer_{};
	vk::buffer dispatch_group_config_buffer_{};
	vk::buffer indirect_buffer_{};

	user_data_index_table<> user_vertex_data_entries_{};
	// user_data_index_table<> user_non_vertex_data_entries_{};
	std::vector<vertex_data_entry> vertex_data_entries_{};

	vk::descriptor_layout descriptor_layout_{};
	vk::descriptor_buffer descriptor_buffer_{};

	VkSampler sampler_{};
	std::uint32_t hardware_mesh_maximum_dispatch_count_{};

	std::uint32_t get_mesh_dispatch_limit() const noexcept{
		return 32;
	}

public:
	[[nodiscard]] batch_v2() = default;

	[[nodiscard]] explicit batch_v2(
		const vk::allocator_usage& a,
		const batch_base_line_config& config,
		const user_data_index_table<>& vertex_data_table,
		VkSampler sampler
	)
		: config_(config)
		, allocator_{a}
		, indirect_buffer_(allocator_, {
				.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
				.size = sizeof(VkDrawMeshTasksIndirectCommandEXT),
				.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
			}, {.usage = VMA_MEMORY_USAGE_CPU_TO_GPU})
		, user_vertex_data_entries_{vertex_data_table}
		, descriptor_layout_{
			a.get_device(), VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
			[&](vk::descriptor_layout_builder& builder){
				//task meta info {instruction range, vtx ubo index initialize}
				builder.push_seq(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);
				//instructions
				builder.push_seq(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);
				//group dispatch payloads
				builder.push_seq(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);
				//instructions
				// builder.push_seq(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, config.image_usable_count);
				//vertex ubos
				for(unsigned i = 0; i < vertex_data_table.size(); ++i){
					builder.push_seq(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);
				}
				//TODO uniform buffer
			}}
		, descriptor_buffer_(allocator_, descriptor_layout_, descriptor_layout_.binding_count())
		, sampler_(sampler){
		set_batch_vertex_data_entries_();
	}

	void begin_rendering() noexcept{
		if(submit_groups_.empty()){
			submit_groups_.push_back({user_vertex_data_entries_.size(), get_mesh_dispatch_limit()});
		}

		for (auto& vertex_data_entry : vertex_data_entries_){
			vertex_data_entry.reset();
		}

		submit_groups_.front().reset({});
		current_group = submit_groups_.data();
	}

	void push_instr(std::span<const std::byte> instr){
		check_size(instr.size());
		const auto& instr_head = *instruction::start_lifetime_as<instruction_head>(instr.data());
		assert(std::to_underlying(instr_head.type) < std::to_underlying(instruction::instr_type::SIZE));


		if(instr_head.type == instr_type::uniform_update){
			if(instr_head.payload.ubo.group_index){
				// const auto& entry = user_non_vertex_data_entries_[instr_head.payload.ubo.global_index];
			}else{
				const auto& entry = user_vertex_data_entries_[instr_head.payload.ubo.global_index];
				vertex_data_entries_[instr_head.payload.ubo.global_index].push(get_payload_data_span(instr.data()));
			}

			return;
		}

		gpu_vertex_data_advance_data flags{};
		for (auto&& [idx, vertex_data_entry] : vertex_data_entries_ | std::views::enumerate){
			if(vertex_data_entry.collapse()){
				const instruction_head instruction_head{
					.type = instr_type::uniform_update,
					.size = sizeof(instruction::instruction_head),
					.payload = {.marching_data = {static_cast<std::uint32_t>(idx)}}
				};
				try_push_(instruction_head, instr.data());
			}
		}

		try_push_(instr_head, instr.data());
	}

	std::size_t get_current_submit_group_index() const noexcept{
		assert(current_group != nullptr);
		return current_group - submit_groups_.data();
	}

	void load_to_gpu(){
		assert(current_group != nullptr);
		assert(!submit_groups_.empty());
		const auto submit_group_subrange = std::span{submit_groups_.data(), current_group + 1};

		std::uint32_t totalDispatchCount{};
		{
			//setup dispatch config buffer;
			VkDeviceSize deviceSize{};
			for (const auto & group_subrange : submit_group_subrange){
				deviceSize += group_subrange.get_used_dispatch_groups().size_bytes();
			}

			if(dispatch_group_config_buffer_.get_size() < deviceSize){
				dispatch_group_config_buffer_ = vk::buffer{
					allocator_, {
						.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
						.size = deviceSize,
						.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
					}, {.usage = VMA_MEMORY_USAGE_CPU_TO_GPU}
				};
			}

			vk::buffer_mapper mapper{vertex_data_buffer_};
			VkDeviceSize cur_off{};
			for (const auto & [idx, reference] : submit_group_subrange | std::views::enumerate){
				const auto range = reference.get_used_dispatch_groups();
				mapper.load_range(range, cur_off);
				cur_off += range.size_bytes();
				totalDispatchCount += range.size();
			}
		}

		{
			//reserve vertex transform data
			//TODO add align?
			{
				VkDeviceSize required_size{};
				for (const auto& entry : vertex_data_entries_){
					required_size += entry.get_required_byte_size();
				}
				if(vertex_data_buffer_.get_size() < required_size){
					vertex_data_buffer_ = vk::buffer{
						allocator_, {
							.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
							.size = required_size,
							.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
						}, {.usage = VMA_MEMORY_USAGE_CPU_TO_GPU}
					};
				}
			}

			vk::buffer_mapper mapper{vertex_data_buffer_};
			VkDeviceSize cur_offset{};
			for (const auto& entry : vertex_data_entries_){
				mapper.load_range(entry.get_data_span(), cur_offset);
				cur_offset += entry.get_required_byte_size();
			}
		}

		// if(descriptor_buffer_.get_chunk_count() < submit_group_subrange.size()){
		// 	descriptor_buffer_ = vk::descriptor_buffer(
		// 		allocator_,
		// 		descriptor_layout_, descriptor_layout_.binding_count(),
		// 		submit_group_subrange.size()
		// 	);
		// }

		//update buffers
		{
			struct submit_group_meta_data{
				std::uint32_t instruction_begin{};
				std::uint32_t _align;
			};

			const std::size_t vtx_data_group_size = user_vertex_data_entries_.size();
			std::vector<std::uint32_t> initial_vertex_data_offsets(vtx_data_group_size * submit_group_subrange.size());

			std::vector<submit_group_meta_data> meta_info(submit_group_subrange.size() + 1);
			std::uint32_t instructionCurOff{};
			for (const auto & [idx, submit_group] : submit_group_subrange | std::views::enumerate){
				meta_info[idx].instruction_begin = instructionCurOff;
				instructionCurOff += submit_group.get_used_size();
			}
			meta_info.back().instruction_begin = instructionCurOff;

			// const auto
			const std::size_t group_meta_info_size = sizeof(submit_group_meta_data) + vtx_data_group_size * sizeof(std::uint32_t);
			const auto meta_info_total_size = submit_group_subrange.size() * group_meta_info_size;

			VkDeviceSize bufferReq = instructionCurOff + meta_info.size() * sizeof(submit_group_meta_data);
			if(gpu_instruction_buffer_.get_size() < bufferReq){
				gpu_instruction_buffer_ = vk::buffer{
					allocator_, {
						.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
						.size = bufferReq,
						.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
					}, {.usage = VMA_MEMORY_USAGE_CPU_TO_GPU}
				};
			}

			// for (auto && submit_group_start_addr : meta_info){
			// 	submit_group_start_addr.instruction_begin += meta_info_total_size;
			// }

			vk::buffer_mapper mapper{gpu_instruction_buffer_};

			for (const auto & [idx, submit_group] : submit_group_subrange | std::views::enumerate){
				const auto group_initial = group_meta_info_size * idx;
				(void)mapper.load(meta_info[idx], group_initial);
				(void)mapper.load_range(
					std::span{initial_vertex_data_offsets.data() + vtx_data_group_size * idx, vtx_data_group_size},
					sizeof(submit_group_meta_data) + group_initial);
				(void)mapper.load_range(std::span{submit_group.get_buffer_data(), submit_group.get_used_size()}, meta_info[idx].instruction_begin);
			}


			vk::descriptor_mapper dbo_mapper{descriptor_buffer_};
			dbo_mapper.set_storage_buffer(0, gpu_instruction_buffer_.get_address(), meta_info_total_size);
			dbo_mapper.set_storage_buffer(1, gpu_instruction_buffer_.get_address() + meta_info_total_size, instructionCurOff);
			dbo_mapper.set_storage_buffer(2,
				dispatch_group_config_buffer_.get_address() + meta_info_total_size,
				submit_group_subrange.size() * get_mesh_dispatch_limit() * sizeof(dispatch_group_info));

			VkDeviceSize cur_offset{};
			for (const auto& [i, entry] : vertex_data_entries_ | std::views::enumerate){
				dbo_mapper.set_storage_buffer(3 + i, vertex_data_buffer_.get_address() + cur_offset, entry.get_required_byte_size());
				cur_offset += entry.get_required_byte_size();
			}
		}

		vk::buffer_mapper{indirect_buffer_}.load(VkDrawMeshTasksIndirectCommandEXT{
			.groupCountX = totalDispatchCount,
			.groupCountY = 1,
			.groupCountZ = 1
		});
	}


	VkDescriptorSetLayout get_descriptor_set_layout() const noexcept{
		return descriptor_layout_;
	}

	void record_command(VkPipelineLayout pipelineLayout, VkCommandBuffer cmd){
		const VkDescriptorBufferBindingInfoEXT info = descriptor_buffer_;
		vk::cmd::bindDescriptorBuffersEXT(cmd, 1, &info);
		std::uint32_t Zero{};
		std::size_t Zerot{};
		vk::cmd::setDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &Zero, &Zerot);

		vk::cmd::drawMeshTasksIndirect(cmd, indirect_buffer_);
	}



private:
	void setup_current_group_ubo_timestamp_() const noexcept{
		for (auto && [idx, timestamp] : current_group->group_initial_vertex_data_timestamps_ | std::views::enumerate){
			timestamp = vertex_data_entries_[idx].get_current_index();
		}
	}

	void set_batch_vertex_data_entries_(){
		vertex_data_entries_.resize(user_vertex_data_entries_.size());
		for (auto&& [idx, data_entry] : user_vertex_data_entries_ | std::views::enumerate){
			vertex_data_entries_[idx].unit_size = data_entry.entry.size;
		}
	}


	bool try_push_(const instruction_head& instr_head, const std::byte* instr){
		while(!current_group->push(instr_head, instr)){
			auto last_param = current_group->get_extend_able_params();
			if(current_group == std::to_address(submit_groups_.rbegin())){
				current_group = &submit_groups_.emplace_back(user_vertex_data_entries_.size(), get_mesh_dispatch_limit());
			}else{
				++current_group;
			}

			setup_current_group_ubo_timestamp_();
			current_group->reset(last_param);
		}
		return false;
	}

	bool try_push_(const instruction_head& instr_head){
		return try_push_(instr_head, reinterpret_cast<const std::byte*>(std::addressof(instr_head)));
	}
};
}