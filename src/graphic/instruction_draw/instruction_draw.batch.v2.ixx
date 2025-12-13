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

struct vertex_data_entry{
	std::size_t unit_size{};
	std::vector<std::byte> data_{};
	bool pending{};

	void reset() noexcept{
		data_.clear();
		pending = false;
	}

	std::size_t get_current_index() noexcept{
		if(data_.empty())return 0;
		pending = false;
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


struct submit_group{
	friend batch_v2;
private:
	instruction_buffer instruction_buffer_{};
	std::vector<dispatch_group_info> dispatch_config_storage{};
	std::vector<std::uint32_t> group_initial_vertex_data_timestamps_{};

	std::span<vertex_data_entry> vertex_data_entries_{};

	std::byte* ptr_to_head{};
	std::uint32_t index_to_last_chunk_head_{};

	std::uint32_t verticesBreakpoint{};
	std::uint32_t nextPrimitiveOffset{};

	std::uint32_t currentMeshCount{};
	std::uint32_t pushedVertices{};
	std::uint32_t pushedPrimitives{};

	void setup_current_dispatch_group_info(){
		for (const auto & [i, vertex_data_entry] : vertex_data_entries_ | std::views::enumerate){
			const auto idx = vertex_data_entries_.size() * currentMeshCount + i;
			group_initial_vertex_data_timestamps_[idx] = vertex_data_entry.get_current_index();
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
		std::span<vertex_data_entry> entries
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

	vk::buffer buffer_dispatch_info_{};
	vk::buffer buffer_instruction_{};
	vk::buffer buffer_vertex_data_{};

	vk::buffer buffer_indirect_{};

	user_data_index_table<> user_vertex_data_entries_{};
	std::vector<vertex_data_entry> vertex_data_entries_{};

	image_view_history_dynamic dynamic_image_view_history_{};

	std::vector<vk::binding_spec> bindings{};
	vk::descriptor_layout descriptor_layout_{};
	vk::dynamic_descriptor_buffer descriptor_buffer_{};

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
		VkSampler sampler
	)
		: config_(config)
		, allocator_{a}
		, buffer_indirect_(allocator_, {
				.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
				.size = sizeof(VkDrawMeshTasksIndirectCommandEXT),
				.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
			}, {.usage = VMA_MEMORY_USAGE_CPU_TO_GPU})
		, user_vertex_data_entries_{vertex_data_table}
		, dynamic_image_view_history_(config.image_usable_count)
		, bindings({
			{0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
			{1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},

			{2, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER},

			{3, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
			{4, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},

		})
		, descriptor_layout_{
			a.get_device(),
			VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
			[&](vk::descriptor_layout_builder& builder){
				//dispatch info {instruction range, vtx ubo index initialize}
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
			}}
		, descriptor_buffer_(allocator_, descriptor_layout_, descriptor_layout_.binding_count(), {})
		, sampler_(sampler){
		set_batch_vertex_data_entries_();
	}

	void begin_rendering() noexcept{
		if(submit_groups_.empty()){
			submit_groups_.push_back({user_vertex_data_entries_.size(), get_mesh_dispatch_limit(), vertex_data_entries_});
		}

		for (auto && submit_group : submit_groups_){
			submit_group.instruction_buffer_.clear();
		}

		for (auto& vertex_data_entry : vertex_data_entries_){
			vertex_data_entry.reset();
		}

		submit_groups_.front().reset({});
		current_group = submit_groups_.data();
	}

	void end_rendering() noexcept{
		current_group->finalize();
		dynamic_image_view_history_.optimize_and_reset();
		const auto submit_group_subrange = std::span{submit_groups_.data(), current_group + 1};

		for (const auto & group_subrange : submit_group_subrange){
			group_subrange.resolve_image(dynamic_image_view_history_);
		}
		auto cur_size = dynamic_image_view_history_.get().size();
		if(bindings[2].count != cur_size){
			bindings[2].count = cur_size;
			descriptor_buffer_.reconfigure(descriptor_layout_, descriptor_layout_.binding_count(), bindings);
		}
	}

	void push_instr(std::span<const std::byte> instr){
		check_size(instr.size());
		const auto& instr_head = get_instr_head(instr.data());
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

		for (auto&& [idx, vertex_data_entry] : vertex_data_entries_ | std::views::enumerate){
			if(vertex_data_entry.collapse()){
				const instruction_head instruction_head{
					.type = instr_type::uniform_update,
					.size = sizeof(instruction::instruction_head),
					.payload = {.marching_data = {static_cast<std::uint32_t>(idx)}}
				};
				try_push_(instruction_head);
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

		const auto dispatch_timeline_size = vertex_data_entries_.size() * sizeof(std::uint32_t);
		const auto dispatch_unit_size = sizeof(dispatch_group_info) + dispatch_timeline_size;
		{
			//setup dispatch config buffer;
			VkDeviceSize deviceSize{};
			for (const auto & group : submit_group_subrange){
				deviceSize += group.get_used_dispatch_groups().size_bytes() + group.get_used_time_line_datas().size_bytes();
			}
			deviceSize += dispatch_unit_size; //patch sentinel instruction offset

			if(buffer_dispatch_info_.get_size() < deviceSize){
				buffer_dispatch_info_ = vk::buffer{
					allocator_, {
						.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
						.size = deviceSize,
						.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
					}, {.usage = VMA_MEMORY_USAGE_CPU_TO_GPU}
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
					std::memcpy(buffer.data() + dispatch_unit_size * i + sizeof(info), timeline.data() + i * vertex_data_entries_.size(), dispatch_timeline_size);
				}

				mapper.load_range(buffer, pushed_size);
				pushed_size += buffer.size();
				totalDispatchCount += dispatch.size();

				current_instr_offset += group.get_total_instruction_size();
			}

			//Add instruction sentinel
			mapper.load(dispatch_group_info{current_instr_offset}, pushed_size);
		}

		{
			//reserve vertex transform data
			//TODO add align?
			{
				VkDeviceSize required_size{};
				for (const auto& entry : vertex_data_entries_){
					required_size += entry.get_required_byte_size();
				}
				if(buffer_vertex_data_.get_size() < required_size){
					buffer_vertex_data_ = vk::buffer{
						allocator_, {
							.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
							.size = required_size,
							.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
						}, {.usage = VMA_MEMORY_USAGE_CPU_TO_GPU}
					};
				}
			}

			vk::buffer_mapper mapper{buffer_vertex_data_};
			VkDeviceSize cur_offset{};
			for (const auto& entry : vertex_data_entries_){
				mapper.load_range(entry.get_data_span(), cur_offset);
				cur_offset += entry.get_required_byte_size();
			}
		}

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
				buffer_instruction_ = vk::buffer{
					allocator_, {
						.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
						.size = instructionSize,
						.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
					}, {.usage = VMA_MEMORY_USAGE_CPU_TO_GPU}
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


			vk::dynamic_descriptor_mapper dbo_mapper{descriptor_buffer_};
			dbo_mapper.set_element_at(0, 0, buffer_dispatch_info_.get_address(), dispatch_unit_size * (1 + totalDispatchCount));


			dbo_mapper.set_element_at(1, 0, buffer_instruction_.get_address(), instructionSize);


			dbo_mapper.set_images_at(2, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, sampler_, dynamic_image_view_history_.get());

			VkDeviceSize cur_offset{};
			for (const auto& [i, entry] : vertex_data_entries_ | std::views::enumerate){
				dbo_mapper.set_element_at(3 + i, 0, buffer_vertex_data_.get_address() + cur_offset, entry.get_required_byte_size());
				cur_offset += entry.get_required_byte_size();
			}
		}

		vk::buffer_mapper{buffer_indirect_}.load(VkDrawMeshTasksIndirectCommandEXT{
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

		vk::cmd::drawMeshTasksIndirect(cmd, buffer_indirect_);
	}



private:
	void set_batch_vertex_data_entries_(){
		vertex_data_entries_.resize(user_vertex_data_entries_.size());
		for (auto&& [idx, data_entry] : user_vertex_data_entries_ | std::views::enumerate){
			vertex_data_entries_[idx].unit_size = data_entry.entry.size;
			vertex_data_entries_[idx].data_.reserve(data_entry.entry.size * 8);
		}
	}


	bool try_push_(const instruction_head& instr_head, const std::byte* instr){
		while(!current_group->push(instr_head, instr)){
			auto last_param = current_group->get_extend_able_params();
			if(current_group == std::to_address(submit_groups_.rbegin())){
				current_group = &submit_groups_.emplace_back(user_vertex_data_entries_.size(), get_mesh_dispatch_limit(), vertex_data_entries_);
			}else{
				++current_group;
			}

			current_group->reset(last_param);
		}
		return false;
	}

	bool try_push_(const instruction_head& instr_head){
		return try_push_(instr_head, reinterpret_cast<const std::byte*>(std::addressof(instr_head)));
	}
};
}