module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>


#ifdef __AVX2__
#include <intrin.h>
#endif


export module mo_yanxi.graphic.draw.instruction.batch.frontend;

export import mo_yanxi.graphic.draw.instruction.batch.common;
export import mo_yanxi.graphic.draw.instruction.general;
export import mo_yanxi.graphic.draw.instruction.state_tracker;
export import mo_yanxi.vk.record_context;
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

    void set_capacity(std::size_t new_capacity){
       images.resize(4 + (new_capacity + 3) / 4 * 4);
       use_count.resize(4 + (new_capacity + 3) / 4 * 4);
    }

    void extend(handle_t handle){
       auto lastSz = images.size();
       images.resize(images.size() + 4);
       // 修复：必须同步扩展 use_count，否则在 try_push 中必定越界
       use_count.resize(images.size());
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
       for(std::size_t i = 0; i < images.size(); ++i){
          use_count[i].handle = images[i];
       }
       std::ranges::sort(use_count, std::ranges::greater{}, &use_record::use_count);

       std::size_t i = 0;
       for(; i < images.size(); ++i){
          if(use_count[i].use_count == 0){
             break;
          }
          images[i] = use_count[i].handle;
       }

       // 擦除无效元素
       images.erase(images.begin() + i, images.end());
       // 修复：强制对齐到 4 的整数倍，以保证 SIMD 指令的绝对安全
       // vector 的 resize 针对新增的元素会自动执行 zero-initialization，也就是 nullptr
       images.resize((images.size() + 3) / 4 * 4);

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
             latest_index = static_cast<std::uint32_t>(idx);
             ++use_count[idx].use_count;
             return static_cast<std::uint32_t>(idx);
          }

          if(cur == nullptr){
             latest = cur = image;
             latest_index = static_cast<std::uint32_t>(idx);
             count_ = static_cast<std::uint32_t>(idx + 1);
             ++use_count[idx].use_count;
             changed = true;
             return static_cast<std::uint32_t>(idx);
          }
       }
