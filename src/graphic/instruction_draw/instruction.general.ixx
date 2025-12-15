module;

#include <immintrin.h>
#include <vulkan/vulkan.h>
#include "gch/small_vector.hpp"
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.graphic.draw.instruction.general;
import std;

import mo_yanxi.meta_programming;
import mo_yanxi.type_register;

namespace mo_yanxi::graphic::draw::instruction{

export template<typename Rng, typename T>
concept contiguous_range_of = std::ranges::contiguous_range<Rng> && std::same_as<std::ranges::range_value_t<Rng>, T>;

export constexpr inline std::size_t instr_required_align = 16;

static_assert(std::has_single_bit(instr_required_align));

export FORCE_INLINE std::byte* find_aligned_on(std::byte* where) noexcept{
	const auto A = std::bit_cast<std::uintptr_t>(where);
	static constexpr std::size_t M = instr_required_align;
	static constexpr std::size_t Mask = M - 1;

	// A + Mask: 确保至少跨越到下一个 M 的倍数
	// & ~Mask: 清零低位，实现向下取整到 M 的倍数
	const auto next = (A + Mask) & (~Mask);

	return std::bit_cast<std::byte*>(next);
}

export FORCE_INLINE const std::byte* find_aligned_on(const std::byte* where) noexcept{
	return find_aligned_on(const_cast<std::byte*>(where));
}

export struct instruction_buffer{
 	static constexpr std::size_t align = 32;

private:
	std::byte* src{};
	std::byte* dst{};

public:
	[[nodiscard]] constexpr instruction_buffer() noexcept = default;

	[[nodiscard]] explicit instruction_buffer(std::size_t byte_size){
		const auto actual_size = ((byte_size + (align - 1)) / align) * align;
		const auto p = ::operator new(actual_size, std::align_val_t{align});
		src = new(p) std::byte[actual_size]{};
		dst = src + actual_size;
	}

	~instruction_buffer(){
		std::destroy_n(src, size());
		::operator delete(src, size(), std::align_val_t{align});
	}

	[[nodiscard]] FORCE_INLINE CONST_FN constexpr std::size_t size() const noexcept{
		return dst - src;
	}

	[[nodiscard]] FORCE_INLINE CONST_FN constexpr std::byte* begin() const noexcept{
		return std::assume_aligned<align>(src);
	}

	[[nodiscard]] FORCE_INLINE CONST_FN constexpr std::byte* data() const noexcept{
		return std::assume_aligned<align>(src);
	}

	void clear() const noexcept{
		std::memset(begin(), 0, size());
	}

	[[nodiscard]] FORCE_INLINE CONST_FN constexpr std::byte* end() const noexcept{
		return std::assume_aligned<align>(dst);
	}

	instruction_buffer(const instruction_buffer& other)
		: instruction_buffer(other.size()){
		std::memcpy(src, other.src, size());
	}

	constexpr instruction_buffer(instruction_buffer&& other) noexcept
		: src{std::exchange(other.src, {})},
		  dst{std::exchange(other.dst, {})}{
	}

	instruction_buffer& operator=(const instruction_buffer& other){
		if(this == &other) return *this;
		*this = instruction_buffer{other};
		return *this;
	}

	constexpr instruction_buffer& operator=(instruction_buffer&& other) noexcept{
		if(this == &other) return *this;
		std::destroy_n(src, size());
		::operator delete(src, size(), std::align_val_t{align});

		src = std::exchange(other.src, {});
		dst = std::exchange(other.dst, {});
		return *this;
	}

