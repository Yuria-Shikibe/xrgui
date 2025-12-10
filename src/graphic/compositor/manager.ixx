module;

#include <vulkan/vulkan.h>
#include "plf_hive.h"
#include "gch/small_vector.hpp"
#include <vk_mem_alloc.h>

export module mo_yanxi.graphic.compositor.manager;

export import mo_yanxi.graphic.compositor.resource;
export import mo_yanxi.math.vector2;

import mo_yanxi.utility;
import mo_yanxi.vk;
import mo_yanxi.vk.cmd;
import mo_yanxi.vk.util;
import std;

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
constexpr VkPipelineStageFlags2 deduce_stage(VkImageLayout layout) noexcept{
	switch(layout){
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL :[[fallthrough]];
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : return VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : return VK_PIPELINE_STAGE_2_TRANSFER_BIT;

	default : return VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	}
}

export
constexpr VkAccessFlags2 deduce_external_image_access(VkPipelineStageFlags2 stage) noexcept{
	//TODO use bit_or to merge, instead of individual test?
	switch(stage){
	case VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT : return VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	case VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT : return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	default : return VK_ACCESS_2_NONE;
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

struct manager{
private:
	plf::hive<pass_data> passes_{};
	std::vector<pass_data*> execute_sequence_{};

	plf::hive<resource_entity_external> external_resources_{};
	plf::hive<external_descriptor> external_descriptors_{};

	allocation_raii gpu_mem_{};
	VkExtent2D extent_{};

	std::vector<vk::combined_image_type<vk::aliased_image>> allocated_images_{};
	std::vector<vk::aliased_buffer> allocated_buffers_{};

public:
	[[nodiscard]] manager() = default;

	[[nodiscard]] explicit manager(const vk::allocator_usage& allocator)
		: gpu_mem_(allocator){
	}

	void resize(VkExtent2D ext){
		if(extent_.width == ext.width && extent_.height == ext.height) return;
		extent_ = ext;
	}

	[[nodiscard]] VkExtent2D get_extent() const noexcept{
		return extent_;
	}

	template <typename T, typename... Args>
	add_result<T> add_pass(Args&&... args){
		pass_data& pass = *passes_.insert(pass_data{
				std::make_unique<T>(
					mo_yanxi::front_redundant_construct<T>(gpu_mem_.get_allocator(), std::forward<Args>(args)...))
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
			stage.meta->post_init(gpu_mem_.get_allocator(), {extent_.width, extent_.height});
			stage.meta->reset_resources(gpu_mem_.get_allocator(), stage, {extent_.width, extent_.height});
		}
	}


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
		VkDevice device = gpu_mem_.get_allocator().get_device();

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
			for(const auto& range : concurrent_ranges){
				// 对齐调整
				VkDeviceSize needed_start = (candidate_offset + current.requirements.alignment - 1)
					/ current.requirements.alignment * current.requirements.alignment;

				if(needed_start + current.requirements.size <= range.first){
					// 找到缝隙了（在这个 range 之前）
					candidate_offset = needed_start;
					goto found;
				}
				// 否则跳过这个 range
				candidate_offset = std::max(candidate_offset, range.second);
			}

			// 4. 如果所有缝隙都不行，放在最后
			candidate_offset = (candidate_offset + current.requirements.alignment - 1)
				/ current.requirements.alignment * current.requirements.alignment;

		found:
			current.assigned_offset = candidate_offset;
			total_size = std::max(total_size, candidate_offset + current.requirements.size);
			assigned.push_back(&current);
		}

		// --- 第四步：使用 VMA 进行实际分配 ---

		// 1. 分配一大块物理显存
		VkMemoryRequirements final_req = {};
		final_req.size = total_size;
		final_req.memoryTypeBits = common_mem_type_bits;
		final_req.alignment = 1; // 具体的对齐已经在 offset 计算中处理了

		VmaAllocationCreateInfo alloc_create_info = {};
		alloc_create_info.usage = VMA_MEMORY_USAGE_GPU_ONLY; // 或 AUTO

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


	struct entity_state{
		VkPipelineStageFlags2 last_stage{VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT};
		VkAccessFlags2 last_access{VK_ACCESS_2_NONE};
		VkImageLayout last_layout{VK_IMAGE_LAYOUT_UNDEFINED};

		bool external_init_required{};

		[[nodiscard]] entity_state() = default;

		[[nodiscard]] explicit(false) entity_state(const resource_entity_external& ext){
			last_stage = ext.dependency.src_stage;
			last_access = ext.dependency.src_access;
			last_layout = ext.dependency.src_layout;
		}
	};

	// 在 render_graph_manager 结构体中

	void create_command(VkCommandBuffer buffer){
		// 追踪每个逻辑资源实体（resource_entity）的当前状态
		std::unordered_map<const resource_handle*, entity_state> res_states{};
		// 追踪当前 Pass 修改过的资源，防止同一个 Pass 内多次 Barrier
		std::unordered_map<const resource_handle*, bool> already_modified_mark{};

		vk::cmd::dependency_gen dependency_gen{};

		// [新增] 追踪物理内存（Allocation）的最后使用阶段，用于解决别名同步问题
		// Key: VmaAllocation (指针或句柄), Value: Last Pipeline Stage
		std::unordered_map<VmaAllocation, VkPipelineStageFlags2> allocation_barriers;

		for(const auto& stage : execute_sequence_ | ranges::views::deref){
			already_modified_mark.clear();

			auto& inout = stage.sockets();
			auto& ref = stage.used_resources_;

			// --- 阶段 1: 处理输入资源的 Barrier (Pre-Pass) ---
			for(const auto& [in_idx, data_idx] : inout.get_valid_in()){

				auto& cur_req = inout.data[data_idx];

				// 检查是否为外部导入资源，如果是，标记需要初始化
				for(const auto& res : stage.external_inputs_){
					if(res.slot == in_idx){
						auto [itr, suc] = res_states.try_emplace(ref.inputs[res.slot].get_identity(),
						                                         entity_state{*res.resource});
						if(!suc) continue;
						auto& state = itr->second;
						state.external_init_required = true;
					}
				}

				const auto rentity = ref.get_in(in_idx);
				assert(rentity != nullptr);
				const overload_narrow overloader{
						[&](const image_requirement& r, const image_entity& entity){
							const auto& req = cur_req.get<image_requirement>();

							VkImageLayout old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
							VkPipelineStageFlags2 old_stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
							VkAccessFlags2 old_access = VK_ACCESS_2_NONE;

							const auto new_layout = req.get_expected_layout();

							// [关键修改]：检查资源状态
							if(const auto itr = res_states.find(rentity.get_identity()); itr != res_states.end()){
								// 如果之前用过（追踪到了状态），继承之前的布局
								old_layout = itr->second.last_layout;
								old_stage = itr->second.last_stage;
								old_access = itr->second.last_access;
							} else{
								// [别名修正]：如果是第一次遇到这个实体（新别名），强制 UNDEFINED
								// 即使物理内存里有数据，对于这个新 Image 句柄也是无效的
								old_layout = VK_IMAGE_LAYOUT_UNDEFINED;

								// [同步修正]：检查这块物理内存之前是否被其他别名使用过
								// 如果有，我们需要一个 Execution Barrier 等待之前的操作完成
								// 这里简化处理：假设上一帧的 fence 已经处理了帧间同步，
								// 我们只关注帧内 Pass 间的内存复用。
								// 如果不想追踪 Allocation，可以在每个 Pass 开始前加一个 Global Barrier (性能略低但安全)
							}

							//TODO
							const auto next_stage = deduce_stage(new_layout);
							const auto next_access = req.get_image_access(next_stage);
							const auto aspect = r.get_aspect();

							dependency_gen.push(
								entity.handle.image, // 使用 image_handle 中的 image
								old_stage, old_access,
								next_stage, next_access,
								old_layout, new_layout,
								{
									.aspectMask = aspect,
									.baseMipLevel = 0,
									.levelCount = r.mip_level,
									.baseArrayLayer = 0,
									.layerCount = 1 // 支持 array layers
								}
							);

							// 更新状态追踪
							auto& mark = already_modified_mark[rentity.get_identity()];
							auto& state = res_states[rentity.get_identity()];
							state.last_layout = new_layout;
							state.external_init_required = false;

							if(mark){
								state.last_stage |= next_stage;
								state.last_access |= next_access;
							} else{
								state.last_stage = next_stage;
								state.last_access = next_access;
								mark = true;
							}
						},
						[&](const buffer_requirement& r, const buffer_entity& entity){
							// Buffer Barrier Logic (通常 Buffer 不需要 Layout Transition，但需要 Memory Barrier)
							// 这里应该根据 access flags 插入 barrier
						}
					};

				std::visit(overloader, rentity.overall_requirement.req, rentity.resource);
			}

			// --- 阶段 2: 处理输出资源的初始化 (Pre-Pass) ---
			// 类似于 Render Pass 的 LoadOp::Clear 或 DontCare，我们需要将输出转换为目标布局
			for(const auto& [out_idx, data_idx] : inout.get_valid_out()){
				const auto rentity = ref.get_out(out_idx);
				assert(rentity != nullptr);
				auto& cur_req = inout.data[data_idx];

				const overload_narrow overloader{
						[&](const image_requirement& r, const image_entity& entity){
							const auto& req = cur_req.get<image_requirement>();

							VkImageLayout old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
							VkPipelineStageFlags2 old_stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
							VkAccessFlags2 old_access = VK_ACCESS_2_NONE;

							// [别名修正] 输出通常意味着写入。
							// 如果这是该 Pass 独占的别名 Image，它总是从 UNDEFINED 开始。
							// 除非它是 "Load" 操作（inout slot），但那是上面 valid_in 处理的。
							// 所以对于纯 output，我们倾向于认为它是 discard 内容的。

							if(const auto itr = res_states.find(rentity.get_identity()); itr != res_states.end()){
								old_layout = itr->second.last_layout;
								old_stage = itr->second.last_stage;
								old_access = itr->second.last_access;
							} else{
								old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
							}

							const auto new_layout = req.get_expected_layout();
							const auto next_stage = deduce_stage(new_layout);
							const auto next_access = req.get_image_access(next_stage);
							const auto aspect = r.get_aspect();

							dependency_gen.push(
								entity.handle.image,
								old_stage, old_access,
								next_stage, next_access,
								old_layout, new_layout,
								{
									.aspectMask = aspect,
									.baseMipLevel = 0,
									.levelCount = r.mip_level,
									.baseArrayLayer = 0,
									.layerCount = 1
								}
							);

							auto& mark = already_modified_mark[rentity.get_identity()];
							auto& state = res_states[rentity.get_identity()];
							state.last_layout = new_layout;

							if(mark){
								state.last_stage |= next_stage;
								state.last_access |= next_access;
							} else{
								state.last_stage = next_stage;
								state.last_access = next_access;
								mark = true;
							}
						},
						[&](const buffer_requirement& r, const buffer_entity& entity){
						}
					};
				std::visit(overloader, rentity.overall_requirement.req, rentity.resource);
			}

			// [应用 Barrier]
			// 这一步解决了布局转换，但对于别名内存，可能还需要一个 Global Execution Barrier
			// 如果你的 dependency_gen 足够智能，可以自动合并，否则建议在这里手动插入一个
			// 针对内存复用的粗粒度 Barrier (防止 Write-After-Write 或 Write-After-Read Hazard)
			// {
			// 	// 简单的安全性策略：在 Pass 开始前，确保所有之前的 Compute/Graphics 任务完成
			// 	// 仅当使用了内存别名时才必须，但为了安全可以全局加
			// 	// 优化方案是只针对 dirty 的 allocation 加 barrier
			// 	VkMemoryBarrier2 memory_barrier = {
			// 			.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
			// 			.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, // 过于保守，可优化
			// 			.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
			// 			.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
			// 			.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT
			// 		};
			// 	VkDependencyInfo dep_info = {
			// 			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			// 			.memoryBarrierCount = 1,
			// 			.pMemoryBarriers = &memory_barrier
			// 		};
			// 	// 注意：这会序列化所有 Pass，这是最安全的别名迁移策略。
			// 	// 如果想优化，需要分析 LiveInterval 的重叠情况。
			// 	vkCmdPipelineBarrier2(buffer, &dep_info);
			// }

			if(!dependency_gen.empty()) dependency_gen.apply(buffer);

			// --- 阶段 3: 记录 Pass 命令 ---
			stage.meta->record_command(gpu_mem_.get_allocator(), stage, {extent_.width, extent_.height}, buffer);

			// --- 阶段 4: 状态更新与最终转换 (Post-Pass) ---
			// 恢复输出的最终格式（如果需要在 Pass 内完成 Transition）
			// ... (保持你原有的逻辑，更新 res_states) ...

			// [原有代码逻辑]
			for(const auto& [out_idx, data_idx] : inout.get_valid_in()){
				// ... 更新 state ...
				const auto rentity = ref.inputs[out_idx];
				auto& cur_req = inout.data[data_idx];
				const overload_narrow overloader{
					[&](const image_requirement& r, const image_entity& entity){
						auto layout = cur_req.get<image_requirement>().get_expected_layout_on_output();
						auto& state = res_states.at(rentity.get_identity());
						//TODO update access check
						state.last_layout = layout;
						state.last_stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
						state.last_access = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
					},
					[&](const buffer_requirement&, const buffer_entity&){
					}
				};

				std::visit(overloader, rentity.overall_requirement.req, rentity.resource);
			}

			for(const auto& [out_idx, data_idx] : inout.get_valid_out()){
				// ... 同样更新 Outputs 的 state ...
				const auto rentity = ref.outputs[out_idx];
				auto& cur_req = inout.data[data_idx];
				const overload_narrow overloader{
					[&](const image_requirement& r, const image_entity& entity){
						auto layout = cur_req.get<image_requirement>().get_expected_layout_on_output();
						auto& state = res_states.at(rentity.get_identity());
						//TODO update access check
						state.last_layout = layout;
						state.last_stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
						state.last_access = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
					},
					[&](const buffer_requirement&, const buffer_entity&){
					}
				};

				std::visit(overloader, rentity.overall_requirement.req, rentity.resource);

				// --- 阶段 5: 处理外部输出 (External Outputs) ---
				// 保持你原有的逻辑，将资源转换回外部期望的 Layout
				for(const auto& res : stage.external_outputs_){
					if(res.resource->dependency.dst_layout == VK_IMAGE_LAYOUT_UNDEFINED) continue;
					if(res.slot != out_idx) continue;

					std::visit(
						overload_narrow{
							[&](const image_requirement& r, const image_entity& entity){
								VkImageLayout old_layout{};
								VkPipelineStageFlags2 old_stage{VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT};
								VkAccessFlags2 old_access{VK_ACCESS_2_NONE};
								if(const auto itr = res_states.find(rentity.get_identity()); itr != res_states.end()){
									old_layout = itr->second.last_layout;
									old_stage = itr->second.last_stage;
									old_access = itr->second.last_access;
								}

								const auto new_layout = res.resource->dependency.dst_layout;
								const auto next_stage = value_or(res.resource->dependency.dst_stage,
																 deduce_stage(new_layout),
																 VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
								const auto next_access = value_or(res.resource->dependency.dst_access,
																  deduce_external_image_access(next_stage),
																  VK_ACCESS_2_NONE);

								const auto& req = cur_req.get<image_requirement>();
								const auto aspect = r.get_aspect();

								dependency_gen.push(
									entity.handle.image,
									old_stage,
									old_access,
									next_stage,
									next_access,
									old_layout,
									new_layout,
									{
										.aspectMask = aspect,
										.baseMipLevel = 0,
										.levelCount = r.mip_level,
										.baseArrayLayer = 0,
										.layerCount = 1
									}
								);
							},
							[&](const buffer_requirement& r, const buffer_entity& entity){
							}
						}, rentity.overall_requirement.req, rentity.resource);
				}
			}


		}

		if(!dependency_gen.empty()) dependency_gen.apply(buffer);
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
