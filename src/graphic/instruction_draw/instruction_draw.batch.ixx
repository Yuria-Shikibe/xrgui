module;

#include <vulkan/vulkan.h>
#include <mo_yanxi/adapted_attributes.hpp>
#include "gch/small_vector.hpp"

export module mo_yanxi.graphic.draw.instruction.batch;

import mo_yanxi.vk;

import mo_yanxi.graphic.draw.instruction;

import mo_yanxi.type_register;
import std;

//TODO remove the dependency of context?
template <typename T>
FORCE_INLINE void insert_every_n_inplace(T& vec, std::size_t stride, std::size_t count, const std::ranges::range_value_t<T>& value = {}) {
	if (stride == 0) {
		return;
	}

	const auto original_size = std::ranges::size(vec);
	if (stride > original_size) {
		return;
	}

	const auto insertions_count = original_size / stride;

	vec.reserve(original_size + insertions_count * count);

	auto it = std::ranges::begin(vec);
	for (std::size_t i = 0; i < insertions_count; ++i) {
		std::ranges::advance(it, stride);
		it = vec.insert(it, count, value);
		std::ranges::advance(it, count);
	}
}

namespace mo_yanxi::graphic::draw::instruction{

#pragma region mesh_group_dispatch
struct dispatch_result{
	std::byte* next;
	std::uint32_t count;
	std::uint32_t next_primit_offset;
	std::uint32_t next_vertex_offset;
	instr_type next_type;
	bool img_requires_update;

	VkImageView current_img{};

	[[nodiscard]] bool contains_next() const noexcept{
		return next_vertex_offset != 0;
	}

	[[nodiscard]] bool no_next(std::byte* original) const noexcept{
		return next == original && count == 0 && next_type == instr_type::noop && !img_requires_update;
	}
};

struct alignas(16) dispatch_group_info{
	std::uint32_t instruction_offset; //offset in 16 Byte TODO remove the scaling?
	std::uint32_t vertex_offset;
	std::uint32_t primitive_offset;
	std::uint32_t primitive_count;
};

constexpr inline std::uint32_t MaxTaskDispatchPerTime = 32;
constexpr inline std::uint32_t MaxVerticesPerMesh = 64;

/**
 *
 * @brief parse given instructions between [begin, sentinel), store mesh dispatch info to storage
 */
FORCE_INLINE inline dispatch_result get_dispatch_info(
	std::byte* const instr_begin,
	const std::byte* const instr_sentinel,
	std::span<dispatch_group_info> storage,
	image_view_history& image_cache,
	const std::uint32_t initial_primitive_offset,
	const std::uint32_t initial_vertex_offset
) noexcept{
	CHECKED_ASSUME(!storage.empty());
	CHECKED_ASSUME(instr_sentinel >= instr_begin);

	std::uint32_t currentMeshCount{};
	const auto ptr_to_src = std::assume_aligned<16>(instr_begin);

	std::uint32_t verticesBreakpoint{initial_vertex_offset};
	std::uint32_t nextPrimitiveOffset{initial_primitive_offset};
	VkImageView current_img{};

	auto ptr_to_head = std::assume_aligned<16>(instr_begin);

	bool requires_image_try_push{true};

	while(currentMeshCount < storage.size()){
		storage[currentMeshCount].primitive_offset = nextPrimitiveOffset;
		storage[currentMeshCount].vertex_offset = verticesBreakpoint > 2
			                                          ? (verticesBreakpoint - 2)
			                                          : (verticesBreakpoint = 0);

		std::uint32_t pushedVertices{};
		std::uint32_t pushedPrimitives{};

		const auto ptr_to_chunk_head = ptr_to_head;

		const auto get_remain_vertices = [&] FORCE_INLINE{
			return MaxVerticesPerMesh - pushedVertices;
		};

		const auto save_chunk_head_and_incr = [&] FORCE_INLINE{
			if(pushedPrimitives == 0) return;
			assert((ptr_to_chunk_head - ptr_to_src) % 16 == 0);
			storage[currentMeshCount].instruction_offset = ptr_to_chunk_head - ptr_to_src;
			storage[currentMeshCount].primitive_count = pushedPrimitives;
			++currentMeshCount;
		};

		while(true){
			const auto& head = *start_lifetime_as<instruction_head>(ptr_to_head);
			assert(std::to_underlying(head.type) < std::to_underlying(instruction::instr_type::SIZE));

			if(ptr_to_head + head.get_instr_byte_size() > instr_sentinel){
				save_chunk_head_and_incr();
				return {
						ptr_to_head, currentMeshCount, nextPrimitiveOffset, verticesBreakpoint, head.type, false,
						current_img
					};
			}

			switch(head.type){
			case instr_type::noop :
			case instr_type::uniform_update :
				save_chunk_head_and_incr();
				return {ptr_to_head, currentMeshCount, nextPrimitiveOffset, verticesBreakpoint, head.type, false};
			default : break;
			}

			if(requires_image_try_push){
				requires_image_try_push = false;
				if(auto rst = set_image_index(ptr_to_head + sizeof(instruction_head), image_cache); !rst){
					save_chunk_head_and_incr();
					return {
							ptr_to_head, currentMeshCount, nextPrimitiveOffset, verticesBreakpoint, head.type, true,
							rst.image
						};
				} else{
					current_img = rst.image;
				}
			}

			auto nextVertices = get_vertex_count(head.type, ptr_to_head);

			if(verticesBreakpoint){
				assert(verticesBreakpoint >= 3);
				assert(verticesBreakpoint < nextVertices);
				nextVertices -= (verticesBreakpoint -= 2); //make sure a complete primitive is draw
			}


			if(pushedVertices + nextVertices <= MaxVerticesPerMesh){
				pushedVertices += nextVertices;
				pushedPrimitives += get_primitive_count(head.type, ptr_to_head, nextVertices);

				verticesBreakpoint = 0;
				nextPrimitiveOffset = 0;

				// last_instr = ptr_to_head;
				ptr_to_head = std::assume_aligned<16>(ptr_to_head + head.get_instr_byte_size());
				requires_image_try_push = true;

				if(pushedVertices == MaxVerticesPerMesh) break;
			} else{
				const auto remains = get_remain_vertices();
				const auto primits = get_primitive_count(head.type, ptr_to_head, remains);
				nextPrimitiveOffset += primits;
				pushedPrimitives += primits;

				verticesBreakpoint += remains;

				break;
			}
		}

		save_chunk_head_and_incr();
	}

	return {ptr_to_head, currentMeshCount, nextPrimitiveOffset, verticesBreakpoint, instr_type::noop, false, current_img};
}
#pragma endregion

constexpr inline std::size_t maximum_images = 4;
constexpr inline std::size_t max_dispatch_per_workgroup = 32;
constexpr inline std::size_t working_group_count = 4;

struct dispatch_config{
	std::uint32_t shared_instr_image_index_override;
	std::uint32_t _cap[3];
	std::array<dispatch_group_info, max_dispatch_per_workgroup> group_info;
};

export
struct batch_command_slots{
private:
	std::array<vk::command_buffer, working_group_count> groups{};

public:
	[[nodiscard]] batch_command_slots() = default;

