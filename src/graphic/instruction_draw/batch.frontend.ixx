module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>


#ifdef __AVX2__
#include <intrin.h>
#endif


export module mo_yanxi.graphic.g2d.batch.frontend;

export import mo_yanxi.graphic.g2d.batch.common;
export import mo_yanxi.graphic.g2d.general;
export import mo_yanxi.graphic.g2d.state_tracker;
export import mo_yanxi.vk.record_context;
export import mo_yanxi.graphic.g2d;
export import mo_yanxi.user_data_entry;

import mo_yanxi.type_register;
import std;

namespace mo_yanxi::graphic::g2d{
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
	std::vector<section_event> section_events_{};

	state_tracker tracker_{};

	data_entry_group data_group_per_timeline_info_{};
	data_entry_group data_group_per_draw_call_info_{};
	mo_yanxi::graphic::image_view_registry* image_view_registry_{};

	contiguous_draw_list* current_group{};


	std::uint32_t get_expected_instruction_capacity() const noexcept{
		return 4096;
	}

public:
	[[nodiscard]] draw_list_context() = default;

	[[nodiscard]] draw_list_context(
		const hardware_limit_config& config,
		const data_layout_table<>& vertex_data_table,
		const data_layout_table<>& non_vertex_data_table,
		mo_yanxi::graphic::image_view_registry& image_view_registry
	)
		: hardware_limit_(config)
		  , data_group_per_timeline_info_{vertex_data_table}
		  , data_group_per_draw_call_info_{non_vertex_data_table}
		  , image_view_registry_{std::addressof(image_view_registry)}{
	}

	void clear() noexcept{
		submit_groups_.clear();
		section_events_.clear();
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
		section_events_.clear();
	}

	void end_rendering(){
		current_group->finalize();

		if(current_group->empty()){
			if(!section_events_.empty() && section_events_.back().group_index ==
				get_current_submit_group_index()){
				section_events_.pop_back();
			}
		} else{
			advance_current_group();
		}

		assert(image_view_registry_ != nullptr);

		const auto submit_group_subrange = get_valid_submit_groups();

		for(const auto& group_subrange : submit_group_subrange){
			group_subrange.for_each_instruction([&] FORCE_INLINE (const instruction_head& head, std::byte* payload){
				switch(head.type){
				case instr_type::noop : break;
				case instr_type::uniform_update : break;
				default :{
					auto& gen = *std::launder(reinterpret_cast<primitive_generic*>(payload));
					if(gen.image){
						gen.image = image_view_registry_->resolve_binding(gen.image);
					}

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

	std::span<const section_event> get_section_events() const noexcept{
		return std::span{section_events_.data(), section_events_.size()};
	}

	/**
	 * @brief 提交状态变更
	 * @param config 状态提交与分段配置
	 * @param tag 状态标签 (Major + Minor)
	 * @param payload 数据
	 * @param offset 数据偏移
	 */
	void push_state(state_push_config config, tag_type tag, std::span<const std::byte> payload, unsigned offset = 0){
		tracker_.clear_mask(config.to_clear);
		tracker_.update_depth(config.depth_op, tag.major);

		if(config.tracks_persistent_state()){
			tracker_.update(tag, payload, offset);
		}

		if(config.emits_immediate_delta()){
			flush_pending_state_deltas_();
			auto& event = ensure_section_event_();
			event.state_deltas.push(tag, payload, offset);
		}

		if(config.forces_section_break() && !config.emits_immediate_delta()){
			ensure_section_event_();
		}
	}


	void push_instr(const instruction_head instr_head, const std::byte* instr){
		assert(current_group);

		check_size(instr_head.payload_size);
		assert(std::to_underlying(instr_head.type) < std::to_underlying(instr_type::SIZE));

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
				assert(std::to_underlying(head.type) < std::to_underlying(instr_type::SIZE));

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

	std::uint32_t get_section_count() const noexcept{
		return static_cast<std::uint32_t>(section_events_.size());
	}

	const section_state_delta_set& get_section_state_deltas(std::size_t index) const noexcept{
		return section_events_[index].state_deltas;
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

		if(current_group->empty() && !section_events_.empty()){
			return section_events_.back();
		}

		auto idx = static_cast<unsigned>(get_current_submit_group_index());
		auto& event = section_events_.emplace_back(idx);
		advance_current_group();
		return event;
	}

	void flush_pending_state_deltas_(){
		section_state_delta_set delta_set;
		if(!tracker_.flush(delta_set)) return;
		auto& event = ensure_section_event_();
		event.state_deltas.append(delta_set);
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