#else
       const __m256i target = _mm256_set1_epi64x(std::bit_cast<std::int64_t>(image));
       const __m256i zero = _mm256_setzero_si256();
       // 修复：将判断条件从 != 修改为 <，增强在非完全对齐情况下的鲁棒性
       for(std::uint32_t group_idx = 0; group_idx < images.size(); group_idx += 4){
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
       // 当空间不足时：由于前面的逻辑无法找到可用的 nullptr 槽位，说明已满，进行自动追加扩展
       const auto idx = static_cast<std::uint32_t>(images.size());
       set_capacity(idx * 2); // set_capacity 保证会将其大小扩大至足够，且同步应用到 images 和 use_count
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
		const std::size_t i = ientry - table.begin();
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
	std::vector<section_event> submit_transitions_{};

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
					data_group_per_timeline_info_.size(), get_expected_instruction_capacity(),
					data_group_per_timeline_info_.entries
				});
		}

		data_group_per_draw_call_info_.reset();
		data_group_per_timeline_info_.reset();
		tracker_.reset();

		submit_groups_.front().reset();
		current_group = submit_groups_.data();
		submit_transitions_.clear();
	}

	void end_rendering() noexcept{
		current_group->finalize();
		dynamic_image_view_history_.optimize_and_reset();

		if(current_group->empty()){
			if(!submit_transitions_.empty() && submit_transitions_.back().break_before_index ==
				get_current_submit_group_index()){
				submit_transitions_.pop_back();
			}
		} else{
			advance_current_group();
		}

		const auto submit_group_subrange = get_valid_submit_groups();

		for(const auto& group_subrange : submit_group_subrange){
			group_subrange.for_each_instruction([&] FORCE_INLINE (const instruction_head& head, std::byte* payload){
				switch(head.type){
				case instr_type::noop : break;
				case instr_type::uniform_update : break;
				default :{
					auto& gen = *std::launder(reinterpret_cast<primitive_generic*>(payload));
					gen.image.index = dynamic_image_view_history_.try_push(gen.image.get_image_view());

					switch(head.type){
					case instr_type::poly :{
						auto& instr = *std::launder(reinterpret_cast<poly*>(payload));
						instr.segments.apply_reciprocal();
						break;
					}
					case instr_type::poly_partial :{
						auto& instr = *std::launder(reinterpret_cast<poly_partial*>(payload));
						instr.segments.apply_reciprocal();
						break;
					}
					case instr_type::constrained_curve :{
						auto& instr = *std::launder(reinterpret_cast<parametric_curve*>(payload));
						instr.segments.apply_reciprocal();
						break;
					}
					default : break;
					}
					break;
				}
				}
			});
		}
	}

	const state_tracker& get_tracker() const noexcept{
		return tracker_;
	}

	const hardware_limit_config& get_config() const noexcept{
		return hardware_limit_;
	}

	std::span<contiguous_draw_list> get_valid_submit_groups() noexcept{
		return std::span{submit_groups_.data(), current_group};
	}

	std::span<const contiguous_draw_list> get_valid_submit_groups() const noexcept{
		return std::span{submit_groups_.data(), current_group};
	}

	std::span<const section_event> get_state_transitions() const noexcept{
		return std::span{submit_transitions_.data(), submit_transitions_.size()};
	}

	template <typename T>
	auto get_used_images() const noexcept{
		return dynamic_image_view_history_.get<T>();
	}

	/**
	 * @brief 提交状态变更
	 * @param config 状态配置 (指定是否为幂等)
	 * @param tag 状态标签 (Major + Minor)
	 * @param payload 数据
	 * @param offset 数据偏移
	 */
	void push_state(state_push_config config, tag_type tag, std::span<const std::byte> payload, unsigned offset = 0){
		tracker_.clear_mask(config.to_clear);
		tracker_.update_depth(config.depth_op, tag.major);


		switch(config.type){
		case state_push_type::idempotent : tracker_.update(tag, payload, offset);
			break;
		case state_push_type::non_idempotent :{
			flush_pending_state_deltas_();
			auto& event = ensure_section_event_();
			event.state_deltas.push(tag, payload, offset);
			break;
		}
		default : std::unreachable();
		}
	}


	void push_instr(const instruction_head instr_head, const std::byte* instr){
		assert(current_group);

		check_size(instr_head.payload_size);
		assert(std::to_underlying(instr_head.type) < std::to_underlying(instruction::instr_type::SIZE));

		if(instr_head.type == instr_type::uniform_update){
			push_uniform_update_(instr_head, instr);
			return;
		}

		flush_pending_state_deltas_();
		flush_pending_uniform_updates_();

		try_push_(instr_head, instr);
	}

	void push_instr_batch(std::span<const instruction_head> heads, const std::byte* payload){
		if(heads.empty()) return;

		assert(current_group);


		flush_pending_state_deltas_();

		const std::byte* cur_payload = payload;
		const std::size_t count = heads.size();
		std::size_t idx = 0;
		bool need_collapse_check = true;


		while(idx < count){
			if(need_collapse_check && heads[idx].type != instr_type::uniform_update){
				flush_pending_uniform_updates_();
				need_collapse_check = false;
			}


			while(idx < count && heads[idx].type != instr_type::uniform_update){
				const auto& head = heads[idx];


				check_size(head.payload_size);
				assert(std::to_underlying(head.type) < std::to_underlying(instruction::instr_type::SIZE));

				try_push_(head, cur_payload);
				cur_payload += head.payload_size;
				++idx;
			}


			while(idx < count && heads[idx].type == instr_type::uniform_update){
				const auto& head = heads[idx];
				check_size(head.payload_size);

				push_uniform_update_(head, cur_payload);


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
		return static_cast<std::uint32_t>(submit_transitions_.size());
	}

	const state_transition_config& get_break_config_at(std::size_t index) const noexcept{
		return submit_transitions_[index].state_deltas;
	}

private:
	void advance_current_group(){
		if(current_group == std::to_address(submit_groups_.rbegin())){
			current_group = &submit_groups_.emplace_back(data_group_per_timeline_info_.size(),
			                                             get_expected_instruction_capacity(),
			                                             data_group_per_timeline_info_.entries);
		} else{
			++current_group;
		}


		current_group->reset();
	}

	section_event& ensure_section_event_(){
		current_group->finalize();

		if(current_group->empty() && !submit_transitions_.empty()){
			return submit_transitions_.back();
		}

		auto idx = static_cast<unsigned>(get_current_submit_group_index());
		auto& event = submit_transitions_.emplace_back(idx);
		advance_current_group();
		return event;
	}

	void flush_pending_state_deltas_(){
		state_transition_config diff_config;
		if(!tracker_.flush(diff_config)) return;
		auto& event = ensure_section_event_();
		event.state_deltas.append(diff_config);
	}

	void flush_pending_uniform_updates_(){
		for(auto&& [idx, vertex_data_entry] : data_group_per_timeline_info_.entries | std::views::enumerate){
			if(vertex_data_entry.collapse()){
				const instruction_head collapse_head{
					.type = instr_type::uniform_update,
					.payload = {.marching_data = {static_cast<std::uint32_t>(idx)}}
				};
				try_push_(collapse_head);
			}
		}

		section_event* event{};
		for(auto&& [idx, vertex_data_entry] : data_group_per_draw_call_info_.entries | std::views::enumerate){
			if(!vertex_data_entry.collapse()) continue;
			if(!event){
				event = &ensure_section_event_();
			}
			event->per_draw_uniform_bumps.push_back(static_cast<std::uint32_t>(idx));
		}
	}

	void push_uniform_update_(const instruction_head instr_head, const std::byte* instr){
		const auto payload = std::span{instr, instr_head.payload_size};
		const auto target_index = instr_head.payload.ubo.index;

		if(instr_head.payload.ubo.group_index){
			data_group_per_draw_call_info_.push(target_index, payload);
		} else{
			data_group_per_timeline_info_.push(target_index, payload);
		}
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