	template <bool noCheck = false>
	void resize(const std::size_t new_size){
		if constexpr (!noCheck){
			if(size() == new_size){
				return;
			}
		}


		const auto p = ::operator new(new_size, std::align_val_t{align});
		auto* next = new(p) std::byte[new_size]{};
		std::memcpy(next, src, size());
		::operator delete(src, size(), std::align_val_t{align});
		src = next;
		dst = src + new_size;
	}

};


template <typename T>
constexpr std::size_t get_size(const T& arg){
	if constexpr (std::ranges::input_range<T>){
		static_assert(std::ranges::sized_range<T>, "T must be sized range");
		static_assert(std::is_trivially_copyable_v<std::ranges::range_value_t<T>>);
		return std::ranges::size(arg) * sizeof(std::ranges::range_value_t<T>);
	}else{
		static_assert(std::is_trivially_copyable_v<T>);
		return sizeof(T);
	}
}

export
template <typename... Args>
constexpr std::size_t get_type_size_sum(const Args& ...args) noexcept{
	return ((instruction::get_size(args)) + ... + 0uz);
}

export
template <typename T>
[[nodiscard]] const T* start_lifetime_as(const void* p) noexcept{
	const auto mp = const_cast<void*>(p);
	const auto bytes = new(mp) std::byte[sizeof(T)];
	const auto ptr = reinterpret_cast<const T*>(bytes);
	(void)*ptr;
	return ptr;
}

export
template <typename T>
[[nodiscard]] T* start_lifetime_as(void* p) noexcept{
	const auto mp = p;
	const auto bytes = new(mp) std::byte[sizeof(T)];
	const auto ptr = reinterpret_cast<T*>(bytes);
	(void)*ptr;
	return ptr;
}

export
struct user_data_indices{
	std::uint32_t global_index;
	std::uint32_t group_index;
};

export
struct state_change_config{
	std::uint32_t index;
};

export
struct gpu_vertex_data_advance_data{
	std::uint32_t index;
};

static_assert(sizeof(user_data_indices) == 8);

export
template <typename T>
	requires (sizeof(T) <= 8)
union dispatch_info_payload{
	T draw;

	user_data_indices ubo;
	gpu_vertex_data_advance_data marching_data;
};

export
template <typename EnumTy, typename DrawInfoTy>
	requires (std::is_enum_v<EnumTy>)
struct alignas(instr_required_align) generic_instruction_head{
	EnumTy type;
	std::uint32_t size; //size include head
	dispatch_info_payload<DrawInfoTy> payload;

	[[nodiscard]] constexpr std::ptrdiff_t get_instr_byte_size() const noexcept{
		return size;
	}

	[[nodiscard]] constexpr std::ptrdiff_t get_payload_byte_size() const noexcept{
		return size - sizeof(generic_instruction_head);
	}
};

export
struct batch_backend_interface{
	using host_impl_ptr = void*;

	using function_signature_buffer_acquire = void(host_impl_ptr, std::span<const std::byte> payload);
	using function_signature_consume_all = void(host_impl_ptr);
	using function_signature_wait_idle = void(host_impl_ptr);
	using function_signature_update_state_entry = void(host_impl_ptr, std::uint32_t flag, std::span<const std::byte> payload);

private:
	host_impl_ptr host;
	std::add_pointer_t<function_signature_buffer_acquire> fptr_push;
	std::add_pointer_t<function_signature_consume_all> fptr_consume;
	std::add_pointer_t<function_signature_wait_idle> fptr_wait_idle;
	std::add_pointer_t<function_signature_update_state_entry> state_handle;


public:
	[[nodiscard]] constexpr batch_backend_interface() = default;

	template <typename HostT,
	std::invocable<HostT&, std::span<const std::byte>> AcqFn,
	std::invocable<HostT&> ConsumeFn,
	std::invocable<HostT&> WaitIdleFn,
	std::invocable<HostT&, std::uint32_t, std::span<const std::byte>> StateHandleFn
	>
	[[nodiscard]] constexpr batch_backend_interface(
		HostT& host,
		AcqFn,
		ConsumeFn,
		WaitIdleFn,
		StateHandleFn
	) noexcept : host(std::addressof(host)), fptr_push(+[](host_impl_ptr host, std::span<const std::byte> instr){
		return AcqFn::operator()(*static_cast<HostT*>(host), instr);
	}), fptr_consume(+[](host_impl_ptr host) static {
		ConsumeFn::operator()(*static_cast<HostT*>(host));
	}), fptr_wait_idle(+[](host_impl_ptr host) static {
		WaitIdleFn::operator()(*static_cast<HostT*>(host));
	}), state_handle(+[](host_impl_ptr host, std::uint32_t flag, std::span<const std::byte> payload) static {
		StateHandleFn::operator()(*static_cast<HostT*>(host), flag, payload);
	}){

	}

	explicit operator bool() const noexcept{
		return host;
	}

	template <typename HostTy>
	HostTy& get_host() const noexcept{
		CHECKED_ASSUME(host != nullptr);
		return *static_cast<HostTy*>(host);
	}

	void push(std::span<const std::byte> instr) const {
		CHECKED_ASSUME(fptr_push != nullptr);
		fptr_push(host, instr);
	}

