module;

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#if !defined(XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE) || defined(__RESHARPER__)
#include "plf_hive.h"
#include "gch/small_vector.hpp"
#endif


export module mo_yanxi.graphic.compositor.manager;

export import mo_yanxi.graphic.compositor.resource;
export import mo_yanxi.math.vector2;
import mo_yanxi.math;

import mo_yanxi.utility;
import mo_yanxi.vk;
import mo_yanxi.vk.cmd;
import mo_yanxi.vk.util;
import std;

#ifdef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
import <plf_hive.h>;
import <gch/small_vector.hpp>;
#endif

namespace mo_yanxi::graphic::compositor{
export
struct pass_data;

export
struct manager;

struct life_trace_group;

#pragma region DependencyResolve

export
struct pass_dependency{
	pass_data* id;
	inout_index src_idx;
	inout_index dst_idx;
};

struct pass_identity{
	pass_data* where{nullptr};
	pass_data* external_target{nullptr};
	slot_pair slot;

	const pass_data* operator->() const noexcept{
		return where;
	}

	bool operator==(const pass_identity& i) const noexcept{
		if(where == i.where){
			if(where == nullptr){
				return external_target == i.external_target && slot == i.slot;
			} else{
				return true;
			}
		}

		return false;
	}

	[[nodiscard]] std::string format(std::string_view endpoint_name = "Tml") const;
};


struct resource_trace{
	bool is_external_spec{};
	resource_entity resource_entity{};
	std::vector<pass_identity> passed_by{};

	[[nodiscard]] explicit resource_trace(const resource_requirement& requirement)
		: resource_entity(requirement){
	}

	[[nodiscard]] pass_identity get_head() const noexcept{
		return passed_by.front();
	}

	[[nodiscard]] inout_index source_out() const noexcept{
		return get_head().slot.out;
	}


	void set_external(const resource_entity::variant_t& res){
		assert(!is_external_spec);
		if(resource_entity.empty())resource_entity.set_resource(res);
		is_external_spec = true;
	}

	bool is_external() const noexcept{
		return is_external_spec;
	}
};

#pragma endregion

#pragma region PassImpl

export
struct local_data_entry{
	slot_pair target_slot;
	// resource_requirement req;
	bool throughout_lifetime;
};

export
struct pass_resource_reference{
	friend manager;

private:
	gch::small_vector<resource_entity, 4> inputs{};
	gch::small_vector<resource_entity, 2> outputs{};

public:
	[[nodiscard]] bool has_in(inout_index index) const noexcept{
		if(index >= inputs.size()) return false;
		return inputs[index] != nullptr;
	}

	[[nodiscard]] bool has_out(inout_index index) const noexcept{
		if(index >= outputs.size()) return false;
		return outputs[index] != nullptr;
	}

	[[nodiscard]] resource_entity get_in(inout_index index) const noexcept{
		if(index >= inputs.size()) return {};
		return inputs[index];
	}

	[[nodiscard]] resource_entity get_out(inout_index index) const noexcept{
		if(index >= outputs.size()) return {};
		return outputs[index];
	}

	void set_in(inout_index idx, const resource_entity& res){
		inputs.resize(std::max<std::size_t>(idx + 1, inputs.size()));
		inputs[idx] = res;
	}

	void set_out(inout_index idx, const resource_entity& res){
		outputs.resize(std::max<std::size_t>(idx + 1, outputs.size()));
		outputs[idx] = res;
	}

	void set_inout(inout_index idx, const resource_entity& res){
		set_in(idx, res);
		set_out(idx, res);
	}

	void set_local(const local_data_entry& entry, const resource_entity& res){
		if(entry.target_slot.has_in()){
			set_in(entry.target_slot.in, res);
		}

		if(entry.target_slot.has_out()){
			set_out(entry.target_slot.out, res);
		}
	}
};

export
struct pass_impl{
	friend pass_data;
	friend manager;

	static constexpr math::u32size2 compute_group_unit_size2{16, 16};

	constexpr static math::u32size2 get_work_group_size(math::u32size2 image_size) noexcept{
		return image_size.add(compute_group_unit_size2.copy().sub(1u, 1u)).div(compute_group_unit_size2);
	}

	[[nodiscard]] explicit pass_impl(){
	}

	virtual ~pass_impl() = default;

protected:
	virtual void post_init(const vk::allocator_usage& allocator, const math::u32size2 extent){
	}

	virtual void reset_resources(const vk::allocator_usage& allocator, const pass_data& pass,
	                             const math::u32size2 extent){
	}

	[[nodiscard]] virtual const pass_inout_connection& sockets() const noexcept = 0;

	[[nodiscard]] virtual std::string_view get_name() const noexcept{
		return {};
	}


	virtual void record_command(
		const vk::allocator_usage& allocator,
		const pass_data& pass,
		math::u32size2 extent,
		VkCommandBuffer buffer){
	}
};

struct sync_baked_data {
	struct wait_info {
		VkEvent event;
		std::vector<VkImageMemoryBarrier2> image_barriers;
		std::vector<VkBufferMemoryBarrier2> buffer_barriers;
	};

	struct signal_info {
		VkEvent event;
		VkPipelineStageFlags2 stage_mask;
	};

	// [生产者阶段] 需要在该 Pass 开始时重置的事件 (用于循环录制)
	std::vector<VkEvent> events_to_reset;

	// [消费者阶段] 在 Pass 开始前，对于紧邻依赖（上一帧是生产者），直接使用 Barrier
	std::vector<VkImageMemoryBarrier2> immediate_image_barriers_in;
	std::vector<VkBufferMemoryBarrier2> immediate_buffer_barriers_in;

	// [消费者阶段] 在 Pass 开始前，对于非紧邻依赖，等待事件
	// 注意：vkCmdWaitEvents2 需要由事件触发的 Barrier 信息
	std::vector<wait_info> waits;

	// [生产者阶段] 在 Pass 结束后，设置事件（Signal）
	// 包含触发该事件的 Stage Mask
	std::vector<signal_info> signals;

	// [生产者阶段] 在 Pass 结束后，对于外部输出或特殊需求，执行的 Post-Barrier
	std::vector<VkImageMemoryBarrier2> immediate_image_barriers_out;
	std::vector<VkBufferMemoryBarrier2> immediate_buffer_barriers_out;
};

export
struct pass_data{
	friend manager;
	friend life_trace_group;

private:
	gch::small_vector<pass_dependency> dependencies_resource_{};
	gch::small_vector<pass_data*> dependencies_executions_{};
	gch::small_vector<external_resource_usage> external_inputs_{};
	gch::small_vector<external_resource_usage> external_outputs_{};


	gch::small_vector<local_data_entry> local_data_{};

	pass_resource_reference used_resources_{};

	std::unique_ptr<pass_impl> meta{};

	sync_baked_data sync_info_{};

public:
	[[nodiscard]] explicit pass_data(std::unique_ptr<pass_impl>&& meta)
		: meta(std::move(meta)){
	}

	[[nodiscard]] std::string get_identity_name() const noexcept{
		return std::format("({:#X}){}", static_cast<std::uint16_t>(std::bit_cast<std::uintptr_t>(this)),
		                   meta->get_name());
	}

	[[nodiscard]] const pass_inout_connection& sockets() const noexcept{
		return meta->sockets();
	}

	void add_dep(const pass_dependency dep){
		dependencies_resource_.push_back(dep);
	}

	void add_dep(const std::initializer_list<pass_dependency> dep){
		dependencies_resource_.append(dep);
	}

	void add_exec_dep(pass_data* dep){
		dependencies_executions_.push_back(dep);
	}

	void add_exec_dep(const std::initializer_list<pass_data*> dep){
		dependencies_executions_.append(dep);
	}

