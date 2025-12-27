module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

#ifdef __AVX2__
#include <immintrin.h>
#endif


export module mo_yanxi.graphic.draw.instruction.batch.frontend;

export import mo_yanxi.graphic.draw.instruction.batch.common;
export import mo_yanxi.graphic.draw.instruction.general;
export import mo_yanxi.graphic.draw.instruction.util;
export import mo_yanxi.user_data_entry;

import mo_yanxi.type_register;
import std;

namespace mo_yanxi::graphic::draw::instruction{

#pragma region Legacy

/*
//TODO support user defined size?
export
template <std::size_t CacheCount = 4>
struct image_view_history{
	static constexpr std::size_t max_cache_count = (CacheCount + 3) / 4 * 4;
	static_assert(max_cache_count % 4 == 0);
	static_assert(sizeof(void*) == sizeof(std::uint64_t));
	using handle_t = image_handle_t;

private:
	alignas(32) std::array<handle_t, max_cache_count> images{};
	handle_t latest{};
	std::uint32_t latest_index{};
	std::uint32_t count{};
	bool changed{};

public:
	bool check_changed() noexcept{
		return std::exchange(changed, false);
	}

	FORCE_INLINE void clear(this image_view_history& self) noexcept{
		self = {};
	}

	[[nodiscard]] FORCE_INLINE std::span<const handle_t> get() const noexcept{
		return {images.data(), count};
	}

	[[nodiscard]] FORCE_INLINE /*constexpr#1# std::uint32_t try_push(handle_t image) noexcept{
		if(!image) return std::numeric_limits<std::uint32_t>::max(); //directly vec4(1)
		if(image == latest) return latest_index;

#ifndef __AVX2__
		for(std::size_t i = 0; i < images.size(); ++i){
			auto& cur = images[i];
			if(image == cur){
				latest = image;
				latest_index = i;
				return i;
			}

			if(cur == nullptr){
				latest = cur = image;
				latest_index = i;
				count = i + 1;
				changed = true;
				return i;
			}
		}
#else

		const __m256i target = _mm256_set1_epi64x(std::bit_cast<std::int64_t>(image));
		const __m256i zero = _mm256_setzero_si256();

		for(std::uint32_t group_idx = 0; group_idx != max_cache_count; group_idx += 4){
			const auto group = _mm256_load_si256(reinterpret_cast<const __m256i*>(images.data() + group_idx));

			const auto eq_mask = _mm256_cmpeq_epi64(group, target);
			if(const auto eq_bits = std::bit_cast<std::uint32_t>(_mm256_movemask_epi8(eq_mask))){
				const auto idx = group_idx + /*local offset#1#std::countr_zero(eq_bits) / 8;
				latest = image;
				latest_index = idx;
				return idx;
			}

			const auto null_mask = _mm256_cmpeq_epi64(group, zero);
			if(const auto null_bits = std::bit_cast<std::uint32_t>(_mm256_movemask_epi8(null_mask))){
				const auto idx = group_idx + /*local offset#1#std::countr_zero(null_bits) / 8;
				images[idx] = image;
				latest = image;
				latest_index = idx;
				count = idx + 1;
				changed = true;
				return idx;
			}
		}
#endif

		return max_cache_count;
	}
};*/

#pragma endregion

template <typename T, std::size_t Alignment = alignof(T)>
class aligned_allocator {
public:
	using value_type = T;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;
	using propagate_on_container_move_assignment = std::true_type;
	using is_always_equal = std::true_type;

	static_assert(std::has_single_bit(Alignment), "Alignment must be a power of 2");
	static_assert(Alignment >= alignof(T), "Alignment must be at least alignof(T)");

	constexpr aligned_allocator() noexcept = default;
	constexpr aligned_allocator(const aligned_allocator&) noexcept = default;

	template <typename U>
	constexpr aligned_allocator(const aligned_allocator<U, Alignment>&) noexcept {}

	template <typename U>
	struct rebind {
		using other = aligned_allocator<U, Alignment>;
	};

	[[nodiscard]] static T* allocate(std::size_t n) {
		if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
			throw std::bad_alloc();
		}

		const std::size_t bytes = n * sizeof(T);
		void* p = ::operator new(bytes, std::align_val_t{Alignment});
		return static_cast<T*>(p);
	}

	static void deallocate(T* p, std::size_t n) noexcept {
		::operator delete(p, static_cast<std::align_val_t>(Alignment));
	}