	[[nodiscard]] explicit batch_command_slots(
		const vk::command_pool& pool
	){
		for(auto&& group : groups){
			group = pool.obtain();
		}
	}

	VkCommandBuffer operator[](std::uint32_t idx) const noexcept{
		return groups[idx];
	}

	auto begin() const noexcept{
		return groups.begin();
	}

	auto end() const noexcept{
		return groups.end();
	}
};

export
struct batch_descriptor_slots{
private:

	vk::descriptor_layout user_descriptor_layout_{};
	vk::descriptor_buffer user_descriptor_buffer_{};

public:
	[[nodiscard]] batch_descriptor_slots() = default;

	[[nodiscard]] batch_descriptor_slots(
		vk::allocator_usage allocator,
		const vk::descriptor_layout_builder& desc_layout_builder,
		std::uint32_t chunk_size
	)
		:
		user_descriptor_layout_(allocator.get_device(), VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT, desc_layout_builder),
		user_descriptor_buffer_{
			allocator, user_descriptor_layout_, user_descriptor_layout_.binding_count(),
			chunk_size
		}{
	}

	[[nodiscard]] batch_descriptor_slots(
		vk::allocator_usage allocator,
		std::regular_invocable<vk::descriptor_layout_builder&> auto desc_layout_builder,
		std::uint32_t chunk_size
	)
		: batch_descriptor_slots{allocator, [&]{
			vk::descriptor_layout_builder bd{};
			std::invoke(desc_layout_builder, bd);
			return bd;
		}(), chunk_size}{
	}

	const vk::descriptor_buffer& dbo() const noexcept{
		return user_descriptor_buffer_;
	}

	void bind(std::invocable<std::uint32_t, const vk::descriptor_mapper&> auto group_binder){
		vk::descriptor_mapper m{user_descriptor_buffer_};

		for(std::uint32_t i = 0; i < user_descriptor_buffer_.get_chunk_count(); ++i){
			std::invoke(group_binder, i, m);
		}
	}

	vk::descriptor_mapper get_mapper() noexcept{
		return vk::descriptor_mapper{user_descriptor_buffer_};
	}