	void add_output(const std::initializer_list<external_resource_usage> externals){
		external_outputs_.append(externals);
	}

	void add_input(const std::initializer_list<external_resource_usage> externals){
		external_inputs_.append(externals);
	}

	void add_in_out(const std::initializer_list<external_resource_usage> externals){
		external_inputs_.append(externals);
		external_outputs_.append(externals);
	}


	bool add_local(const slot_pair slot_pair, const bool throughout_lifetime = false){
		assert(!slot_pair.is_invalid());

		for(const auto& local_data : local_data_){
			if(local_data.target_slot == slot_pair){
				throw std::runtime_error("duplicated local data target slot");
			}
		}

		local_data_.push_back({
				.target_slot = slot_pair,
				.throughout_lifetime = throughout_lifetime,
			});

		return true;
	}

	resource_requirement get_local_requirement(slot_pair slot_pair) const{
		resource_requirement req{};
		if(auto r = sockets().get_in(slot_pair.in)){
			req = *r;
		}

		if(auto r = sockets().get_out(slot_pair.out)){
			if(req.empty()){
				req = *r;
			} else if(!req.promote(*r)){
				throw std::runtime_error("incompatible requirement");
			}
		}

		if(req.empty()){
			throw std::runtime_error("empty requirement");
		}

		return req;
	}


	auto get_external_inputs() const noexcept{
		return std::span{external_inputs_.data(), external_inputs_.size()};
	}

	auto get_external_outputs() const noexcept{
		return std::span{external_outputs_.data(), external_outputs_.size()};
	}

	std::span<const local_data_entry> get_locals() const noexcept{
		return std::span{local_data_};
	}

	[[nodiscard]] const pass_resource_reference& get_used_resources() const noexcept{
		return used_resources_;
	}

	// [[nodiscard]] const resource_entity& at_out(inout_index slot) const{
	// 	return used_resources_.at_out(slot);
	// }
	//
	// [[nodiscard]] const resource_entity& at_in(inout_index slot) const{
	// 	return used_resources_.at_in(slot);
	// }
};

#pragma endregion


std::string pass_identity::format(std::string_view endpoint_name) const{
	static constexpr auto fmt_slot = [](std::string_view epn, inout_index idx) -> std::string{
		return idx == no_slot ? std::string(epn) : std::format("{}", idx);
	};

	return std::format("[{}] {} [{}]", fmt_slot(endpoint_name, slot.in),
	                   std::string(where ? where->get_identity_name() : "endpoint"), fmt_slot(endpoint_name, slot.out));
}
}

template <>
struct std::hash<mo_yanxi::graphic::compositor::pass_identity>{
	static std::size_t operator()(const mo_yanxi::graphic::compositor::pass_identity& idt) noexcept{
		if(idt.where){
			auto p0 = std::bit_cast<std::uintptr_t>(idt.where);
			return std::hash<std::size_t>{}(p0);
		} else{
			auto p1 = std::bit_cast<std::size_t>(idt.external_target) ^ static_cast<std::size_t>(idt.slot.out) ^ (
				static_cast<std::size_t>(idt.slot.in) << 31);
			return std::hash<std::size_t>{}(p1);
		}
	}
};


namespace mo_yanxi::graphic::compositor{
export
constexpr VkPipelineStageFlags2 deduce_stage(VkImageLayout layout) noexcept {
	switch (layout) {
		// 渲染目标相关
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
	case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
	case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL:
		return VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
			   VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;

		// 只读/采样相关
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
	case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
	case VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL:
		// 通常涵盖所有着色器阶段，也可以细化为特定阶段
		return VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

		// 传输相关
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		return VK_PIPELINE_STAGE_2_TRANSFER_BIT;

		// 通用/存储图像
	case VK_IMAGE_LAYOUT_GENERAL:
		return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

		// 呈现相关
	case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
		return VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;

	default:
		return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	}
}

export
constexpr VkAccessFlags2 deduce_access(VkImageLayout layout, VkPipelineStageFlags2 stage) noexcept {
	switch (layout) {
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		return VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;

	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
	case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
	case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL:
		return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
			   VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
		return VK_ACCESS_2_SHADER_READ_BIT;

	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		return VK_ACCESS_2_TRANSFER_READ_BIT;

	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		return VK_ACCESS_2_TRANSFER_WRITE_BIT;

	case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
		return VK_ACCESS_2_NONE;

	case VK_IMAGE_LAYOUT_GENERAL:
		// GENERAL 布局通常用于 Storage Image 或特定硬件优化
		if (stage & (VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT)) {
			return VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
		}
		return VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;

	default:
		return VK_ACCESS_2_NONE;
	}
}

#pragma region Trace


struct local_data_trace : local_data_entry{
	pass_data* where;
};

struct life_trace_group{
private:
	std::vector<resource_trace> bounds{};

	std::vector<local_data_trace> locals{};

public:
	[[nodiscard]] life_trace_group() = default;

	template <std::ranges::input_range T>
		requires (std::convertible_to<std::ranges::range_reference_t<T&&>, const pass_data&>)
	[[nodiscard]] life_trace_group(T&& pass_execute_sequence){
		//patch from terminal to source inputs

		for(pass_data& pass : pass_execute_sequence){
			auto& inout = pass.sockets();
			auto inout_indices = inout.get_inout_indices();

			for(const auto& entry : pass.external_outputs_){
				auto& rst = bounds.emplace_back(inout.at_out(entry.slot));
				rst.set_external(entry.resource->resource);

				rst.passed_by.push_back({nullptr, &pass, {entry.slot, no_slot}});
				auto& next = rst.passed_by.emplace_back(&pass, nullptr, slot_pair{no_slot, entry.slot});

				if(auto itr = std::ranges::find(inout_indices, entry.slot, &slot_pair::out);
					itr != inout_indices.end()){
					next.slot.in = itr->in;
				}
			}

			for(std::size_t i = 0; i < inout.input_slots.size(); ++i){
				//filter pure inputs slots
				if(std::ranges::contains(inout_indices, i, &slot_pair::in)) continue;
				if(std::ranges::contains(pass.local_data_, i, [](const local_data_entry& e){
					return e.target_slot.in;
				}))continue;

				auto& rst = bounds.emplace_back(inout.at_in(i));
				auto& cur = rst.passed_by.emplace_back(&pass);
				cur.slot.in = i;
			}
		}

		auto is_done = [](const resource_trace& b){
			return b.passed_by.back().slot.in == no_slot;
		};

		for(auto&& res_bnd : bounds){
			if(is_done(res_bnd)) continue;

			auto cur_head = res_bnd.passed_by.back();
			while(true){
				for(const auto& dependency : cur_head->dependencies_resource_){
					if(dependency.dst_idx == cur_head.slot.in){
						auto& pass = *dependency.id;
						auto& inout = pass.sockets();
						auto indices = inout.get_inout_indices();

						auto& o = inout.at_out(dependency.src_idx);
						if(!res_bnd.resource_entity.overall_requirement.promote(o)){
							throw std::runtime_error("resource not compatible");
						}

						auto& cur = res_bnd.passed_by.emplace_back(&pass);
						cur.slot.out = dependency.src_idx;

						if(auto itr = std::ranges::find(indices, dependency.src_idx, &slot_pair::out);
							itr != indices.end()){
							cur.slot.in = itr->in;
						}

						break;
					}
				}

				if(cur_head == res_bnd.passed_by.back()){
					break;
				}
				cur_head = res_bnd.passed_by.back();
			}

			if(is_done(res_bnd)) continue;

			assert(cur_head.where != nullptr);
			for(const auto& entry : cur_head.where->external_inputs_){
				if(entry.slot == cur_head.slot.in){
					res_bnd.passed_by.push_back(pass_identity{nullptr, cur_head.where, no_slot, entry.slot});
					res_bnd.set_external(entry.resource->resource);

					break;
				}
			}
		}


		for(auto& lifebound : bounds){
			std::ranges::reverse(lifebound.passed_by);
		}


		unique();

		for(pass_data& pass : pass_execute_sequence){
			locals.append_range(pass.local_data_ | std::views::transform([&](const local_data_entry& e){
				return local_data_trace{e, std::addressof(pass)};
			}));
		}
	}