	void consume_all() const{
		CHECKED_ASSUME(fptr_consume != nullptr);
		fptr_consume(host);
	}

	void wait_idle() const{
		CHECKED_ASSUME(fptr_wait_idle != nullptr);
		fptr_wait_idle(host);
	}

	void flush() const{
		consume_all();
		wait_idle();
	}


	void update_state(std::uint32_t index, std::span<const std::byte> payload) const{
		state_handle(host, index, payload);
	}

};


}


namespace mo_yanxi::graphic::draw{
namespace instruction{

#ifndef MO_YANXI_GRAPHIC_DRAW_INSTRUCTION_IMAGE_HANDLE_TYPE
using image_handle_t = VkImageView;
#else
using image_handle_t = MO_YANXI_GRAPHIC_DRAW_INSTRUCTION_IMAGE_HANDLE_TYPE;
static_assert(sizeof(image_handle_t) == 8)
#endif

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

	[[nodiscard]] FORCE_INLINE /*constexpr*/ std::uint32_t try_push(handle_t image) noexcept{
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
				const auto idx = group_idx + /*local offset*/std::countr_zero(eq_bits) / 8;
				latest = image;
				latest_index = idx;
				return idx;
			}

			const auto null_mask = _mm256_cmpeq_epi64(group, zero);
			if(const auto null_bits = std::bit_cast<std::uint32_t>(_mm256_movemask_epi8(null_mask))){
				const auto idx = group_idx + /*local offset*/std::countr_zero(null_bits) / 8;
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
};


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
	static_assert(sizeof(void*) == sizeof(std::uint64_t));
	using handle_t = image_handle_t;

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