	friend bool operator==(const aligned_allocator&, const aligned_allocator&) { return true; }
	friend bool operator!=(const aligned_allocator&, const aligned_allocator&) { return false; }
};

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

	[[nodiscard]] FORCE_INLINE /*constexpr*/ std::uint32_t try_push(handle_t image) noexcept{
		if(!image) return std::numeric_limits<std::uint32_t>::max(); //directly vec4(1)
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
				const auto idx = group_idx + /*local offset*/std::countr_zero(eq_bits) / 8;
				latest = image;
				latest_index = idx;
				count_ = std::max(count_, idx + 1);
				++use_count[idx].use_count;
				return idx;
			}

			const auto null_mask = _mm256_cmpeq_epi64(group, zero);
			if(const auto null_bits = std::bit_cast<std::uint32_t>(_mm256_movemask_epi8(null_mask))){
				const auto idx = group_idx + /*local offset*/std::countr_zero(null_bits) / 8;
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

		const auto idx =  images.size();
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
constexpr inline std::uint32_t MaxVerticesPerMesh = 64;

//
inline void check_size(std::size_t size) {
	if (size % 16 != 0) {
		throw std::invalid_argument("instruction size must be a multiple of 16");
	}
}

export
struct hardware_limit_config{
	std::uint32_t max_group_count{};
	std::uint32_t max_group_size{};
	std::uint32_t max_vertices_per_group{};
	std::uint32_t max_primitives_per_group{};
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


// (Logic extracted to Host Context)
export
struct draw_list_context{
private:
	hardware_limit_config hardware_limit_{};

	std::vector<contiguous_draw_list> submit_groups_{};
	std::vector<state_transition> submit_transitions_{};

	state_transition_config defer_transition_config_front_{};
	state_transition_config defer_transition_config_back_{};

	data_entry_group data_group_vertex_info_{};
	data_entry_group data_group_non_vertex_info_{};

	image_view_history_dynamic dynamic_image_view_history_{16};

	contiguous_draw_list* current_group{};
	std::uint32_t get_mesh_dispatch_limit() const noexcept{
		return 32;
	}

public:
	[[nodiscard]] draw_list_context() = default;

	[[nodiscard]] draw_list_context(
		const hardware_limit_config& config,
		const data_layout_table<>& vertex_data_table,
		const data_layout_table<>& non_vertex_data_table
	)
		: hardware_limit_(config)
		, data_group_vertex_info_{vertex_data_table}
		, data_group_non_vertex_info_{non_vertex_data_table}{
	}

	void begin_rendering() noexcept{
		if(submit_groups_.empty()){
			submit_groups_.push_back({
					data_group_vertex_info_.size(), get_mesh_dispatch_limit(), data_group_vertex_info_.entries
				});
		}

		data_group_non_vertex_info_.reset();
		data_group_vertex_info_.reset();

		submit_groups_.front().reset({});
		current_group = submit_groups_.data();
		submit_transitions_.clear();
	}

	void end_rendering() noexcept{
		current_group->finalize();
		dynamic_image_view_history_.optimize_and_reset();

		if(current_group->empty()){
		} else{
			current_group++;
			auto& trans = submit_transitions_.emplace_back(static_cast<unsigned>(get_current_submit_group_index()));
			apply_deferred_transition_(trans);
		}

		const auto submit_group_subrange = get_valid_submit_groups();

		for(const auto& group_subrange : submit_group_subrange){
			group_subrange.for_each_instruction([&] FORCE_INLINE (const instruction_head& head, std::byte* payload){
				switch(head.type){
				case instr_type::noop : break;
				case instr_type::uniform_update : break;
				default : auto& gen = *instruction::start_lifetime_as<primitive_generic>(payload);
					gen.image.index = dynamic_image_view_history_.try_push(gen.image.get_image_view());
					break;
				}
			});
		}
	}

	const hardware_limit_config& get_config() const noexcept{
		return hardware_limit_;
	}

	std::span<contiguous_draw_list> get_valid_submit_groups() noexcept{
		return std::span{submit_groups_.data(), current_group};
	}

	std::span<const state_transition> get_state_transitions() const noexcept{
		return std::span{submit_transitions_.data(), submit_transitions_.size()};
	}

	template <typename T>
	auto get_used_images() const noexcept{
		return dynamic_image_view_history_.get<T>();
	}

	void push_state(state_push_config config, std::uint32_t flag, std::span<const std::byte> payload){
		switch(config.target){
		case state_push_target::immediate :{
			//TODO pending check
			//TODO lazy check
			state_transition* breakpoint;
			if(current_group->get_pushed_instruction_size() != 0 || submit_transitions_.empty()){
				current_group->finalize();
				advance_current_group();
				if(!submit_transitions_.empty()) apply_deferred_transition_(submit_transitions_.back());

				breakpoint = &submit_transitions_.emplace_back(static_cast<unsigned>(get_current_submit_group_index()));
			} else{
				breakpoint = &submit_transitions_.back();
			}

			breakpoint->config.push(flag, payload);
			break;
		}
		case state_push_target::defer_pre :{
			defer_transition_config_front_.push(flag, payload);
			break;
		}
		case state_push_target::defer_post :{
			defer_transition_config_back_.push(flag, payload);
			break;
		}
		default : std::unreachable();
		}
	}

	void push_instr(std::span<const std::byte> instr){
		assert(current_group);

		check_size(instr.size());
		const auto& instr_head = get_instr_head(instr.data());
		assert(std::to_underlying(instr_head.type) < std::to_underlying(instruction::instr_type::SIZE));

		if(instr_head.type == instr_type::uniform_update){
			const auto payload = get_payload_data_span(instr.data());
			const auto targetIndex = instr_head.payload.ubo.index;
			//TODO use other name to replace group index
			if(instr_head.payload.ubo.group_index){
				data_group_non_vertex_info_.push(targetIndex, payload);
			} else{
				data_group_vertex_info_.push(targetIndex, payload);
			}

			return;
		}

		for(auto&& [idx, vertex_data_entry] : data_group_vertex_info_.entries | std::views::enumerate){
			if(vertex_data_entry.collapse()){
				const instruction_head instruction_head{
						.type = instr_type::uniform_update,
						.size = sizeof(instruction::instruction_head),
						.payload = {.marching_data = {static_cast<std::uint32_t>(idx)}}
					};
				try_push_(instruction_head);
			}
		}

		state_transition* breakpoint{};
		for(auto&& [idx, vertex_data_entry] : data_group_non_vertex_info_.entries | std::views::enumerate){
			if(vertex_data_entry.collapse()){
				if(!breakpoint){
					const auto cur_idx = get_current_submit_group_index() + 1;
					if(!submit_transitions_.empty())apply_deferred_transition_(submit_transitions_.back());
					breakpoint = &submit_transitions_.emplace_back(static_cast<unsigned>(cur_idx));
				}

				breakpoint->uniform_buffer_marching_indices.push_back(static_cast<std::uint32_t>(idx));
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

	template <typename S>
	[[nodiscard]] auto& get_data_group_vertex_info(this S& self) noexcept{
		return self.data_group_vertex_info_;
	}

	template <typename S>
	[[nodiscard]] auto& get_data_group_non_vertex_info(this S& self) noexcept{
		return self.data_group_non_vertex_info_;
	}

	std::uint32_t get_submit_sections_count() const noexcept{
		return static_cast<std::uint32_t>(submit_transitions_.size());
	}

	const state_transition_config& get_break_config_at(std::size_t index) const noexcept{
		return submit_transitions_[index].config;
	}

private:
	//
	void advance_current_group(){
		auto last_param = current_group->get_extend_able_params();
		if(current_group == std::to_address(submit_groups_.rbegin())){
			current_group = &submit_groups_.emplace_back(data_group_vertex_info_.size(), get_mesh_dispatch_limit(),
				data_group_vertex_info_.entries);
		} else{
			++current_group;
		}

		current_group->reset(last_param);
	}

	bool try_push_(const instruction_head& instr_head, const std::byte* instr){
		//Check hardware limit
		while(!current_group->push(instr_head, instr, MaxVerticesPerMesh)){
			advance_current_group();
		}
		return false;
	}

	bool try_push_(const instruction_head& instr_head){
		return try_push_(instr_head, reinterpret_cast<const std::byte*>(std::addressof(instr_head)));
	}

	void apply_deferred_transition_(state_transition& transition){
		transition.config.append_front(defer_transition_config_front_);
		transition.config.append(defer_transition_config_back_);
		defer_transition_config_front_.clear();
		defer_transition_config_back_.clear();
	}
};
}