	auto begin(this auto& self) noexcept{
		return std::ranges::begin(self.bounds);
	}

	auto end(this auto& self) noexcept{
		return std::ranges::end(self.bounds);
	}

	auto size() const noexcept{
		return bounds.size();
	}

	auto local_size() const noexcept{
		return locals.size();
	}

	auto get_locals() const noexcept{
		return std::span{locals};
	}

private:
	/**
	 * @brief Merge traces with the same input source
	 */
	void unique(){
		auto& range = bounds;

		std::unordered_map<pass_identity, resource_trace*> checked_outs{};
		auto itr = std::ranges::begin(range);
		auto end = std::ranges::end(range);

		while(itr != end){
			auto& trace = checked_outs[itr->get_head()];

			if(trace != nullptr){
				const auto& view = itr->passed_by;
				const auto mismatch = std::ranges::mismatch(
					trace->passed_by, view);
				for(auto& pass : std::ranges::subrange{mismatch.in2, view.end()}){
					trace->passed_by.push_back(pass);
				}

				if(!itr->resource_entity.overall_requirement.promote(trace->resource_entity.overall_requirement)){
					throw std::runtime_error("resource not compatible");
				}

				if(!trace->resource_entity.overall_requirement.promote(itr->resource_entity.overall_requirement)){
					throw std::runtime_error("resource not compatible");
				}

				itr = range.erase(itr);
				end = std::ranges::end(range);
			} else{
				if(itr->source_out() != no_slot) trace = std::to_address(itr);
				++itr;
			}
		}
	}
};

struct resource_runtime_info{
	union{
		std::nullptr_t unknown{};
		VkImage image;
		VkBuffer buffer;
	};

	VkMemoryRequirements mem_reqs{};
	VkDeviceSize offset{}; // 计算出的偏移量
};

#pragma endregion

export
template <typename T>
struct add_result{
	pass_data& pass;
	T& meta;

	[[nodiscard]] pass_data* id() const noexcept{
		return std::addressof(pass);
	}
};

struct allocation_raii{
private:
	vk::allocator_usage allocator_{};
	VmaAllocation allocation_{};

public:
	[[nodiscard]] allocation_raii() = default;

	[[nodiscard]] explicit(false) allocation_raii(const vk::allocator_usage& allocator)
		: allocator_(allocator){
	}

	void allocate(const VkMemoryRequirements& requirement,
	              const VmaAllocationCreateInfo& alloc_info){
		assert(allocator_);
		free_();
		allocation_ = allocator_.allocate_memory(requirement, alloc_info);
	}

	allocation_raii(const allocation_raii& other) = delete;

	allocation_raii(allocation_raii&& other) noexcept
		: allocator_{std::move(other.allocator_)},
		  allocation_{std::exchange(other.allocation_, {})}{
	}

	allocation_raii& operator=(const allocation_raii& other) = delete;

	allocation_raii& operator=(allocation_raii&& other) noexcept{
		if(this == &other) return *this;
		free_();
		allocator_ = std::move(other.allocator_);
		allocation_ = std::exchange(other.allocation_, {});
		return *this;
	}

	[[nodiscard]] const vk::allocator_usage& get_allocator() const{
		return allocator_;
	}

	[[nodiscard]] VmaAllocation get_allocation() const{
		return allocation_;
	}

	~allocation_raii(){
		free_();
	}

private:
	void free_() noexcept{
		if(allocation_){
			assert(allocator_);
			allocator_.deallocate(allocation_);
		}
	}
};


struct entity_state{
	VkPipelineStageFlags2 last_stage{VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT};
	VkAccessFlags2 last_access{VK_ACCESS_2_NONE};
	VkImageLayout last_layout{VK_IMAGE_LAYOUT_UNDEFINED};

	VkEvent signal_event{VK_NULL_HANDLE};

	bool external_init_required{};

	[[nodiscard]] entity_state() = default;

	[[nodiscard]] explicit(false) entity_state(const resource_entity_external& ext){
		last_stage = ext.dependency.src_stage;
		last_access = ext.dependency.src_access;
		last_layout = ext.dependency.src_layout;
		external_init_required = true;
	}
};

struct resource_sim_state {
	VkPipelineStageFlags2 last_stage;
	VkAccessFlags2 last_access;
	VkImageLayout current_layout;

	static constexpr std::size_t external = ~0;
	std::optional<std::size_t> last_write_pass_index; // external 表示外部导入，如果为空则未写入
	std::optional<std::size_t> last_read_pass_index;

	// 如果该资源在之前的某个 Pass 结束时触发了 Event，记录下来
	// Key: PassIndex, Value: EventHandle (or Index)
	// 这里的 Event 是由“上一次写入/Layout变换”的 Pass 产生的
	std::optional<std::size_t> producer_event_index;
};

struct manager{
private:

	vk::allocator allocator_major_{};
	vk::allocator allocator_frags_{};

	plf::hive<pass_data> passes_{};
	std::vector<pass_data*> execute_sequence_{};

	plf::hive<resource_entity_external> external_resources_{};
	plf::hive<external_descriptor> external_descriptors_{};

	allocation_raii gpu_mem_{};
	VkExtent2D extent_{};

	std::vector<vk::combined_image_type<vk::aliased_image>> allocated_images_{};
	std::vector<vk::aliased_buffer> allocated_buffers_{};

	vk::event_vector baked_events_{vk::allocator_usage{allocator_major_}.get_device()};

public:
	[[nodiscard]] manager() = default;

	[[nodiscard]] explicit manager(const vk::allocator_usage& allocator)
	: allocator_major_(allocator.get_context_info(), VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT | VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT)
	, allocator_frags_(allocator.get_context_info(), VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT | VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT)
	, gpu_mem_{allocator_major_}
	{
	}

	void resize(VkExtent2D ext, bool update = true){
		if(extent_.width == ext.width && extent_.height == ext.height) return;
		extent_ = ext;

		allocated_images_.clear();
		allocated_buffers_.clear();

		if(update){
			analysis_minimal_allocation();
			pass_post_init();
			analyze_dependencies_and_bake();
		}

	}

	[[nodiscard]] VkExtent2D get_extent() const noexcept{
		return extent_;
	}

	template <typename T, typename... Args>
	add_result<T> add_pass(Args&&... args){
		pass_data& pass = *passes_.insert(pass_data{
				std::make_unique<T>(
					mo_yanxi::front_redundant_construct<T>(vk::allocator_usage{allocator_frags_}, std::forward<Args>(args)...))
			});
		return add_result<T>{pass, static_cast<T&>(*pass.meta)};
	}

	[[nodiscard]] auto& add_external_descriptor(const external_descriptor& desc){
		return *external_descriptors_.insert(desc);
	}

	[[nodiscard]] auto& add_external_resource(resource_entity_external res){
		return *external_resources_.insert(std::move(res));
	}