	[[nodiscard]] VkDescriptorSetLayout descriptor_set_layout() const noexcept{
		return user_descriptor_layout_;
	}
};

export
struct batch;

struct working_group{
	constexpr static std::array semaphore_stages{
		VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT,
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
		// VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
	};
	friend batch;
private:
	image_view_history image_view_history{};

	std::byte* span_end{};

	std::uint64_t current_signal_index{};


	std::array<vk::semaphore, semaphore_stages.size()> semaphores{};

	void set_sentinel(std::byte* instr_span_end) noexcept{
		span_end = instr_span_end;
	}

public:
	[[nodiscard]] std::uint64_t get_current_signal_index() const noexcept{
		return current_signal_index;
	}
	//
	// [[nodiscard]] VkSemaphore get_attc_semaphore() const noexcept{
	// 	return attc_semaphore;
	// }
};

export
struct descriptor_buffer_usage{
	VkDescriptorBufferBindingInfoEXT info;
	VkDeviceSize chunk_size;
	std::uint32_t target_set;
};

export
template <typename Alloc = std::allocator<descriptor_buffer_usage>>
struct record_context{
private:
	std::vector<descriptor_buffer_usage, typename std::allocator_traits<Alloc>::template rebind_alloc<descriptor_buffer_usage>> binding_infos;
	std::vector<std::uint32_t, typename std::allocator_traits<Alloc>::template rebind_alloc<std::uint32_t>> designators{};
	std::vector<VkDeviceSize, typename std::allocator_traits<Alloc>::template rebind_alloc<VkDeviceSize>> tempOffsets{};
	std::vector<VkDescriptorBufferBindingInfoEXT, typename std::allocator_traits<Alloc>::template rebind_alloc<VkDescriptorBufferBindingInfoEXT>> tempBindInfos{};

public:
	[[nodiscard]] explicit record_context(
		const Alloc& alloc = {})
	: binding_infos(alloc)
	, designators(alloc)
	, tempOffsets(alloc)
	, tempBindInfos(alloc){}

	auto& get_bindings() noexcept{
		return binding_infos;
	}

	void prepare_bindings(){
		std::ranges::sort(binding_infos, {}, &descriptor_buffer_usage::target_set);
		tempBindInfos.assign_range(binding_infos | std::views::transform(&descriptor_buffer_usage::info));
	}

	void operator()(VkPipelineLayout layout, const VkCommandBuffer buf, std::uint32_t index, VkPipelineBindPoint bindingPoint){
		static constexpr auto is_consecutive = [
			](const descriptor_buffer_usage& left, const descriptor_buffer_usage& right){
			assert(right.target_set != left.target_set);
			return right.target_set == left.target_set + 1;
		};

		vk::cmd::bindDescriptorBuffersEXT(buf, std::ranges::size(tempBindInfos), std::ranges::data(tempBindInfos));

		std::uint32_t current_src{};
		for(auto&& chunk : binding_infos | std::views::chunk_by(is_consecutive)){
			const std::uint32_t chunk_size = std::ranges::size(chunk);
			designators.assign_range(std::views::iota(current_src, current_src + chunk_size));
			tempOffsets.assign_range(chunk | std::views::transform([&](const descriptor_buffer_usage& info){
				return info.chunk_size * index;
			}));

			vk::cmd::setDescriptorBufferOffsetsEXT(
				buf, bindingPoint, layout,
				chunk.front().target_set,
				chunk_size,
				std::ranges::data(designators), std::ranges::data(tempOffsets));

			current_src += chunk_size;
		}
	}
};


struct batch{
	using work_group = working_group;
	static constexpr std::uint32_t batch_work_group_count = working_group_count;

private:
	vk::descriptor_layout descriptor_layout_{};
	vk::descriptor_buffer descriptor_buffer_{};

	dispatch_config temp_dispatch_info_{};
	std::array<working_group, working_group_count> groups_{};
	std::uint32_t current_idle_group_index_{};
	std::uint32_t current_dspt_group_index_ = groups_.size();

	using ubo_table_type = user_data_index_table<>;

	ubo_table_type ubo_table_;
	std::vector<std::byte> ubo_cache_{};
	gch::small_vector<std::size_t, (working_group_count + 1) * 6> ubo_timeline_mark_{};
	gch::small_vector<user_data_entry> ubo_update_data_cache_;

	instruction::instruction_buffer instruction_buffer_{};
	std::byte* instruction_idle_ptr_{};
	std::byte* instruction_pend_ptr_{};
	std::byte* instruction_dspt_ptr_{};
	std::uint32_t last_primit_offset_{};
	std::uint32_t last_vertex_offset_{};
	std::uint32_t last_shared_instr_size_{};