	[[nodiscard]] image_view_history_dynamic(std::uint32_t capacity){
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

	[[nodiscard]] FORCE_INLINE std::span<const handle_t> get() const noexcept{
		return {images.data(), count_};
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


struct image_set_result{
	image_handle_t image;
	bool succeed;

	constexpr explicit operator bool() const noexcept{
		return succeed;
	}
};


export union image_view{
	image_handle_t view;
	std::uint64_t index;

	void set_view(image_handle_t view) noexcept{
		new(&this->view) image_handle_t(view);
	}

	void set_index(std::uint32_t idx) noexcept{
		new(&this->index) std::uint64_t(idx);
	}

	[[nodiscard]] image_handle_t get_image_view() const noexcept{
		return view;
	}
};

export struct draw_mode{
	std::uint32_t cap;
};

export struct alignas(instr_required_align) primitive_generic{
	image_view image;
	draw_mode mode;
	float depth;
};

export enum struct instr_type : std::uint32_t{
	noop,
	uniform_update,

	triangle,
	quad,
	rectangle,

	line,
	line_segments,
	line_segments_closed,

	poly,
	poly_partial,

	constrained_curve,


	rect_ortho,
	rect_ortho_outline,
	row_patch,

	SIZE,
};

export struct draw_payload{
	std::uint32_t vertex_count;
	std::uint32_t primitive_count;
};

export using instruction_head = generic_instruction_head<instr_type, draw_payload>;


static_assert(std::is_standard_layout_v<instruction_head>);

export
template <typename Instr, typename Arg>
struct is_valid_consequent_argument : std::false_type{

};

template <typename Instr, typename Arg>
constexpr inline bool is_valid_consequent_argument_v = is_valid_consequent_argument<Instr, Arg>::value;

export
template <typename Instr, typename... Args>
concept valid_consequent_argument = (is_valid_consequent_argument_v<Instr, Args> && ...);

export
template <typename T>
constexpr inline instr_type instruction_type_of = instr_type::SIZE;

export
template <typename T>
concept known_instruction = instruction_type_of<T> != instr_type::SIZE;

export
template <typename T, typename... Args>
	requires (std::is_trivially_copyable_v<T> && valid_consequent_argument<T, Args...>)
constexpr std::size_t get_instr_size(const Args& ...args) noexcept{
	return sizeof(instruction_head)  + sizeof(T) + instruction::get_type_size_sum<Args...>(args...);
}

export
template <typename T, typename... Args>
	requires requires{
		requires !known_instruction<T> || requires(const T& instr, const Args&... args){
			{instr.get_vertex_count(args...)} -> std::convertible_to<std::uint32_t>;
		};
	}
constexpr instruction_head make_instruction_head(const T& instr, const Args&... args) noexcept{
	const auto required = instruction::get_instr_size<T, Args...>(args...);
	assert(required % instr_required_align == 0);

	const auto vtx = [&] -> std::uint32_t{
		if constexpr(known_instruction<T>){
			return instr.get_vertex_count(args...);
		} else{
			return 0;
		}
	}();

	const auto pmt = [&] -> std::uint32_t{
		if constexpr(known_instruction<T>){
			return instr.get_primitive_count(args...);
		} else{
			return 0;
		}
	}();


	return instruction_head{
		.type = instruction_type_of<T>,
		.size = static_cast<std::uint32_t>(required),
		.payload = {.draw = {.vertex_count = vtx, .primitive_count = pmt}}
	};
}


export
[[nodiscard]] FORCE_INLINE const instruction_head& get_instr_head(const void* p) noexcept{
	return *start_lifetime_as<instruction_head>(std::assume_aligned<instr_required_align>(p));
}


template <typename T, typename... Args>
	requires (std::is_trivially_copyable_v<T> && valid_consequent_argument<T, Args...>)
FORCE_INLINE void place_instr_at_impl(
	std::byte* const where,
	const instruction_head& head,
	const T& payload,
	const Args&... args
){
	const auto total_size = instruction::get_instr_size<T, Args...>(args...);

	assert(std::bit_cast<std::uintptr_t>(where) % instr_required_align == 0);
	auto pwhere = std::assume_aligned<instr_required_align>(where);

	std::memcpy(pwhere, &head, sizeof(instruction_head));
	pwhere += sizeof(instruction_head);
	std::memcpy(pwhere, &payload, sizeof(payload));

	if constexpr(sizeof...(args) > 0){
		static constexpr auto place_at = []<typename Ty>(std::byte*& w, const Ty& arg){
			if constexpr (std::ranges::contiguous_range<Ty>){
				static_assert(std::is_trivially_copyable_v<std::ranges::range_value_t<Ty>> && !std::ranges::range<std::ranges::range_value_t<Ty>>);
				const auto byte_size = sizeof(std::ranges::range_value_t<Ty>) * std::ranges::size(arg);
				std::memcpy(w, std::ranges::data(arg), byte_size);
				return w = std::assume_aligned<instr_required_align>(w + byte_size);
			}else{
				std::memcpy(w, &arg, sizeof(Ty));
				return w = std::assume_aligned<instr_required_align>(w + sizeof(Ty));
			}
		};

		pwhere += sizeof(instruction_head);

		[&]<std::size_t ... Idx>(std::index_sequence<Idx...>) FORCE_INLINE{
			(place_at.template operator()<Args>(pwhere, args), ...);
		}(std::make_index_sequence<sizeof...(Args)>{});
	}
}

export
template <known_instruction T, typename... Args>
	requires (std::is_trivially_copyable_v<T> && valid_consequent_argument<T, Args...>)
FORCE_INLINE void place_instruction_at(
	std::byte* const where,
	const T& payload,
	const Args&... args
) noexcept{
	instruction::place_instr_at_impl(
		where, instruction::make_instruction_head(payload, args...), payload, args...);
}

export
template <typename T>
	requires (std::is_trivially_copyable_v<T>)
FORCE_INLINE void place_ubo_update_at(
	std::byte* const where,
	const T& payload,
	const user_data_indices info
) noexcept{
	instruction::place_instr_at_impl(
		where, instruction_head{
			.type = instr_type::uniform_update,
			.size = get_instr_size<T>(),
			.payload = {.ubo = info}
		}, payload);
}

export
FORCE_INLINE void place_ubo_update_at(
	std::byte* const where,
	const std::byte* payload,
	const std::uint32_t payload_size,
	const user_data_indices info
) noexcept{
	const auto total_size = payload_size + static_cast<std::uint32_t>(sizeof(instruction_head));
	assert(std::bit_cast<std::uintptr_t>(where) % instr_required_align == 0);
	const auto pwhere = std::assume_aligned<instr_required_align>(where);

	const instruction_head head{
		.type = instr_type::uniform_update,
		.size = total_size,
		.payload = {.ubo = info}
	};
	std::memcpy(pwhere, &head, sizeof(instruction_head));
	std::memcpy(pwhere + sizeof(instruction_head), payload, payload_size);
}

export
[[nodiscard]] FORCE_INLINE inline std::span<const std::byte> get_payload_data_span(const std::byte* ptr_to_instr) noexcept{
	const auto head = get_instr_head(ptr_to_instr);
	const std::size_t ubo_size = head.get_payload_byte_size();
	return std::span{ptr_to_instr + sizeof(instruction_head), ubo_size};
}

export
FORCE_INLINE inline image_set_result set_image_index(void* instruction, image_view_history<>& cache) noexcept{
	auto& generic = *static_cast<primitive_generic*>(instruction);

	const auto view = generic.image.get_image_view();
	assert(std::bit_cast<std::uintptr_t>(view) != 0x00000000'ffffffffULL);
	const auto idx = cache.try_push(view);
	if(idx == image_view_history<>::max_cache_count) return image_set_result{view, false};
	generic.image.set_index(idx);
	return image_set_result{view, true};
}

export
struct user_data_entry{
	std::uint32_t size;
	std::uint32_t local_offset;
	std::uint32_t global_offset;
	std::uint32_t group_index;

	bool is_vertex_only;

	explicit operator bool() const noexcept{
		return size != 0;
	}

	[[nodiscard]] std::span<const std::byte> to_range(const std::byte* base_address) const noexcept{
		return {base_address + global_offset, size};
	}
};

export
struct user_data_entries{
	const std::byte* base_address;
	std::span<const user_data_entry> entries;

	explicit operator bool() const noexcept{
		return !entries.empty();
	}
};

export
struct user_data_identity_entry{
	type_identity_index id;
	user_data_entry entry;
};

export
template <typename Container = std::vector<user_data_identity_entry>>
struct user_data_index_table{
	static_assert(std::same_as<std::ranges::range_value_t<Container>, user_data_identity_entry>, "Container must have user_data_identity_entry as value_type");
	static_assert(std::ranges::contiguous_range<Container>, "Container must be contiguous");
	static_assert(std::ranges::sized_range<Container>, "Container must be contiguous");

	static constexpr bool is_allocator_aware = requires{
		typename Container::allocator_type;
	};

	static constexpr bool is_reservable = requires(Container& cont, std::size_t sz){
		cont.reserve();
	};

	using allocator_type = decltype([]{
		if constexpr (is_allocator_aware){
			return typename Container::allocator_type{};
		}else{
			return ;
		}
	}());

private:
	std::size_t required_capacity_{};
	Container entries{};

	template <typename ...Ts>
	void load(){
		auto push = [&]<typename Ty, std::size_t I>(std::size_t current_base_size){
			entries.push_back(user_data_identity_entry{
				.id = unstable_type_identity_of<Ty>(),
				.entry = {
					.size = static_cast<std::uint32_t>(sizeof(Ty)),
					.local_offset = static_cast<std::uint32_t>(required_capacity_ - current_base_size),
					.global_offset = static_cast<std::uint32_t>(required_capacity_),
					.group_index = I,
					.is_vertex_only = requires{
						typename Ty::vertex_only;
					}
				}
			});

			required_capacity_ += (sizeof(Ty) + instr_required_align - 1) / instr_required_align * instr_required_align;
		};

		[&]<std::size_t ...Idx>(std::index_sequence<Idx...>){
			([&]<typename T, std::size_t I>(){
				const auto cur_base = required_capacity_;

				[&]<std::size_t ...J>(std::index_sequence<J...>){
					(push.template operator()<std::tuple_element_t<J, T>, I>(cur_base), ...);
				}(std::make_index_sequence<std::tuple_size_v<T>>{});
			}.template operator()<Ts, Idx>(), ...);
		}(std::index_sequence_for<Ts...>());
	}

public:
	template <typename T>
	friend struct user_data_index_table;

	[[nodiscard]] user_data_index_table() = default;

	template <std::ranges::input_range InputRng, typename ...Ts>
		requires (std::convertible_to<std::ranges::range_value_t<InputRng>, user_data_identity_entry> && std::constructible_from<Container, Ts&&...>)
	[[nodiscard]] explicit(false) user_data_index_table(const InputRng& other, Ts&& ...container_args) : entries(std::forward<Ts>(container_args)...){
		if constexpr (is_reservable && std::ranges::sized_range<const InputRng&>){
			entries.reserve(std::ranges::size(other));
		}
		std::ranges::copy(other, std::back_inserter(entries));
		if(!std::ranges::empty(entries)){
			const user_data_identity_entry& last = *std::ranges::rbegin(entries);
			required_capacity_ = last.entry.global_offset + (last.entry.size + instr_required_align - 1) / instr_required_align * instr_required_align;
		}
	}

	template <typename ...Ts>
		requires (is_tuple_v<Ts> && ...)
	[[nodiscard]] explicit(false) user_data_index_table(
		const allocator_type& allocator,
		std::in_place_type_t<Ts>...
		) requires(is_allocator_aware) : entries(allocator){
		this->load<Ts...>();
	}

	template <typename ...Ts>
		requires (is_tuple_v<Ts> && ...)
	[[nodiscard]] explicit(false) user_data_index_table(
		std::in_place_type_t<Ts>...
	){
		this->load<Ts...>();
	}

	[[nodiscard]] std::uint32_t required_capacity() const noexcept{
		return required_capacity_;
	}

	[[nodiscard]] user_data_identity_entry* begin() noexcept{
		return std::ranges::data(entries);
	}

	[[nodiscard]] user_data_identity_entry* end() noexcept{
		return std::ranges::data(entries) + std::ranges::size(entries);
	}

	[[nodiscard]] const user_data_identity_entry* begin() const noexcept{
		return std::ranges::data(entries);
	}

	[[nodiscard]] const user_data_identity_entry* end() const noexcept{
		return std::ranges::data(entries) + std::ranges::size(entries);
	}

	[[nodiscard]] std::uint32_t group_size_at(const user_data_identity_entry* where) const noexcept{
		const auto end_ = end();
		assert(where < end_ && where > std::ranges::data(entries));

		const std::uint32_t base_offset{where->entry.group_index - where->entry.local_offset};
		for(auto p = where; p != end_; p++){
			if(p->entry.group_index != where->entry.group_index){
				return (p->entry.group_index - p->entry.local_offset) - base_offset;
			}
		}

		return required_capacity_ - base_offset;
	}


	[[nodiscard]] std::size_t size() const noexcept{
		return std::ranges::size(entries);
	}

	[[nodiscard]] bool empty() const noexcept{
		return std::ranges::empty(entries);
	}

	[[nodiscard]] const user_data_identity_entry* operator[](type_identity_index id) const noexcept{
		const auto beg = begin();
		const auto end = beg + size();
		return std::ranges::find(beg, end, id, &user_data_identity_entry::id);
	}

	template <typename T>
	[[nodiscard]] const user_data_identity_entry& at() const noexcept{
		auto ptr = (*this)[unstable_type_identity_of<T>()];
		assert(ptr != std::ranges::data(entries) + std::ranges::size(entries));
		return *ptr;
	}

	[[nodiscard]] const user_data_entry& operator[](std::uint32_t idx) const noexcept{
		assert(idx < size());
		return entries[idx].entry;
	}

	template <typename T>
	[[nodiscard]] FORCE_INLINE user_data_indices index_of() const noexcept{
		const user_data_identity_entry* ptr = (*this)[unstable_type_identity_of<T>()];
		assert(ptr < end());
		return {static_cast<std::uint32_t>(ptr - begin()), ptr->entry.group_index};
	}

	[[nodiscard]] FORCE_INLINE user_data_indices index_of(type_identity_index index) const noexcept{
		const user_data_identity_entry* ptr = (*this)[index];
		assert(ptr < end());
		return {static_cast<std::uint32_t>(ptr - begin()), ptr->entry.group_index};
	}

	[[nodiscard]] FORCE_INLINE user_data_indices index_of_checked(type_identity_index index) const{
		const user_data_identity_entry* ptr = (*this)[index];
		if(ptr >= end()){
			throw std::out_of_range("customized type index out of range");
		}
		return {static_cast<std::uint32_t>(ptr - begin()), ptr->entry.group_index};
	}

	auto get_entries() const noexcept {
		return std::span{begin(), size()};
	}

	auto get_entries_mut() noexcept {
		return std::span{begin(), size()};
	}

	void append(const user_data_index_table& other){
		if(std::ranges::empty(entries)){
			*this = other;
			return;
		}
		auto group_base = static_cast<user_data_identity_entry&>(*std::ranges::rbegin(entries)).entry.group_index + 1;
		if constexpr (is_reservable){
			entries.reserve(size() + other.size());
		}

		for (user_data_identity_entry entry : other.entries){
			entry.entry.group_index += group_base;
			entry.entry.global_offset += required_capacity_;
			entries.push_back(entry);
		}
		required_capacity_ += other.required_capacity_;
	}
};

}



export
template <typename T>
struct quad_group{
	T v00{}, v10{}, v01{}, v11{};

	[[nodiscard]] FORCE_INLINE constexpr quad_group() = default;

	[[nodiscard]] FORCE_INLINE explicit(false) constexpr quad_group(const T& v) noexcept : v00(v), v10(v), v01(v), v11(v){}

	template <typename Ty>
		requires (std::constructible_from<T, const Ty&>)
	[[nodiscard]] FORCE_INLINE explicit(false) constexpr quad_group(const Ty& v) noexcept : v00(v), v10(v), v01(v), v11(v){}

	template <typename Ty>
		requires (std::constructible_from<T, const Ty&>)
	[[nodiscard]] FORCE_INLINE explicit(false) constexpr quad_group(const quad_group<Ty>& v) noexcept : v00(v.v00), v10(v.v10), v01(v.v01), v11(v.v11){}

	template <typename T1, typename T2, typename T3, typename T4>
		requires (std::constructible_from<T, const T1&> && std::constructible_from<T, const T2&> && std::constructible_from<T, const T3&> && std::constructible_from<T, const T4&>)
	[[nodiscard]] FORCE_INLINE constexpr quad_group(const T1& v00, const T2& v10, const T3& v01, const T4& v11) noexcept
			: v00(v00),
			  v10(v10),
			  v01(v01),
			  v11(v11){
	}

	[[nodiscard]] FORCE_INLINE constexpr quad_group(const T& v00, const T& v10, const T& v01, const T& v11) noexcept
		: v00(v00),
		  v10(v10),
		  v01(v01),
		  v11(v11){
	}

	constexpr bool operator==(const quad_group&) const noexcept = default;

	[[nodiscard]] FORCE_INLINE friend constexpr quad_group operator+(const quad_group& lhs, const T& rhs) noexcept{
		return {lhs.v00 + rhs, lhs.v10 + rhs, lhs.v01 + rhs, lhs.v11 + rhs};
	}

	[[nodiscard]] FORCE_INLINE friend constexpr quad_group operator-(const quad_group& lhs, const T& rhs) noexcept{
		return {lhs.v00 - rhs, lhs.v10 - rhs, lhs.v01 - rhs, lhs.v11 - rhs};
	}

	[[nodiscard]] FORCE_INLINE friend constexpr quad_group operator*(const quad_group& lhs, const T& rhs) noexcept{
		return {lhs.v00 * rhs, lhs.v10 * rhs, lhs.v01 * rhs, lhs.v11 * rhs};
	}

	[[nodiscard]] FORCE_INLINE friend constexpr quad_group operator/(const quad_group& lhs, const T& rhs) noexcept{
		return {lhs.v00 / rhs, lhs.v10 / rhs, lhs.v01 / rhs, lhs.v11 / rhs};
	}

	[[nodiscard]] FORCE_INLINE friend constexpr quad_group operator+(const T& lhs, const quad_group& rhs) noexcept{
		return {lhs + rhs.v00, lhs + rhs.v10, lhs + rhs.v01, lhs + rhs.v11};
	}

	[[nodiscard]] FORCE_INLINE friend constexpr quad_group operator*(const T& lhs, const quad_group& rhs) noexcept{
		return {lhs * rhs.v00, lhs * rhs.v10, lhs * rhs.v01, lhs * rhs.v11};
	}

	FORCE_INLINE constexpr quad_group& operator+=(const T& rhs) noexcept{
		v00 += rhs;
		v10 += rhs;
		v01 += rhs;
		v11 += rhs;
		return *this;
	}

	FORCE_INLINE constexpr quad_group& operator-=(const T& rhs) noexcept{
		v00 -= rhs;
		v10 -= rhs;
		v01 -= rhs;
		v11 -= rhs;
		return *this;
	}

	FORCE_INLINE constexpr quad_group& operator*=(const T& rhs) noexcept{
		v00 *= rhs;
		v10 *= rhs;
		v01 *= rhs;
		v11 *= rhs;
		return *this;
	}

	FORCE_INLINE constexpr quad_group& operator/=(const T& rhs) noexcept{
		v00 /= rhs;
		v10 /= rhs;
		v01 /= rhs;
		v11 /= rhs;
		return *this;
	}
};


}