	void pass_post_init(){
		for(auto& stage : passes_){
			stage.meta->post_init(allocator_frags_, {extent_.width, extent_.height});
			stage.meta->reset_resources(allocator_frags_, stage, {extent_.width, extent_.height});
		}
	}

private:
// Optimized synchronization analysis: Split barriers into Events where possible
	void analyze_dependencies_and_bake() {
		// 1. 重置所有 Pass 的 Sync Data
		for (auto& pass : passes_) {
			pass.sync_info_ = {};
		}

		std::size_t event_count_needed = 0;
		// 状态追踪表：记录资源的最新写入者、Stage、Access 和 Layout
		std::unordered_map<const resource_handle*, resource_sim_state> res_states{};

		// 2. 预扫描：注册外部导入资源
		// 确立外部资源的初始状态，以便后续 Pass 生成从外部状态转换的 Barrier
		for (const auto* pass_ptr : execute_sequence_) {
			for (const auto& ext_in : pass_ptr->external_inputs_) {
				if (!ext_in.resource) continue;
				if (const auto identity = resource_entity::get_identity(ext_in.resource->resource); !res_states.contains(identity)) {
					resource_sim_state state{};
					state.last_write_pass_index = resource_sim_state::external;
					state.current_layout = ext_in.resource->dependency.src_layout;
					state.last_stage = ext_in.resource->dependency.src_stage;
					state.last_access = ext_in.resource->dependency.src_access;
					state.producer_event_index = std::nullopt; // 外部资源不产生 Event
					res_states[identity] = state;
				}
			}
		}

		for (const auto* pass_ptr : execute_sequence_) {
			for (const auto& local : pass_ptr->get_locals()) {
				if (!local.throughout_lifetime) continue; // 仅处理持久化变量

				// 找到对应的 Resource Entity
				const auto& used_res = pass_ptr->used_resources_;
				resource_entity entity;
				if (local.target_slot.has_in()) entity = used_res.get_in(local.target_slot.in);
				else if (local.target_slot.has_out()) entity = used_res.get_out(local.target_slot.out);

				if (!entity) continue;
				const auto identity = entity.get_identity();

				// 如果已经被注册（例如既是 Local 又是 External Input？虽不常见），则跳过
				if (res_states.contains(identity)) continue;

				// 推断上一帧结束时的状态 (Loop State)
				// 我们查看该 Pass 的 Output Requirement。如果有 Output，则上一帧结束时处于 Output 状态。
				// 如果仅作为 Input (Read-Only History?)，则认为它保持 Input 状态。
				resource_sim_state state{};
				state.last_write_pass_index = resource_sim_state::external; // 视为外部来源，因为来自上一帧
				state.producer_event_index = std::nullopt;

				// 尝试获取 Output Requirement
				if (local.target_slot.has_out()) {
					if (auto req_opt = pass_ptr->sockets().get_out(local.target_slot.out)) {
						std::visit(overload_narrow{
							[&](const image_requirement& r) {
								state.current_layout = r.get_expected_layout_on_output();
								state.last_stage = deduce_stage(state.current_layout);
								// Access 稍显复杂，简单起见假设为 Write，或者根据 req 获取
								// 这里主要为了 Layout Transition 正确
								state.last_access = req_opt->get_access_flags(state.last_stage);

								// Fix Storage
								if (state.last_access & (VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT)) {
									if (state.current_layout != VK_IMAGE_LAYOUT_GENERAL) state.current_layout = VK_IMAGE_LAYOUT_GENERAL;
								}
							},
							[&](const buffer_requirement& r) {
								state.last_stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
								state.last_access = VK_ACCESS_2_SHADER_WRITE_BIT;
							},
							[](std::monostate){}
						}, req_opt->req);
					}
				}
				// 如果没有 Output (只读保留?)，则尝试 Input Requirement
				else if (local.target_slot.has_in()) {
					if (auto req_opt = pass_ptr->sockets().get_in(local.target_slot.in)) {
						std::visit(overload_narrow{
							[&](const image_requirement& r) {
								state.current_layout = r.get_expected_layout();
								state.last_stage = deduce_stage(state.current_layout);
								state.last_access = req_opt->get_access_flags(state.last_stage);
							},
							[&](const buffer_requirement& r) {
								state.last_stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
								state.last_access = VK_ACCESS_2_SHADER_READ_BIT;
							},
							[](std::monostate){}
						}, req_opt->req);
					}
				}

				res_states[identity] = state;
			}
		}

		// 3. 模拟执行：遍历渲染图生成同步指令
		for (auto&& [i, current_pass] : execute_sequence_ | ranges::views::deref | std::views::enumerate) {
			auto& inout_sockets = current_pass.sockets();
			auto& resources = current_pass.used_resources_;
			auto& sync = current_pass.sync_info_;

			// 记录当前 Pass 已处理过 Barrier 的资源，避免 InOut 资源重复生成 Barrier
			std::unordered_set<const resource_handle*> processed_identities;

			// --- Helper: 生成 Barrier 或 Event ---
			auto process_barrier_or_event = [&](const resource_handle* identity,
			                                    const resource_sim_state& state,
			                                    VkPipelineStageFlags2 dst_stage,
			                                    VkAccessFlags2 dst_access,
			                                    VkImageLayout target_layout,
			                                    const image_requirement* img_req,
			                                    const buffer_requirement* buf_req,
			                                    VkImage image_handle,
			                                    VkBuffer buffer_handle) {

				VkImageLayout old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
				VkPipelineStageFlags2 src_stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
				VkAccessFlags2 src_access = VK_ACCESS_2_NONE;

				// A. 确定源状态
				if (!state.last_write_pass_index.has_value()) {
					// Case A: 内部首次使用 -> 丢弃旧内容 (Undefined)
					old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
				} else if (state.last_write_pass_index == resource_sim_state::external) {
					// Case B: 外部导入 -> 保留外部内容
					old_layout = state.current_layout;
					src_stage = state.last_stage;
					src_access = state.last_access;
				} else {
					// Case C: 内部依赖 -> 继承上个 Pass 的状态
					old_layout = state.current_layout;
					src_stage = state.last_stage;
					src_access = state.last_access;
				}

				// B. 判断是否真的需要同步 (Sync Necessity Analysis)
				bool need_sync = false;

				// 检查是否为写操作
				auto is_write_access = [](VkAccessFlags2 access) {
					constexpr VkAccessFlags2 write_bits =
						VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
						VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT |
						VK_ACCESS_2_HOST_WRITE_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT |
						VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
					return (access & write_bits) != 0;
				};

				// 必须同步的情况：首次使用或外部导入
				if (!state.last_write_pass_index.has_value() ||
					state.last_write_pass_index == resource_sim_state::external) {
					need_sync = true;
				} else {
					// 内部依赖：检查 Layout 变化或内存 hazard
					bool layout_changed = false;
					if (img_req) {
						layout_changed = (state.current_layout != target_layout);
						// [关键修复] 如果源 Layout 是 Undefined，即使目标也是 Undefined，也强制 Barrier 确保正确转换
						if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED) layout_changed = true;
					}

					const bool prev_is_write = is_write_access(state.last_access);
					const bool curr_is_write = is_write_access(dst_access);
					const bool memory_hazard = prev_is_write || curr_is_write;

					if (layout_changed || memory_hazard) {
						need_sync = true;
					}
				}

				if (!need_sync) return;

				// C. 决策：使用 Split Barrier (Event) 还是 Pipeline Barrier
				bool use_event = false;

				// 重新计算 curr_is_write，确保逻辑一致

				const bool curr_is_write = is_write_access(dst_access);

				if (state.last_write_pass_index.has_value() &&
					state.last_write_pass_index != resource_sim_state::external) {
					const auto producer_idx = state.last_write_pass_index.value();

					// 基础判断：距离足够远才考虑 Event
					if (i > producer_idx + 1) {
						use_event = true;
					}

					// [修复核心]: WAR (Write-After-Read) 检查
					// 如果当前是写入操作，且存在“晚于”生产者的读取操作
					// 必须强制使用 Barrier，因为生产者的 Event 无法涵盖后续读者的完成状态
					if (curr_is_write && use_event) {
						if (state.last_read_pass_index.has_value()) {
							// 如果最后一次读取发生在生产者之后 (说明中间有 Reader Pass)
							if (state.last_read_pass_index.value() > producer_idx) {
								use_event = false;
							}
						}
					}
				}

				// D. 生成指令
				if (use_event) {
					const auto producer_idx = state.last_write_pass_index.value();
					pass_data& producer_pass = *execute_sequence_[producer_idx];

					std::size_t evt_idx = 0;
					// Hack: 利用 map 引用修改 producer event index
					auto& mutable_state = const_cast<resource_sim_state&>(state); // state 是 const引用传进来的，但在 map 里是非 const

					if (mutable_state.producer_event_index.has_value()) {
						evt_idx = mutable_state.producer_event_index.value();
					} else {
						evt_idx = event_count_needed++;
						mutable_state.producer_event_index = evt_idx;
						VkEvent placeholder_evt = std::bit_cast<VkEvent>(static_cast<std::uintptr_t>(evt_idx + 1));
						producer_pass.sync_info_.events_to_reset.push_back(placeholder_evt);
						producer_pass.sync_info_.signals.push_back({placeholder_evt, src_stage});
					}

					VkEvent placeholder_evt = std::bit_cast<VkEvent>(static_cast<std::uintptr_t>(evt_idx + 1));
					auto& wait_entry = sync.waits.emplace_back();
					wait_entry.event = placeholder_evt;

					if (img_req) {
						wait_entry.image_barriers.push_back({
							.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
							.srcStageMask = src_stage,
							.srcAccessMask = src_access,
							.dstStageMask = dst_stage,
							.dstAccessMask = dst_access,
							.oldLayout = old_layout,
							.newLayout = target_layout,
							.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
							.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
							.image = image_handle,
							.subresourceRange = {
								.aspectMask = img_req->get_aspect(),
								.baseMipLevel = 0,
								.levelCount = img_req->mip_level,
								.baseArrayLayer = 0,
								.layerCount = 1
							}
						});
					} else if (buf_req) {
						wait_entry.buffer_barriers.push_back({
							.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
							.srcStageMask = src_stage,
							.srcAccessMask = src_access,
							.dstStageMask = dst_stage,
							.dstAccessMask = dst_access,
							.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
							.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
							.buffer = buffer_handle,
							.offset = 0,
							.size = VK_WHOLE_SIZE
						});
					}
				} else {
					// === 使用 Immediate Barrier ===
					if (img_req) {
						sync.immediate_image_barriers_in.push_back({
							.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
							.srcStageMask = src_stage,
							.srcAccessMask = src_access,
							.dstStageMask = dst_stage,
							.dstAccessMask = dst_access,
							.oldLayout = old_layout,
							.newLayout = target_layout,
							.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
							.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
							.image = image_handle,
							.subresourceRange = {
								.aspectMask = img_req->get_aspect(),
								.baseMipLevel = 0,
								.levelCount = img_req->mip_level,
								.baseArrayLayer = 0,
								.layerCount = 1
							}
						});
					} else if (buf_req) {
						sync.immediate_buffer_barriers_in.push_back({
							.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
							.srcStageMask = src_stage,
							.srcAccessMask = src_access,
							.dstStageMask = dst_access,
							.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
							.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
							.buffer = buffer_handle,
							.offset = 0,
							.size = VK_WHOLE_SIZE
						});
					}
				}
			};

			// --- Phase 1: 处理 Inputs (Generate Barriers) ---
			for (const auto& [in_idx, data_idx] : inout_sockets.get_valid_in()) {
				const auto rentity = resources.get_in(in_idx);
				if (!rentity) continue;

				const auto identity = rentity.get_identity();
				processed_identities.insert(identity); // 标记已处理

				auto& req_obj = inout_sockets.data[data_idx];
				resource_sim_state current_state{};
				if (auto it = res_states.find(identity); it != res_states.end()) {
					current_state = it->second;
				}

				std::visit(overload_narrow{
					[&](const image_requirement& r, const image_entity& entity) {
						VkImageLayout target_layout = r.get_expected_layout();
						VkPipelineStageFlags2 dst_stage = deduce_stage(target_layout);
						VkAccessFlags2 dst_access = req_obj.get_access_flags(dst_stage);

						// [Fix] 如果是 Storage Image 用法，强制使用 GENERAL 布局，防止默认推导为 READ_ONLY
						if (dst_access & (VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT)) {
							if (target_layout != VK_IMAGE_LAYOUT_GENERAL) {
								target_layout = VK_IMAGE_LAYOUT_GENERAL;
							}
						}

						process_barrier_or_event(identity, current_state,
						                         dst_stage, dst_access, target_layout,
						                         &r, nullptr, entity.handle.image, VK_NULL_HANDLE);

						// 状态更新推迟到 Output 阶段处理，或者在这里预处理
						// 为了保持一致性，我们在这里仅做 Barrier，状态更新放在循环末尾统一做
						// 但我们需要记录 layout_changed 等信息给 Phase 2 使用？
						// 实际上 Phase 1 和 Phase 2 对于 InOut 资源是重叠的。
						// 如果是 Input-Only，这里更新状态。
						// 简化起见：Input 阶段也更新状态（Read-only update），Output 阶段覆盖状态（Write update）。

						// 更新只读状态 (Extending Access)
						// 注意：这会修改 res_states，影响后续 Output 阶段的判断吗？
						// Output 阶段是 "Write"，会覆盖这里的状态。所以没问题。
						// 唯一的问题是：如果 Phase 2 认为这已经是新状态了...
						// 正确的做法是：Barrier 是 "Pre-Pass"，Pass 执行完后才是 "Post-Pass State"。
						// 但为了链式依赖，我们需要知道资源进入 Pass 时是什么状态。
						// 这里我们模拟的是 "Pass 开始前" 的状态转换。
						// 所以更新 res_states 为 "Pass 执行中/后" 的状态是合理的。

						bool is_write = (dst_access & (VK_ACCESS_2_SHADER_WRITE_BIT |
							VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT));

						if(current_state.current_layout != target_layout || is_write){
							auto& state = res_states[identity];
							state.last_write_pass_index = i; // 标记所有权
							state.current_layout = target_layout;
							state.last_stage = dst_stage;
							state.last_access = dst_access;
							state.producer_event_index = std::nullopt;
							// 写入操作会覆盖之前的读取历史，不需要更新 last_read_pass_index
							// 或者可以重置它，但逻辑上只要 last_write > last_read 也是安全的
						} else{
							// RAR (只读累积)
							auto& state = res_states[identity];
							state.last_stage |= dst_stage;
							state.last_access |= dst_access;

							// [新增] 记录此 Pass进行了读取
							state.last_read_pass_index = i;
						}
					},
					[&](const buffer_requirement& r, const buffer_entity& entity) {
						VkPipelineStageFlags2 dst_stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
						VkAccessFlags2 dst_access = req_obj.get_access_flags(dst_stage);

						process_barrier_or_event(identity, current_state,
						                         dst_stage, dst_access, VK_IMAGE_LAYOUT_UNDEFINED,
						                         nullptr, &r, VK_NULL_HANDLE, entity.handle.buffer);

						auto& state = res_states[identity];
						state.last_write_pass_index = i;
						state.last_stage = dst_stage;
						state.last_access = dst_access;
						state.producer_event_index = std::nullopt;
					}
				}, req_obj.req, rentity.resource);
			}

			// --- Phase 2: 处理 Outputs (Generate Barriers for Pure Outputs & Update State) ---
			for (const auto& [out_idx, data_idx] : inout_sockets.get_valid_out()) {
				const auto rentity = resources.get_out(out_idx);
				if (!rentity) continue;
				const auto identity = rentity.get_identity();
				auto& req_obj = inout_sockets.data[data_idx];

				// [关键修复] 检查是否已经处理过 Barrier (即是否为 InOut)
				// 如果是纯输出 (Pure Output)，它不在 Input 列表中，因此尚未生成 Barrier
				if (!processed_identities.contains(identity)) {
					resource_sim_state current_state{};
					if (auto it = res_states.find(identity); it != res_states.end()) {
						current_state = it->second;
					}

					// 为纯输出生成 Barrier (例如 Undefined -> ColorAttachment)
					std::visit(overload_narrow{
						[&](const image_requirement& r, const image_entity& entity) {
							VkImageLayout target_layout = r.get_expected_layout_on_output();
							VkPipelineStageFlags2 dst_stage = deduce_stage(target_layout);
							VkAccessFlags2 dst_access = req_obj.get_access_flags(dst_stage);
							process_barrier_or_event(identity, current_state,
													 dst_stage, dst_access, target_layout,
													 &r, nullptr, entity.handle.image, VK_NULL_HANDLE);
						},
						[&](const buffer_requirement& r, const buffer_entity& entity) {
							VkPipelineStageFlags2 dst_stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
							VkAccessFlags2 dst_access = req_obj.get_access_flags(dst_stage);
							process_barrier_or_event(identity, current_state,
													 dst_stage, dst_access, VK_IMAGE_LAYOUT_UNDEFINED,
													 nullptr, &r, VK_NULL_HANDLE, entity.handle.buffer);
						}
					}, req_obj.req, rentity.resource);
				}

				// 更新最终的 Write State
				std::visit(overload_narrow{
					[&](const image_requirement& r, const image_entity& entity) {
						VkImageLayout target_layout = r.get_expected_layout_on_output();
						VkPipelineStageFlags2 dst_stage = deduce_stage(target_layout);
						VkAccessFlags2 dst_access = req_obj.get_access_flags(dst_stage);

						if (dst_access & (VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT)) {
							if (target_layout != VK_IMAGE_LAYOUT_GENERAL) target_layout = VK_IMAGE_LAYOUT_GENERAL;
						}

						auto& state = res_states[identity];
						state.last_write_pass_index = i;
						state.current_layout = target_layout;
						state.last_stage = dst_stage;
						state.last_access = dst_access;
						state.producer_event_index = std::nullopt;
					},
					[&](const buffer_requirement& r, const buffer_entity& entity) {
						auto& state = res_states[identity];
						state.last_write_pass_index = i;
						state.last_stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
						state.last_access = VK_ACCESS_2_SHADER_WRITE_BIT;
						state.producer_event_index = std::nullopt;
					}
				}, req_obj.req, rentity.resource);
			}

			// --- Phase 3: External Outputs (Transition to External Layout/Stage) ---
			for (const auto& res : current_pass.external_outputs_) {
				if (!res.resource) continue;
				if (res.resource->type() == resource_type::image &&
					res.resource->dependency.dst_layout == VK_IMAGE_LAYOUT_UNDEFINED) continue;

				const auto rentity = resources.get_out(res.slot);
				if (!rentity) continue;
				const auto identity = rentity.get_identity();

				auto& state = res_states[identity];
				// 只有当当前 Pass 是最后写入者时才转换
				if (state.last_write_pass_index != i) continue;

				std::visit(overload_narrow{
					[&](const image_requirement& r, const image_entity& entity) {
						VkImageLayout target_layout = res.resource->dependency.dst_layout;
						VkPipelineStageFlags2 dst_stage = value_or(res.resource->dependency.dst_stage,
																   deduce_stage(target_layout),
																   VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
						VkAccessFlags2 dst_access = value_or(res.resource->dependency.dst_access,
															 deduce_access(target_layout, dst_stage),
															 VK_ACCESS_2_NONE);

						sync.immediate_image_barriers_out.push_back({
							.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
							.srcStageMask = state.last_stage,
							.srcAccessMask = state.last_access,
							.dstStageMask = dst_stage,
							.dstAccessMask = dst_access,
							.oldLayout = state.current_layout,
							.newLayout = target_layout,
							.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
							.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
							.image = entity.handle.image,
							.subresourceRange = {
								.aspectMask = r.get_aspect(),
								.baseMipLevel = 0,
								.levelCount = r.mip_level,
								.baseArrayLayer = 0,
								.layerCount = 1
							}
						});
						state.current_layout = target_layout;
						state.last_stage = dst_stage;
						state.last_access = dst_access;
					},
					[&](const buffer_requirement& r, const buffer_entity& entity) {
						VkPipelineStageFlags2 dst_stage = res.resource->dependency.dst_stage;
						VkAccessFlags2 dst_access = res.resource->dependency.dst_access;

						sync.immediate_buffer_barriers_out.push_back({
							.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
							.srcStageMask = state.last_stage,
							.srcAccessMask = state.last_access,
							.dstStageMask = dst_stage,
							.dstAccessMask = dst_access,
							.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
							.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
							.buffer = entity.handle.buffer,
							.offset = 0,
							.size = VK_WHOLE_SIZE
						});
						state.last_stage = dst_stage;
						state.last_access = dst_access;
					}
				}, rentity.overall_requirement.req, rentity.resource);
			}
		}

		// 5. Finalize: 分配并填充真实的 Event Handles
		baked_events_.resize(event_count_needed);
		auto patch_events = [&](auto& event_list) {
			for (auto& evt : event_list) {
				std::uintptr_t index_val = std::bit_cast<std::uintptr_t>(evt);
				std::size_t idx = static_cast<std::size_t>(index_val) - 1;
				evt = baked_events_[idx];
			}
		};
		auto patch_signal_events = [&](auto& signal_list) {
			for (auto& sig : signal_list) {
				std::uintptr_t index_val = std::bit_cast<std::uintptr_t>(sig.event);
				std::size_t idx = static_cast<std::size_t>(index_val) - 1;
				sig.event = baked_events_[idx];
			}
		};
		auto patch_wait_events = [&](auto& wait_list) {
			for (auto& wait : wait_list) {
				std::uintptr_t index_val = std::bit_cast<std::uintptr_t>(wait.event);
				std::size_t idx = static_cast<std::size_t>(index_val) - 1;
				wait.event = baked_events_[idx];
			}
		};
		for (auto& pass : passes_) {
			patch_events(pass.sync_info_.events_to_reset);
			patch_signal_events(pass.sync_info_.signals);
			patch_wait_events(pass.sync_info_.waits);
		}
	}
	/*
	void bake_events() {

		VkDevice device = vk::allocator_usage{allocator_major_}.get_device();
		std::size_t total_events_needed = 0;

		for (auto & data : passes_){
			std::size_t sz = std::ranges::distance(data.sockets().get_valid_out());
			data.used_events_ = std::span{static_cast<VkEvent*>(nullptr), sz};
			total_events_needed += sz;
		}

		baked_events_.resize(total_events_needed, true);
		total_events_needed = 0;

		for (auto & data : passes_){
			std::size_t sz = data.used_events_.size();
			data.used_events_ = {baked_events_.data() + total_events_needed, sz};
			total_events_needed += sz;
		}
	}
	*/

public:

	void sort(){
		if(passes_.empty()){
			throw std::runtime_error("No outputs found");
		}

		struct node{
			unsigned in_degree{};
		};
		std::unordered_map<pass_data*, node> nodes;


		for(auto& v : passes_){
			nodes.try_emplace(&v);

			for(auto& dep : v.dependencies_resource_){
				++nodes[dep.id].in_degree;
			}

			for(auto& dep : v.dependencies_executions_){
				++nodes[dep].in_degree;
			}
		}

		std::vector<pass_data*> queue;
		queue.reserve(passes_.size() * 2);
		execute_sequence_.reserve(passes_.size());

		std::size_t current_index{};

		{
			//Set Initial Nodes
			for(const auto& [node, indeg] : nodes){
				if(indeg.in_degree) continue;

				queue.push_back(node);
			}
		}

		if(queue.empty()){
			throw std::runtime_error("No zero dependes found");
		}

		while(current_index != queue.size()){
			auto current = queue[current_index++];
			execute_sequence_.push_back(current);

			for(const auto& dependency : current->dependencies_resource_){
				auto& deg = nodes[dependency.id].in_degree;
				--deg;
				if(deg == 0){
					queue.push_back(dependency.id);
				}
			}
			for(const auto& dependency : current->dependencies_executions_){
				auto& deg = nodes[dependency].in_degree;
				--deg;
				if(deg == 0){
					queue.push_back(dependency);
				}
			}
		}

		if(execute_sequence_.size() != passes_.size()){
			throw std::runtime_error("RING detected in execute graph: sorted_tasks_.size() != nodes.size()");
		}

		std::ranges::reverse(execute_sequence_);
	}