	vk::uniform_buffer dispatch_info_buffer_{};

	//TODO merge these two?
	vk::buffer_cpu_to_gpu indirect_buffer{};
	vk::buffer_cpu_to_gpu instruction_gpu_buffer{};

	VkSampler sampler_{};

public:


	struct command_acquire_config{
		user_data_entries data_entries;
		std::span<const unsigned> group_indices;
		std::span<const VkSemaphoreSubmitInfo> group_semaphores;
	};


private:
	//TODO replace this with a fptr and a host ptr?
	/**
	 *
	 * @brief
	 * (batch group index, ubo data update) -> Buffer to execute
	 */
	std::move_only_function<void(const command_acquire_config&)> on_submit{};

public:

	[[nodiscard]] batch() = default;

	[[nodiscard]] batch(
		vk::allocator_usage allocator,
		const VkSampler sampler,
		ubo_table_type ubo_index_table,
		const std::size_t instruction_buffer_size = 1U << 13U
	)
		:
		descriptor_layout_{
			allocator.get_device(), VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
			[](vk::descriptor_layout_builder& builder){
				builder.push_seq(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT); // vertices info
				builder.push_seq(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT); // task barriers
				builder.push_seq(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT,
				                 maximum_images);
			}
		}
		, descriptor_buffer_(allocator, descriptor_layout_, descriptor_layout_.binding_count(),
		                     working_group_count)
		, ubo_table_(std::move(ubo_index_table))
		, ubo_cache_(ubo_table_.required_capacity())
		, ubo_timeline_mark_(ubo_table_.size() * (1 + groups_.size()))
		, instruction_buffer_((instruction_buffer_size == 0 ? 1 << 13 : instruction_buffer_size) + sizeof(instruction_head))
		, dispatch_info_buffer_(
			allocator,
			sizeof(dispatch_config) * working_group_count,
				VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
		, indirect_buffer(vk::buffer_cpu_to_gpu(
			allocator,
			sizeof(VkDrawMeshTasksIndirectCommandEXT) *
			working_group_count,
			VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT))
		, instruction_gpu_buffer(vk::buffer_cpu_to_gpu(
			allocator,
			instruction_buffer_.size(),
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT))
		, sampler_(sampler)
	{
		instruction_idle_ptr_ = instruction_buffer_.begin();
		instruction_dspt_ptr_ = instruction_buffer_.begin();
		instruction_pend_ptr_ = instruction_buffer_.begin();

		vk::descriptor_mapper mapper{descriptor_buffer_};
		for(std::uint32_t i = 0; i < working_group_count; ++i){
			(void)mapper.set_uniform_buffer(1, dispatch_info_buffer_.get_address() + sizeof(dispatch_config) * i,
			                                sizeof(dispatch_config), i);
		}

		for(auto&& group : groups_){
			for (auto & vkSemaphore : group.semaphores){
				vkSemaphore = vk::semaphore{get_device(), 0};
			}
		}
	}

	template <std::invocable<const command_acquire_config&> T>
	void set_submit_callback(T&& fn){
		on_submit = std::forward<T>(fn);
	}

public:
	[[nodiscard]] VkDevice get_device() const noexcept{
		return descriptor_buffer_.get_device();
	}


	[[nodiscard]] vk::allocator_usage get_allocator() const noexcept{
		assert(descriptor_buffer_.get_allocator());
		return descriptor_buffer_.get_allocator();
	}

	std::byte* acquire(std::size_t instr_self_size) {
		// context().wait_on_device();
		/*
		 * TODO eagerly consume valids?
		 */
		const auto total_reserved_req = instr_self_size + sizeof(instruction_head);
		if(total_reserved_req > instruction_buffer_.size()){
			return nullptr;
		}

		while(this->check_need_block<false>(instruction_idle_ptr_, total_reserved_req)){
			if(is_all_idle()){
				consume_n(working_group_count / 2);
			}
			wait_one(false);
		}

		if(instruction_idle_ptr_ + total_reserved_req > instruction_buffer_.end()){
			if(is_all_idle()) consume_n(working_group_count / 2);
			//Reverse buffer to head
			while(this->check_need_block<true>(instruction_buffer_.begin(), total_reserved_req)){
				if(is_all_idle()){
					consume_n(working_group_count / 2);
				}
				wait_one(false);
				if(is_all_done()){
					instruction_pend_ptr_ = instruction_buffer_.begin();
					instruction_dspt_ptr_ = instruction_buffer_.begin();
					break;
				}
			}

			instruction_idle_ptr_ = instruction_buffer_.begin() + instr_self_size;
			std::memset(instruction_idle_ptr_, 0, sizeof(instruction_head));
			return instruction_buffer_.begin();
		}

		const auto next = instruction_idle_ptr_ + instr_self_size;
		std::memset(next, 0, sizeof(instruction_head));

		return std::exchange(instruction_idle_ptr_, next);
	}

	template <instruction::known_instruction T, typename... Args>
		requires (instruction::valid_consequent_argument<T, Args...>)
	void push_instruction(const T& instr, const Args&... args){
		instruction::place_instruction_at(this->acquire(instruction::get_instr_size<T, Args...>(args...)), instr, args...);

	}

	template <typename T>
		requires (!instruction::known_instruction<T>)
	void update_ubo(const T& instr){
		instruction::place_ubo_update_at(this->acquire(instruction::get_instr_size<T>()), instr, ubo_table_.index_of<T>());
	}

	[[nodiscard]] bool is_all_done() const noexcept{
		return
			instruction_idle_ptr_ == instruction_dspt_ptr_ &&
			instruction_idle_ptr_ == instruction_pend_ptr_ &&
			is_all_idle();
	}

	void consume_all(){
		consume_n(std::numeric_limits<std::uint32_t>::max());
	}

	bool consume_n(unsigned count){
		assert(count > 0);

		unsigned initial_idle_group_index = current_idle_group_index_;
		unsigned submitted_current{};
		unsigned submitted_total{};
		auto initial_idle = groups_.size() - get_pushed_group_count();
		const auto need_wait = [&](const unsigned has_dispatched_count) -> bool{
			return has_dispatched_count >= initial_idle;
		};
		while(true){
			auto& group = groups_[current_idle_group_index_];
			const auto begin = instruction_pend_ptr_;

			const auto dspcinfo = get_dispatch_info(
				instruction_pend_ptr_,
				instruction_idle_ptr_ >= instruction_pend_ptr_
					? instruction_idle_ptr_
					: instruction_buffer_.end(),

				temp_dispatch_info_.group_info,
				group.image_view_history, last_primit_offset_,
				last_vertex_offset_);
			instruction_pend_ptr_ = dspcinfo.next;
			last_primit_offset_ = dspcinfo.next_primit_offset;
			last_vertex_offset_ = dspcinfo.next_vertex_offset;

			const auto next_payload = get_payload_data_span(dspcinfo.next);
			const auto& next_head = get_instr_head(dspcinfo.next);
			const auto next_instr_type = next_head.type;


			if(dspcinfo.img_requires_update){
				//TODO better image update strategy ?
				if(!dspcinfo.count){
					group.image_view_history.clear();
					continue;
				}
			}

			auto submit_cached = [&](){
				if(submitted_current){
					submit_current(submitted_current, initial_idle_group_index);
					submitted_total += submitted_current;
					submitted_current = 0;
					initial_idle_group_index = current_idle_group_index_;
					initial_idle = groups_.size() - get_pushed_group_count();

					return true;
				}
				return false;
			};
			auto update_ubo = [&](std::span<const std::byte> data){
				submit_cached();

				const auto idx = next_head.payload.ubo.global_index;

				const auto& entry = ubo_table_[idx];
				assert(entry.size == data.size_bytes());
				std::memcpy(ubo_cache_.data() + entry.global_offset, data.data(), data.size_bytes());
				++ubo_timeline_mark_[idx * (1 + groups_.size())];
			};

            // [新增 helper] 尝试连续消耗后续的 uniform_update 指令
            auto try_consume_consecutive_updates = [&]() {
                while(true) {
                    // 重新计算当前的 sentinel，因为 pend_ptr 已经前移
                    const auto current_sentinel = instruction_idle_ptr_ >= instruction_pend_ptr_
                                                ? instruction_idle_ptr_
                                                : instruction_buffer_.end();

                    // 检查是否有足够的空间读取头部
                    if(instruction_pend_ptr_ + sizeof(instruction_head) > current_sentinel) break;

                    const auto& head = get_instr_head(instruction_pend_ptr_);

                    // 如果类型不是 uniform_update，停止
                    if(head.type != instr_type::uniform_update) break;

                    const auto instr_size = head.get_instr_byte_size();

                    // 检查是否有足够的空间读取完整指令
                    if(instruction_pend_ptr_ + instr_size > current_sentinel) break;

                    // 获取 payload 并更新
                    // 注意：这里需要根据新的 ptr 获取 payload，不能复用外部的 next_head
                    // 假设 get_payload_data_span 可以直接作用于指针
                    const auto payload = get_payload_data_span(instruction_pend_ptr_);

                    // 这里的 update_ubo 需要稍微调整逻辑，因为它依赖了外部捕获的 `next_head` 中的 global_index。
                    // 原始 update_ubo 捕获了 next_head (dspcinfo.next)。
                    // 我们需要内联 update_ubo 的逻辑，或者让 update_ubo 接受 head 参数。
                    // 为保持改动最小，这里直接内联 update_ubo 的核心逻辑：

                    // --- 内联 UBO 更新逻辑开始 ---
                    // 仍然需要调用 submit_cached 确保之前的绘制命令已提交
                    submit_cached();

                    const auto idx = head.payload.ubo.global_index;
                    const auto& entry = ubo_table_[idx];
                    assert(entry.size == payload.size_bytes());
                    std::memcpy(ubo_cache_.data() + entry.global_offset, payload.data(), payload.size_bytes());
                    ++ubo_timeline_mark_[idx * (1 + groups_.size())];
                    // --- 内联 UBO 更新逻辑结束 ---

                    // 推进指针
                    instruction_pend_ptr_ += instr_size;
                }
            };


			if(dspcinfo.count){
				assert(dspcinfo.next >= begin);
				const auto instr_byte_off = begin - instruction_buffer_.begin();
				const auto instr_shared_size = (dspcinfo.contains_next()
					                                ? next_head.get_instr_byte_size()
					                                : 0);
				const auto instr_size = dspcinfo.next - begin + instr_shared_size;

				const bool is_img_history_changed = group.image_view_history.check_changed();
				if(need_wait(submitted_current)){
					//If requires wait and is not undispatched command
					wait_one(is_img_history_changed);
				}

				(void)vk::buffer_mapper{instruction_gpu_buffer}
					.load_range(
						std::span{begin + last_shared_instr_size_, dspcinfo.next + instr_shared_size},
						instr_byte_off + last_shared_instr_size_);
				//Resolved the condition when two drawcall share one instruction, so override the first instr img index is needed
				if(last_shared_instr_size_){
					auto& generic = reinterpret_cast<primitive_generic&>(*(begin + sizeof(instruction_head)));
					assert(generic.image.index < image_view_history::max_cache_count || generic.image.index == ~0U);
					temp_dispatch_info_.shared_instr_image_index_override = generic.image.index;
				} else{
					temp_dispatch_info_.shared_instr_image_index_override = image_view_history::max_cache_count;
				}
				last_shared_instr_size_ = instr_shared_size;
				if(instr_shared_size){
					//Resume next image from index to pointer to view
					auto& generic = reinterpret_cast<primitive_generic&>(*(dspcinfo.next + sizeof(instruction_head)));
					generic.image.set_view(dspcinfo.current_img);
				}

				if(is_img_history_changed){
					vk::descriptor_mapper{descriptor_buffer_}
						.set_image(2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, sampler_,
						           group.image_view_history.get(), current_idle_group_index_);
				}

				(void)vk::buffer_mapper{dispatch_info_buffer_}
					.load(static_cast<const void*>(&temp_dispatch_info_),
					      16 + sizeof(dispatch_group_info) * dspcinfo.count,
					      sizeof(dispatch_config) * current_idle_group_index_);
				(void)vk::descriptor_mapper{descriptor_buffer_}
					.set_storage_buffer(0,
					                    instruction_gpu_buffer.get_address() + instr_byte_off,
					                    instr_size,
					                    current_idle_group_index_);
				VkDrawMeshTasksIndirectCommandEXT indirectCommandExt{dspcinfo.count, 1, 1};
				(void)vk::buffer_mapper{indirect_buffer}
					.load(indirectCommandExt, sizeof(VkDrawMeshTasksIndirectCommandEXT) * current_idle_group_index_);

				++submitted_current;
				++group.current_signal_index;
				push_current_group();

				switch(dspcinfo.next_type){
				case instr_type::uniform_update :{
					update_ubo(next_payload);
					instruction_pend_ptr_ += next_head.get_instr_byte_size();

                    // [修改] 尝试继续消耗后续的 uniform_update
                    try_consume_consecutive_updates();
					break;
				}
				default : break;
				}

				group.set_sentinel(instruction_pend_ptr_);
			}else{
				auto patch_dspt_ptr = [&]{
					if(is_all_idle()){
						instruction_dspt_ptr_ = instruction_pend_ptr_;
					}else{
						get_back_group()->span_end = instruction_pend_ptr_;
					}
				};

				switch(dspcinfo.next_type){
				case instr_type::uniform_update :{
					update_ubo(next_payload);
					instruction_pend_ptr_ += next_head.get_instr_byte_size();

                    // [修改] 尝试继续消耗后续的 uniform_update
                    try_consume_consecutive_updates();

					patch_dspt_ptr();
					break;
				}
				default : break;
				}
			}

			if(next_instr_type == instr_type::noop && instruction_pend_ptr_ > instruction_idle_ptr_){
				assert(last_shared_instr_size_ == 0);
				instruction_pend_ptr_ = instruction_buffer_.begin();
				continue;
			}

			bool hasNext = !dspcinfo.no_next(begin);


			if(!hasNext || (submitted_total >= count)){
				break;
			}
		}

		if(submitted_current){
			submit_current(submitted_current, initial_idle_group_index);
			submitted_total += submitted_current;
		}

		return submitted_total < count;
	}

	void wait_one(bool wait_on_frag = true){
		if(const auto group = get_front_group()){
			group->semaphores[wait_on_frag].wait(group->current_signal_index);

			instruction_dspt_ptr_ = group->span_end;
			pop_front_group();
		}
	}

	template <std::invocable<unsigned, working_group&> Fn>
	FORCE_INLINE auto for_each_submit(Fn fn){
		if(is_all_idle()) return;
		auto cur = current_dspt_group_index_;

		do{
			if constexpr(std::predicate<Fn, working_group&>){
				if(std::invoke(fn, cur, groups_[cur])){
					return cur;
				}
			} else{
				std::invoke(fn, cur, groups_[cur]);
			}

			cur = (cur + 1) % groups_.size();
		} while(cur != current_idle_group_index_);
	}

	void wait_n(const std::uint32_t count, const bool wait_on_frag = true){
		if(!count) return;

		std::array<std::uint64_t, working_group_count> values{};
		std::array<VkSemaphore, working_group_count> semaphores{};
		std::uint32_t pushed{};
		for_each_submit([&](unsigned, const working_group& group){
			if(pushed >= count) return;
			semaphores[pushed] = group.semaphores[wait_on_frag];
			values[pushed] = group.current_signal_index;
			++pushed;
			instruction_dspt_ptr_ = group.span_end;
		});

		pop_n_group(pushed);

		vk::wait_multiple(get_device(), {semaphores.data(), pushed}, {values.data(), pushed});
	}

	void wait_all(bool wait_on_frag = true){
		wait_n(get_pushed_group_count(), wait_on_frag);
	}

	void record_command(
		const VkPipelineLayout layout,
		std::span<const descriptor_buffer_usage> bindingInfoExts,
		std::generator<VkCommandBuffer&&>&& cmdGen) const{

		std::vector<descriptor_buffer_usage> binding_infos{};


		record_context ctx{};
		ctx.get_bindings().reserve(bindingInfoExts.size() + 1);
		ctx.get_bindings().assign_range(bindingInfoExts);
		ctx.get_bindings().push_back({
				.info = descriptor_buffer_.get_bind_info(VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR | VK_BUFFER_USAGE_2_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT),
				.chunk_size = descriptor_buffer_.get_chunk_size()
			});
		ctx.prepare_bindings();
		for(auto&& [idx, buf] : std::move(cmdGen) | std::views::take(groups_.size()) | std::ranges::views::enumerate){
			ctx(layout, buf, idx, VK_PIPELINE_BIND_POINT_GRAPHICS);
			vk::cmd::drawMeshTasksIndirect(buf, indirect_buffer, idx * sizeof(VkDrawMeshTasksIndirectCommandEXT));
		}
	}

	[[nodiscard]] VkDescriptorSetLayout get_batch_descriptor_layout() const noexcept{
		return descriptor_layout_;
	}

	void reset() noexcept {
		assert(is_all_done());
		assert(is_all_idle());

		// current_idle_group_index_ = 0;
		instruction_idle_ptr_ = instruction_buffer_.begin();
		instruction_pend_ptr_ = instruction_buffer_.begin();
		instruction_dspt_ptr_ = instruction_buffer_.begin();
		instruction_buffer_.clear();
		temp_dispatch_info_ = {};
	}

	std::size_t work_group_count() const noexcept{
		return groups_.size();
	}

	void append_ubo_table(const ubo_table_type& table){
		auto last_sz = ubo_table_.size();

		ubo_table_.append(table);
		ubo_cache_.resize(ubo_table_.required_capacity());
		insert_every_n_inplace(ubo_timeline_mark_, last_sz, table.size());
	}

	[[nodiscard]] const ubo_table_type& get_ubo_table() const noexcept{
		return ubo_table_;
	}

private:
	void submit_current(
		unsigned submit_count,
		unsigned initial_group
		){
		std::array<unsigned, working_group_count> group_indices;
		for(unsigned i = 0; i < submit_count; i++){
			group_indices[i] = (initial_group + i) % groups_.size();
		}

		const std::span indices_span{group_indices.data(), submit_count};

		ubo_update_data_cache_.clear();
		const auto ubo_total_count = ubo_table_.size();
		const auto ubo_group_size = (1 + groups_.size());

		if(submit_count){
			const auto latest_group = indices_span.back();
			//TODO switch layout policy to make cache friendly?
			for(unsigned i = 0; i < ubo_total_count; ++i){
				if(ubo_timeline_mark_[i * ubo_group_size + (latest_group + 1)] < ubo_timeline_mark_[i * ubo_group_size]){
					for(const auto idx : indices_span){
						ubo_timeline_mark_[i * ubo_group_size + (idx + 1)] = ubo_timeline_mark_[i * ubo_group_size];
					}
					ubo_update_data_cache_.push_back(ubo_table_[i]);
				}
			}
		}

		//usr update
		std::array<VkSemaphoreSubmitInfo, working_group_count * work_group::semaphore_stages.size()> semaphores{};
		for(unsigned i = 0; i < submit_count; ++i){
			const auto& group = groups_[group_indices[i]];

			for(std::size_t j = 0; j < work_group::semaphore_stages.size(); ++j){
				semaphores[i * work_group::semaphore_stages.size() + j] = VkSemaphoreSubmitInfo{
					.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
					.semaphore = group.semaphores[j],
					.value = group.current_signal_index,
					.stageMask = work_group::semaphore_stages[j],
				};
			}
		}

		assert(on_submit);
		on_submit({
			.data_entries = user_data_entries{ubo_cache_.data(), std::span{ubo_update_data_cache_.data(), ubo_update_data_cache_.size()}},
			.group_indices = indices_span,
			.group_semaphores = std::span{semaphores.data(), submit_count * 2},
		});
	}

	[[nodiscard]] unsigned get_pushed_group_count() const noexcept{
		if(is_all_idle()) return 0;
		return current_dspt_group_index_ < current_idle_group_index_
			       ? current_idle_group_index_ - current_dspt_group_index_
			       : groups_.size() - (current_dspt_group_index_ - current_idle_group_index_);
	}

	[[nodiscard]] working_group* get_front_group() noexcept{
		return is_all_idle() ? nullptr : groups_.data() + current_dspt_group_index_;
	}

	[[nodiscard]] working_group* get_back_group() noexcept{
		return is_all_idle() ? nullptr : groups_.data() + ((current_idle_group_index_  + groups_.size() - 1) % groups_.size());
	}

	template <bool onReverse, typename InstrT, typename... Args>
	FORCE_INLINE bool check_need_block(const std::byte* where, const Args& ...args) const noexcept{
		if(instruction_dspt_ptr_ == where) return onReverse || !is_all_idle();

		if(instruction_dspt_ptr_ > where){
			const auto end = where + instruction::get_instr_size<InstrT, Args...>(args...) + sizeof(instruction_head);
			return end > instruction_dspt_ptr_;
		} else{
			//reversed, where next instruction will never directly collide with dispatched instructions;
			return false;
		}
	}

	template <bool onReverse>
	FORCE_INLINE bool check_need_block(const std::byte* where, std::size_t instr_required_size) const noexcept{
		if(instruction_dspt_ptr_ == where) return onReverse || !is_all_idle();

		if(instruction_dspt_ptr_ > where){
			const auto end = where + instr_required_size;
			return end > instruction_dspt_ptr_;
		} else{
			//reversed, where next instruction will never directly collide with dispatched instructions;
			return false;
		}
	}

	[[nodiscard]] bool is_all_idle() const noexcept{
		return current_dspt_group_index_ == groups_.size();
	}

	void pop_front_group() noexcept{
		current_dspt_group_index_ = (current_dspt_group_index_ + 1) % groups_.size();
		if(current_dspt_group_index_ == current_idle_group_index_){
			current_dspt_group_index_ = groups_.size();
		}
	}

	FORCE_INLINE void pop_n_group(std::uint32_t count) noexcept{
		assert(count <= groups_.size());
		current_dspt_group_index_ = (current_dspt_group_index_ + count) % groups_.size();
		if(current_dspt_group_index_ == current_idle_group_index_){
			current_dspt_group_index_ = groups_.size();
		}
	}

	[[nodiscard]] bool is_all_pending() const noexcept{
		return current_dspt_group_index_ == current_idle_group_index_;
	}

	void push_current_group() noexcept{
		if(is_all_idle()){
			current_dspt_group_index_ = current_idle_group_index_;
		}
		current_idle_group_index_ = (current_idle_group_index_ + 1) % groups_.size();
	}

	void push_group_n(unsigned n) noexcept{
		assert(n <= groups_.size() - get_pushed_group_count());
		if(is_all_idle()){
			current_dspt_group_index_ = current_idle_group_index_;
		}
		current_idle_group_index_ = (current_idle_group_index_ + n) % groups_.size();
	}
};

}
