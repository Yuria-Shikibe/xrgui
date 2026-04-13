module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

#ifdef __AVX2__
#include <immintrin.h>
#endif


export module mo_yanxi.graphic.draw.instruction.batch.frontend;

export import mo_yanxi.graphic.draw.instruction.batch.common;
export import mo_yanxi.graphic.draw.instruction.general;
export import mo_yanxi.graphic.draw.instruction.state_tracker;
export import mo_yanxi.graphic.draw.instruction.util;
export import mo_yanxi.graphic.draw.instruction;
export import mo_yanxi.user_data_entry;

import mo_yanxi.type_register;
import mo_yanxi.aligned_allocator;
import std;

namespace mo_yanxi::graphic::draw::instruction{
export
struct image_view_history_dynamic{
	using handle_t = void*;
	static_assert(sizeof(handle_t) == sizeof(std::uint64_t));

	struct use_record{
		handle_t handle;
		std::uint32_t use_count;
	};

private:
	std::vector<handle_t, aligned_allocator<handle_t, 32>> images{};
	std::vector<use_record> use_count{};
	handle_t latest{};
	std::uint32_t latest_index{};
	std::uint32_t count_{};
	bool changed{};

public:
	[[nodiscard]] image_view_history_dynamic() = default;

	[[nodiscard]] explicit image_view_history_dynamic(std::uint32_t capacity){
		set_capacity(capacity);
	}

	void set_capacity(std::uint32_t new_capacity){
		images.resize(4 + (new_capacity + 3) / 4 * 4);
		use_count.resize(4 + (new_capacity + 3) / 4 * 4);
	}

	void extend(handle_t handle){
		auto lastSz = images.size();
		images.resize(images.size() + 4);
		images[lastSz] = handle;
	}

	bool check_changed() noexcept{
		return std::exchange(changed, false);
	}

	auto size() const noexcept{
		return images.size();
	}

	FORCE_INLINE void clear(this image_view_history_dynamic& self) noexcept{
		self = {};
	}

	FORCE_INLINE void reset() noexcept{
		images.clear();
		use_count.clear();
		latest = nullptr;
		latest_index = 0;
		count_ = 0;
		changed = false;
	}

	FORCE_INLINE void optimize_and_reset() noexcept{
		for(unsigned i = 0; i < images.size(); ++i){
			use_count[i].handle = images[i];
		}
		std::ranges::sort(use_count, std::ranges::greater{}, &use_record::use_count);
		unsigned i = 0;
		for(; i < images.size(); ++i){
			if(use_count[i].use_count == 0){
				break;
			}
			images[i] = use_count[i].handle;
		}
		images.erase(images.begin() + i, images.end());
		use_count.assign(images.size(), use_record{});

		latest = nullptr;
		latest_index = 0;
		count_ = 0;
		changed = false;
	}

	template <typename T>
		requires (sizeof(T) == sizeof(void*))
	[[nodiscard]] FORCE_INLINE std::span<const T> get() const noexcept{
		return {reinterpret_cast<const T*>(images.data()), count_};
	}

	[[nodiscard]] FORCE_INLINE std::uint32_t try_push(handle_t image) noexcept{
		if(!image) return std::numeric_limits<std::uint32_t>::max();
		if(image == latest) return latest_index;
#ifndef __AVX2__
		for(std::size_t idx = 0; idx < images.size(); ++idx){
			auto& cur = images[idx];
			if(image == cur){
				latest = image;
				latest_index = idx;
				++use_count[idx].use_count;
				return idx;
			}

			if(cur == nullptr){
				latest = cur = image;
				latest_index = idx;
				count_ = idx + 1;
				++use_count[idx].use_count;
				changed = true;
				return idx;
			}
		}
#else
		const __m256i target = _mm256_set1_epi64x(std::bit_cast<std::int64_t>(image));
		const __m256i zero = _mm256_setzero_si256();
		for(std::uint32_t group_idx = 0; group_idx != images.size(); group_idx += 4){
			const auto group = _mm256_load_si256(reinterpret_cast<const __m256i*>(images.data() + group_idx));
			const auto eq_mask = _mm256_cmpeq_epi64(group, target);
			if(const auto eq_bits = std::bit_cast<std::uint32_t>(_mm256_movemask_epi8(eq_mask))){
				const auto idx = group_idx + std::countr_zero(eq_bits) / 8;
				latest = image;
				latest_index = idx;
				count_ = std::max(count_, idx + 1);
				++use_count[idx].use_count;
				return idx;
			}

			const auto null_mask = _mm256_cmpeq_epi64(group, zero);
			if(const auto null_bits = std::bit_cast<std::uint32_t>(_mm256_movemask_epi8(null_mask))){
				const auto idx = group_idx + std::countr_zero(null_bits) / 8;
				images[idx] = image;
				latest = image;
				latest_index = idx;
				count_ = idx + 1;
				++use_count[idx].use_count;
				changed = true;
				return idx;
			}
		}
#endif
		const auto idx = images.size();
		set_capacity(idx * 2);
		images[idx] = image;
		++use_count[idx].use_count;
		latest = image;
		latest_index = idx;
		return idx;
	}
};
}