	void analysis_minimal_allocation(){
		// 1. 现有的生命周期分析 (保持不变)
		auto life_bounds_ = life_trace_group{execute_sequence_ | std::views::reverse | ranges::views::deref};


		// 辅助结构：定义资源在时间轴上的生命周期 [start, end]
		struct LiveInterval{
			std::span<pass_data*> range;
			const resource_trace* trace; // 如果是 shared
			const local_data_trace* local; // 如果是 local
			std::size_t local_index;
			VkMemoryRequirements requirements;
			VkDeviceSize assigned_offset = 0;

			VkDeviceSize required_size() const noexcept{
				return requirements.size;
			}
		};
		std::vector<LiveInterval> intervals;

		// --- 第一步：创建虚句柄并获取内存需求 ---
		// 注意：这里需要 context/device，假设你能在 manager 中访问到
		VkDevice device = vk::allocator_usage{allocator_major_}.get_device();

		auto process_requirement = [&](
			const resource_requirement& req,
			LiveInterval interval){
			// 1.1 创建临时句柄 (必须加 ALIAS 标志)
			// 这一步对于获取正确的 Size 和 Alignment 至关重要
			// 代码细节：使用 vkCreateImage/Buffer 但不分配内存
			std::visit(overload{
				           [](std::monostate){
					           throw std::runtime_error("unknown resource");
				           },
				           [&](const image_requirement& r){
					           const VkImageCreateInfo info = r.get_image_create_info(extent_);

					           VkImage img;
					           vkCreateImage(device, &info, nullptr, &img);
					           vkGetImageMemoryRequirements(device, img, &interval.requirements);
					           vkDestroyImage(device, img, nullptr); // 获取完需求即可销毁
				           },
				           [&](const buffer_requirement& r){
				           }
			           }, req.req);

			intervals.push_back(interval);
		};

		for(auto&& [idx, life_bound] : life_bounds_ | std::views::enumerate){
			if(life_bound.is_external()) continue;
			auto span = get_maximum_region(life_bound.passed_by);
			process_requirement(life_bound.resource_entity.overall_requirement, {span, &life_bound, nullptr});
		}


		for(const auto& [idx, local_entry] : life_bounds_.get_locals() | std::views::enumerate){
			auto req = local_entry.where->get_local_requirement(local_entry.target_slot);
			if(local_entry.throughout_lifetime){
				process_requirement(req, {execute_sequence_, nullptr, &local_entry, static_cast<std::size_t>(idx)});
			} else{
				auto itr = std::ranges::find(execute_sequence_, local_entry.where);
				assert(itr != execute_sequence_.end());
				process_requirement(req,
				                    {
					                    std::span{std::to_address(itr), 1}, nullptr, &local_entry,
					                    static_cast<std::size_t>(idx)
				                    });
			}
		}

		// --- 第二步：内存类型分类 (Bucketing) ---
		// 只有 memoryTypeBits 兼容的资源才能放在同一个 VmaAllocation 中
		// 通常 Render Graph 中大部分资源都是 Device Local，可以归为一类
		// 这里简化假设所有资源都兼容 (你需要求所有 memoryTypeBits 的交集)
		std::uint32_t common_mem_type_bits = 0xFFFF'FFFF;
		for(auto& interval : intervals) common_mem_type_bits &= interval.requirements.memoryTypeBits;

		if(common_mem_type_bits == 0){
			throw std::runtime_error{"incompatible memory type"};
			// 错误处理：资源内存类型不兼容，需要拆分为多个 Allocation
		}

		// --- 第三步：贪心算法计算 Offset (Greedy Offset Assignment) ---
		// 按照大小降序排列，有助于减少碎片
		std::ranges::sort(intervals, std::ranges::greater{}, &LiveInterval::required_size);

		VkDeviceSize align = 1;
		VkDeviceSize total_size = 0;

		std::vector<LiveInterval*> assigned; // 已分配偏移的资源

		for(auto& current : intervals){
			VkDeviceSize candidate_offset = 0;

			// 尝试找到一个不与任何“时间上重叠”且“空间上重叠”的资源冲突的 offset
			// 这是一个简化的 First-Fit 策略

			// 1. 收集所有与其时间重叠的已分配资源
			std::vector<std::pair<VkDeviceSize, VkDeviceSize>> concurrent_ranges;
			for(const auto* other : assigned){
				// 检查时间是否重叠
				if(
					std::max(std::to_address(current.range.begin()), std::to_address(other->range.begin())) <
					std::min(std::to_address(current.range.end()), std::to_address(other->range.end()))){
					concurrent_ranges.push_back({
							other->assigned_offset, other->assigned_offset + other->requirements.size
						});
					}
			}

			// 2. 对空间区间排序
			std::ranges::sort(concurrent_ranges);

			// 3. 在缝隙中寻找位置
			for(const auto& [from, to] : concurrent_ranges){
				// 对齐调整
				VkDeviceSize needed_start =
					math::div_ceil(candidate_offset, current.requirements.alignment) * current.requirements.alignment;

				if(needed_start + current.requirements.size <= from){
					// 找到缝隙了（在这个 range 之前）
					candidate_offset = needed_start;
					goto found;
				}
				// 否则跳过这个 range
				candidate_offset = std::max(candidate_offset, to);
			}

			// 4. 如果所有缝隙都不行，放在最后
			candidate_offset = math::div_ceil(candidate_offset, current.requirements.alignment) * current.requirements.alignment;

			found:
				current.assigned_offset = candidate_offset;
			total_size = std::max(total_size, candidate_offset + current.requirements.size);
			assigned.push_back(&current);
		}

		// 1. 分配一大块物理显存
		const VkMemoryRequirements final_req{
			.size = total_size,
			.alignment = align,
			.memoryTypeBits = common_mem_type_bits
		};

		constexpr VmaAllocationCreateInfo alloc_create_info{
			.usage = VMA_MEMORY_USAGE_GPU_ONLY
		};

		gpu_mem_.allocate(final_req, alloc_create_info);

		auto create_resource = [&, this](const resource_requirement& req, VkDeviceSize offset) -> resource_entity{
			//TODO support buffer allocation
			return std::visit(overload_def_noop{
				                  std::in_place_type<resource_entity>,
				                  [&](const image_requirement& r){
					                  const auto info = r.get_image_create_info(extent_);
					                  vk::combined_image_type image{
							                  vk::aliased_image{
								                  gpu_mem_.get_allocator(), gpu_mem_.get_allocation(), offset, info
							                  },
							                  VkImageViewCreateInfo{
								                  .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
								                  .viewType = r.get_image_view_type(),
								                  .format = r.get_format(),
								                  .subresourceRange = VkImageSubresourceRange{
									                  .aspectMask = r.get_aspect(),
									                  .baseMipLevel = 0,
									                  .levelCount = info.mipLevels,
									                  .baseArrayLayer = 0,
									                  .layerCount = 1
								                  }
							                  }
						                  };


					                  const resource_entity ent{req, image_entity{image}};
					                  this->allocated_images_.push_back(std::move(image));
					                  return ent;
				                  }
			                  }, req.req);
		};

		// 2. 创建并绑定实际的别名资源
		for(const auto& interval : intervals){
			if(interval.trace){
				auto entity = create_resource(interval.trace->resource_entity.overall_requirement,
				                              interval.assigned_offset);
				for(const auto& passed_by : interval.trace->passed_by){
					if(!passed_by.where) continue;
					auto& ref = passed_by.where->used_resources_;

					if(passed_by.slot.has_in()){
						ref.set_in(passed_by.slot.in, entity);
					}

					if(passed_by.slot.has_out()){
						ref.set_out(passed_by.slot.out, entity);
					}
				}
			} else{
				auto entity = create_resource(interval.local->where->get_local_requirement(interval.local->target_slot),
				                              interval.assigned_offset);
				interval.local->where->used_resources_.set_local(*interval.local, entity);
			}
		}


		for(auto&& [idx, life_bound] : life_bounds_ | std::views::enumerate){
			if(!life_bound.is_external()) continue;
			for(const auto& passed_by : life_bound.passed_by){
				if(!passed_by.where) continue;
				auto& ref = passed_by.where->used_resources_;

				if(passed_by.slot.has_in()){
					ref.set_in(passed_by.slot.in, life_bound.resource_entity);
				}

				if(passed_by.slot.has_out()){
					ref.set_out(passed_by.slot.out, life_bound.resource_entity);
				}
			}
		}
	}

	void create_command(VkCommandBuffer buffer) {
		// 移除所有运行时状态追踪 (res_states, already_modified_mark 等)
		// 直接按照 Execute Sequence 顺序录制指令

		for (const auto* pass_ptr : execute_sequence_) {
			const auto& pass = *pass_ptr;
			const auto& sync = pass.sync_info_;

			// =========================================================
			// Phase 1: Pre-Pass Synchronization
			// =========================================================

			// 1.1 Reset Events (Producer Reset)
			// 生产者在开始执行前，重置它将在稍后 Signal 的事件。
			// 这是为了支持 CommandBuffer 的重复提交 (Re-submission)。
			if (!sync.events_to_reset.empty()) {
				for (auto event : sync.events_to_reset) {
					// 仅重置状态，不阻塞 Pipeline
					vkCmdResetEvent2(buffer, event, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
				}
			}

			// 1.2 Wait Events (Split Barriers / Long-distance dependencies)
			// 消费者等待远端生产者产生的事件
			for (const auto& wait : sync.waits) {
				const VkDependencyInfo dep_info{
					.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
					.pNext = nullptr,
					.dependencyFlags = 0,
					.memoryBarrierCount = 0,
					.pMemoryBarriers = nullptr,
					.bufferMemoryBarrierCount = static_cast<uint32_t>(wait.buffer_barriers.size()),
					.pBufferMemoryBarriers = wait.buffer_barriers.data(),
					.imageMemoryBarrierCount = static_cast<uint32_t>(wait.image_barriers.size()),
					.pImageMemoryBarriers = wait.image_barriers.data()
				};

				// 这是一个阻塞操作，GPU 会在这里等待 Event 变为 Set 状态
				// 并执行附加的 Memory Barrier (如 Layout Transition)
				vkCmdWaitEvents2(buffer, 1, &wait.event, &dep_info);
			}

			// 1.3 Immediate Barriers (In) (Adjacent dependencies / First use / External)
			// 处理紧邻的依赖或无法使用 Event 的情况
			if (!sync.immediate_image_barriers_in.empty() || !sync.immediate_buffer_barriers_in.empty()) {
				const VkDependencyInfo dep_info{
					.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
					.pNext = nullptr,
					.dependencyFlags = 0,
					.memoryBarrierCount = 0,
					.pMemoryBarriers = nullptr,
					.bufferMemoryBarrierCount = static_cast<uint32_t>(sync.immediate_buffer_barriers_in.size()),
					.pBufferMemoryBarriers = sync.immediate_buffer_barriers_in.data(),
					.imageMemoryBarrierCount = static_cast<uint32_t>(sync.immediate_image_barriers_in.size()),
					.pImageMemoryBarriers = sync.immediate_image_barriers_in.data()
				};

				vkCmdPipelineBarrier2(buffer, &dep_info);
			}

			// =========================================================
			// Phase 2: Execute Pass
			// =========================================================

			// 执行用户定义的 Pass 逻辑
			pass.meta->record_command(allocator_frags_, pass, {extent_.width, extent_.height}, buffer);

			// =========================================================
			// Phase 3: Post-Pass Synchronization
			// =========================================================

			// 3.1 Signal Events (Producer Signal)
			// 通知下游消费者，数据已准备好
			for (const auto& sig : sync.signals) {
				// vkCmdSetEvent2 需要一个 DependencyInfo 来定义触发事件的 Stage 范围
				// 我们使用一个全局 MemoryBarrier 来承载 srcStageMask
				// 具体的 Memory Availability 和 Visibility 由 Wait 侧的 Barrier 处理
				const VkMemoryBarrier2 stage_def_barrier{
					.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
					.pNext = nullptr,
					.srcStageMask = sig.stage_mask, // 关键：定义哪个阶段完成后 Signal
					.srcAccessMask = VK_ACCESS_2_NONE, // Access 由 Wait 侧定义
					.dstStageMask = VK_PIPELINE_STAGE_2_NONE,
					.dstAccessMask = VK_ACCESS_2_NONE
				};

				const VkDependencyInfo dep_info{
					.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
					.pNext = nullptr,
					.dependencyFlags = 0, // 可以根据需要添加 VK_DEPENDENCY_BY_REGION_BIT
					.memoryBarrierCount = 1,
					.pMemoryBarriers = &stage_def_barrier,
					.bufferMemoryBarrierCount = 0,
					.pBufferMemoryBarriers = nullptr,
					.imageMemoryBarrierCount = 0,
					.pImageMemoryBarriers = nullptr
				};

				vkCmdSetEvent2(buffer, sig.event, &dep_info);
			}

			// 3.2 Immediate Barriers (Out) (External Outputs)
			// 处理 Pass 结束后的强制转换（如转换回 Present 布局）
			if (!sync.immediate_image_barriers_out.empty() || !sync.immediate_buffer_barriers_out.empty()) {
				const VkDependencyInfo dep_info{
					.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
					.pNext = nullptr,
					.dependencyFlags = 0,
					.memoryBarrierCount = 0,
					.pMemoryBarriers = nullptr,
					.bufferMemoryBarrierCount = static_cast<uint32_t>(sync.immediate_buffer_barriers_out.size()),
					.pBufferMemoryBarriers = sync.immediate_buffer_barriers_out.data(),
					.imageMemoryBarrierCount = static_cast<uint32_t>(sync.immediate_image_barriers_out.size()),
					.pImageMemoryBarriers = sync.immediate_image_barriers_out.data()
				};

				vkCmdPipelineBarrier2(buffer, &dep_info);
			}
		}
	}

private:
	std::span<pass_data*> get_maximum_region(const std::span<const pass_identity> seq){
		using Itr = decltype(execute_sequence_)::iterator;
		Itr max = execute_sequence_.begin();
		Itr min = execute_sequence_.end();

		for(const auto& p : seq){
			if(!p.where) continue;

			auto itr = std::ranges::find(execute_sequence_, p.where);
			max = std::ranges::max(max, itr);
			min = std::ranges::min(min, itr);
		}

		return std::span{min, std::ranges::next(max)};
	}
};
}