namespace mo_yanxi::graphic::draw::instruction{
inline void check_size(std::size_t size){
	if(size % 16 != 0){
		throw std::invalid_argument("instruction size must be a multiple of 16");
	}
}

export
struct hardware_limit_config{
	std::uint32_t max_group_count{};
	std::uint32_t max_group_size{};
	// [修改] 移除原有的 max_vertices_per_group 和 max_primitives_per_group
	// 改为全局上限记录，用于后续 Buffer 越界预警
	std::uint32_t max_global_vertices{};
	std::uint32_t max_global_primitives{};
};

export
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
		return static_cast<unsigned>(i);
	}

public:
	data_layout_table<> table{};
	std::vector<draw_uniform_data_entry> entries{};
	[[nodiscard]] data_entry_group() = default;

	[[nodiscard]] explicit data_entry_group(const data_layout_table<>& table)
		: table(table), entries(table.size()){
		for(auto&& [idx, data_entry] : table | std::views::enumerate){
			entries[idx].unit_size = data_entry.entry.size;
			entries[idx].data_.reserve(data_entry.entry.size * 8);
		}
	}

	void reset() noexcept{
		for(auto& vertex_data_entry : entries){
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
};

export
struct draw_list_context{
	using tag_type = mo_yanxi::binary_diff_trace::tag;

private:
	hardware_limit_config hardware_limit_{};

	std::vector<contiguous_draw_list> submit_groups_{};
	std::vector<state_transition> submit_transitions_{};

	state_tracker tracker_{};

	data_entry_group data_group_per_timeline_info_{};
	data_entry_group data_group_per_draw_call_info_{};
	image_view_history_dynamic dynamic_image_view_history_{16};

	contiguous_draw_list* current_group{};

	std::uint32_t get_expected_instruction_capacity() const noexcept{
		return 4096;
	}

public:
	[[nodiscard]] draw_list_context() = default;

	[[nodiscard]] draw_list_context(
		const hardware_limit_config& config,
		const data_layout_table<>& vertex_data_table,
		const data_layout_table<>& non_vertex_data_table
	)
		: hardware_limit_(config)
		, data_group_per_timeline_info_{vertex_data_table}
		, data_group_per_draw_call_info_{non_vertex_data_table}{
	}

	void clear() noexcept{
		submit_groups_.clear();
		submit_transitions_.clear();
		current_group = nullptr;
		tracker_.reset();
		data_group_per_timeline_info_.reset();
		data_group_per_draw_call_info_.reset();
	}

	void begin_rendering() noexcept{
		if(submit_groups_.empty()){
			submit_groups_.push_back({
					data_group_per_timeline_info_.size(), get_expected_instruction_capacity(), data_group_per_timeline_info_.entries
				});
		}

		// 【修复】强绑定：第 0 组诞生时，第 0 个状态断点容器同步诞生
		submit_transitions_.clear();
		submit_transitions_.emplace_back(0);

		data_group_per_draw_call_info_.reset();
		data_group_per_timeline_info_.reset();
		tracker_.reset();

		current_group = submit_groups_.data();
		current_group->reset();
	}

	void end_rendering() noexcept{
		current_group->finalize();
		dynamic_image_view_history_.optimize_and_reset();

		// 【修复】不再手动 advance，因为 current_group + 1 的 span 会自然涵盖当前组
		const auto submit_group_subrange = get_valid_submit_groups();

		for(const auto& group_subrange : submit_group_subrange){
			group_subrange.for_each_instruction([&] FORCE_INLINE (const instruction_head& head, std::byte* payload){
				switch(head.type){
				case instr_type::noop : break;
				case instr_type::uniform_update : break;
				default : {
					auto& gen = *instruction::start_lifetime_as<primitive_generic>(payload);
					gen.image.index = dynamic_image_view_history_.try_push(gen.image.get_image_view());

					switch(head.type) {
						case instr_type::poly: {
							auto& instr = *instruction::start_lifetime_as<poly>(payload);
							instr.segments.apply_reciprocal();
							break;
						}
						case instr_type::poly_partial: {
							auto& instr = *instruction::start_lifetime_as<poly_partial>(payload);
							instr.segments.apply_reciprocal();
							break;
						}
						case instr_type::constrained_curve: {
							auto& instr = *instruction::start_lifetime_as<parametric_curve>(payload);
							instr.segments.apply_reciprocal();
							break;
						}
						default: break;
					}
					break;
				}
				}
			});
		}
	}

	const hardware_limit_config& get_config() const noexcept{
		return hardware_limit_;
	}

	std::span<contiguous_draw_list> get_valid_submit_groups() noexcept{
		// 【修复】Span 使用 (pointer, count) 构造，涵盖正在录制的 current_group
		return std::span<contiguous_draw_list>{submit_groups_.data(), get_current_submit_group_index() + 1};
	}

	std::span<const contiguous_draw_list> get_valid_submit_groups() const noexcept{
		return std::span<const contiguous_draw_list>{submit_groups_.data(), get_current_submit_group_index() + 1};
	}

	std::span<const state_transition> get_state_transitions() const noexcept{
		return std::span<const state_transition>{submit_transitions_.data(), submit_transitions_.size()};
	}

	template <typename T>
	auto get_used_images() const noexcept{
		return dynamic_image_view_history_.get<T>();
	}

	void push_state(state_push_config config, tag_type tag, std::span<const std::byte> payload, unsigned offset = 0){
		switch(config.type){
		case state_push_type::idempotent : tracker_.update(tag, payload, offset);
			break;
		case state_push_type::non_idempotent :{
			state_transition_config temp_config;
			if(tracker_.flush(temp_config)){
				force_break_and_insert(std::move(temp_config));
			}

			state_transition_config non_idempotent_config;

			non_idempotent_config.push(tag, payload, offset);
			force_break_and_insert(std::move(non_idempotent_config));
			break;
		}
		default: std::unreachable();
		}
	}

	void push_instr(const instruction_head instr_head, const std::byte* instr){
		assert(current_group);

		check_size(instr_head.payload_size);
		assert(std::to_underlying(instr_head.type) < std::to_underlying(instruction::instr_type::SIZE));

		if(instr_head.type == instr_type::uniform_update){
			const auto payload = std::span{instr, instr_head.payload_size};
			const auto targetIndex = instr_head.payload.ubo.index;

			if(instr_head.payload.ubo.group_index){
				data_group_per_draw_call_info_.push(targetIndex, payload);
			} else{
				data_group_per_timeline_info_.push(targetIndex, payload);
			}
			return;
		}

		{
			state_transition_config diff_config;
			if(tracker_.flush(diff_config)){
				force_break_and_insert(std::move(diff_config));
			}
		}

		for(auto&& [idx, vertex_data_entry] : data_group_per_timeline_info_.entries | std::views::enumerate){
			if(vertex_data_entry.collapse()){
				const instruction_head collapse_head{
					.type = instr_type::uniform_update,
					.payload = {.marching_data = {static_cast<std::uint32_t>(idx)}}
				};
				try_push_(collapse_head);
			}
		}

		unsigned break_idx = -1u;
		for(auto&& [idx, vertex_data_entry] : data_group_per_draw_call_info_.entries | std::views::enumerate){
			if(vertex_data_entry.collapse()){
				if(break_idx == -1u){
					current_group->finalize();
					break_idx = static_cast<unsigned>(get_current_submit_group_index());
					advance_current_group();
				}
				// 【修复】绝对对齐的 UBO 断点追加逻辑
				submit_transitions_[break_idx].uniform_buffer_marching_indices.push_back(static_cast<std::uint32_t>(idx));
			}
		}

		try_push_(instr_head, instr);
	}

	void push_instr_batch(std::span<const instruction_head> heads, const std::byte* payload) {
		if (heads.empty()) return;

		assert(current_group);

		{
			state_transition_config diff_config;
			if (tracker_.flush(diff_config)) {
				force_break_and_insert(std::move(diff_config));
			}
		}

		const std::byte* cur_payload = payload;
		const std::size_t count = heads.size();
		std::size_t idx = 0;
		bool need_collapse_check = true;

		while (idx < count) {
			if (need_collapse_check && heads[idx].type != instr_type::uniform_update) {

				for (auto&& [i, vertex_data_entry] : data_group_per_timeline_info_.entries | std::views::enumerate) {
					if (vertex_data_entry.collapse()) {
						const instruction_head collapse_head{
							.type = instr_type::uniform_update,
							.payload = {.marching_data = {static_cast<std::uint32_t>(i)}}
						};
						try_push_(collapse_head);
					}
				}

				unsigned break_idx = -1u;
				for (auto&& [i, vertex_data_entry] : data_group_per_draw_call_info_.entries | std::views::enumerate) {
					if (vertex_data_entry.collapse()) {
						if (break_idx == -1u) {
							current_group->finalize();
							break_idx = static_cast<unsigned>(get_current_submit_group_index());
							advance_current_group();
						}
						submit_transitions_[break_idx].uniform_buffer_marching_indices.push_back(static_cast<std::uint32_t>(i));
					}
				}
				need_collapse_check = false;
			}

			while (idx < count && heads[idx].type != instr_type::uniform_update) {
				const auto& head = heads[idx];
				check_size(head.payload_size);
				assert(std::to_underlying(head.type) < std::to_underlying(instruction::instr_type::SIZE));

				try_push_(head, cur_payload);
				cur_payload += head.payload_size;
				++idx;
			}

			while (idx < count && heads[idx].type == instr_type::uniform_update) {
				const auto& head = heads[idx];
				check_size(head.payload_size);

				const auto ubo_payload = std::span{cur_payload, head.payload_size};
				const auto targetIndex = head.payload.ubo.index;

				if (head.payload.ubo.group_index) {
					data_group_per_draw_call_info_.push(targetIndex, ubo_payload);
				} else {
					data_group_per_timeline_info_.push(targetIndex, ubo_payload);
				}

				need_collapse_check = true;
				cur_payload += head.payload_size;
				++idx;
			}
		}
	}

	std::size_t get_current_submit_group_index() const noexcept{
		assert(current_group != nullptr);
		return current_group - submit_groups_.data();
	}

	template <typename S>
	[[nodiscard]] auto& get_data_group_vertex_info(this S& self) noexcept{
		return self.data_group_per_timeline_info_;
	}

	template <typename S>
	[[nodiscard]] auto& get_data_group_non_vertex_info(this S& self) noexcept{
		return self.data_group_per_draw_call_info_;
	}

	std::uint32_t get_submit_sections_count() const noexcept{
		// 【修复】统一基准：提交节区数严格等于当前 Group 索引 + 1
		return static_cast<std::uint32_t>(get_current_submit_group_index() + 1);
	}

	const state_transition_config& get_break_config_at(std::size_t index) const noexcept{
		return submit_transitions_[index].config;
	}

private:
	void advance_current_group(){
		auto new_idx = get_current_submit_group_index() + 1;

		if(new_idx == submit_groups_.size()){
			submit_groups_.emplace_back(data_group_per_timeline_info_.size(), get_expected_instruction_capacity(),
				data_group_per_timeline_info_.entries);
		}

		// 安全重分配处理
		current_group = submit_groups_.data() + new_idx;
		current_group->reset();

		// 【修复】强绑定：推进 Group 的同时，必须为其生成同源的 Transition
		if(submit_transitions_.size() <= new_idx){
			submit_transitions_.emplace_back(new_idx);
		} else {
			submit_transitions_[new_idx] = state_transition{static_cast<unsigned>(new_idx)};
		}
	}

	void force_break_and_insert(state_transition_config&& config){
		current_group->finalize();
		auto idx = get_current_submit_group_index();

		// 直接挂载到当前组（无论组空与否）
		submit_transitions_[idx].config.append(config);

		// 无条件开辟下一组
		advance_current_group();
	}

	bool try_push_(const instruction_head& instr_head, const std::byte* instr){
		current_group->push(instr_head, instr);
		return false;
	}

	bool try_push_(const instruction_head& instr_head){
		return try_push_(instr_head, nullptr);
	}
};
}
