module;

#if defined(__has_include) && __has_include(<vulkan/vulkan.h>)
#include <vulkan/vulkan.h>
#define BACKEND_HAS_VULKAN
#endif

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.graphic.draw.instruction.general;

import std;
import mo_yanxi.meta_programming;
import binary_trace;

namespace mo_yanxi::graphic::draw::instruction{

export template<typename Rng, typename T>
concept contiguous_range_of = std::ranges::contiguous_range<Rng> && std::same_as<std::ranges::range_value_t<Rng>, T>;

export constexpr inline std::size_t instr_required_align = 16;

static_assert(std::has_single_bit(instr_required_align));
export FORCE_INLINE std::byte* find_aligned_on(std::byte* where) noexcept{
	const auto A = std::bit_cast<std::uintptr_t>(where);
	static constexpr std::size_t M = instr_required_align;
	static constexpr std::size_t Mask = M - 1;
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
		if(src) {
			std::destroy_n(src, size());
			::operator delete(src, size(), std::align_val_t{align});
		}
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
		if(src) {
			std::destroy_n(src, size());
			::operator delete(src, size(), std::align_val_t{align});
		}
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
		auto* next = new(p) std::byte[new_size];
		std::memcpy(next, src, size());
		::operator delete(src, size(), std::align_val_t{align});
		src = next;
		dst = src + new_size;
	}

	void set_size(const std::size_t new_size){
		if(size() == new_size){
			return;
		}
		const auto p = ::operator new(new_size, std::align_val_t{align});
		auto* next = new(p) std::byte[new_size];
		::operator delete(src, size(), std::align_val_t{align});
		src = next;
		dst = src + new_size;
	}
};


export
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
struct gpu_vertex_data_advance_data{
	std::uint32_t index;
};

export
struct user_data_indices{
	std::uint32_t index;
	std::uint32_t group_index;
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
	std::uint32_t payload_size;
	dispatch_info_payload<DrawInfoTy> payload;
};

// --- Changed Section ---

export
enum struct state_push_type{
	idempotent,
	non_idempotent
};

export
struct state_push_config{
	state_push_type type;
};
// -----------------------

}


namespace mo_yanxi::graphic::draw{
namespace instruction{

#ifndef MO_YANXI_GRAPHIC_DRAW_INSTRUCTION_IMAGE_HANDLE_TYPE
#if defined(__has_include) && __has_include(<vulkan/vulkan.h>)
	export using image_handle_t = VkImageView;
#else
	export using image_handle_t = void*;
#endif
#else
export using image_handle_t = MO_YANXI_GRAPHIC_DRAW_INSTRUCTION_IMAGE_HANDLE_TYPE;
static_assert(sizeof(image_handle_t) == 8)
#endif

export union image_view{
	image_handle_t view;
	std::uint32_t index;

	void set_view(image_handle_t view) noexcept{
		new(&this->view) image_handle_t(view);
	}

	void set_index(std::uint32_t idx) noexcept{
		new(&this->index) std::uint32_t(idx);
	}

	[[nodiscard]] image_handle_t get_image_view() const noexcept{
		return view;
	}
};

export struct draw_mode{
	std::uint32_t value;
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
struct is_valid_consequent_argument : std::false_type{};

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
constexpr std::size_t get_payload_size(const Args& ...args) noexcept{
	return sizeof(T) + instruction::get_type_size_sum<Args...>(args...);
}

export
template <typename T, typename... Args>
	requires requires{
		requires !known_instruction<T> || requires(const T& instr, const Args&... args){
			{instr.get_vertex_count(args...)} -> std::convertible_to<std::uint32_t>;
		};
	}
constexpr instruction_head make_instruction_head(const T& instr, const Args&... args) noexcept{
	const auto required = instruction::get_payload_size<T, Args...>(args...);
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
		.payload_size = static_cast<std::uint32_t>(required),
		.payload = {.draw = {.vertex_count = vtx, .primitive_count = pmt}}
	};
}

template <typename T, typename... Args>
	requires (std::is_trivially_copyable_v<T> && valid_consequent_argument<T, Args...>)
FORCE_INLINE void place_instr_at_impl(
	std::byte* const where,
	const T& payload,
	const Args&... args
) noexcept {
	const auto total_size = instruction::get_payload_size<T, Args...>(args...);

	assert(std::bit_cast<std::uintptr_t>(where) % instr_required_align == 0);
	auto pwhere = std::assume_aligned<instr_required_align>(where);
	std::memcpy(pwhere, &payload, sizeof(payload));

	if constexpr(sizeof...(args) > 0){
		pwhere += sizeof(payload);

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

		[&]<std::size_t ... Idx>(std::index_sequence<Idx...>) FORCE_INLINE{
			((pwhere = place_at.template operator()<Args>(pwhere, args)), ...);
		}(std::make_index_sequence<sizeof...(Args)>{});
	}
}

export
template <known_instruction T, typename... Args>
	requires (std::is_trivially_copyable_v<T> && valid_consequent_argument<T, Args...>)
FORCE_INLINE [[nodiscard]] instruction_head place_instruction_at(
	std::byte* const where,
	const T& payload,
	const Args&... args
) noexcept{
	instruction::place_instr_at_impl(where, payload, args...);
	return instruction::make_instruction_head(payload, args...);
}

export
template <typename T>
	requires (std::is_trivially_copyable_v<T>)
FORCE_INLINE [[nodiscard]] instruction_head place_ubo_update_at(
	std::byte* const where,
	const T& payload,
	const user_data_indices info
) noexcept{
	instruction::place_instr_at_impl(where, payload);
	return instruction_head{
		.type = instr_type::uniform_update,
		.payload_size = get_payload_size<T>(),
		.payload = {.ubo = info}
	};
}

export
FORCE_INLINE [[nodiscard]] instruction_head place_ubo_update_at(
	std::byte* const where,
	const std::byte* payload,
	const std::uint32_t payload_size,
	const user_data_indices info
) noexcept{
	assert(std::bit_cast<std::uintptr_t>(where) % instr_required_align == 0);
	const auto pwhere = std::assume_aligned<instr_required_align>(where);
	std::memcpy(pwhere, payload, payload_size);

	return instruction_head{
		.type = instr_type::uniform_update,
		.payload_size = payload_size,
		.payload = {.ubo = info}
	};

}

export
using state_tag = binary_diff_trace::tag;

export
template <typename L, typename R>
	requires (sizeof(L) == sizeof(state_tag::major) && sizeof(R) == sizeof(state_tag::minor))
FORCE_INLINE CONST_FN [[nodiscard]] constexpr state_tag make_state_tag(L major, R minor) noexcept {
	return {std::bit_cast<decltype(state_tag::major)>(major), std::bit_cast<decltype(state_tag::minor)>(minor)};
}

export
struct batch_backend_interface{
	using host_impl_ptr = void*;

	using function_signature_buffer_acquire = void(host_impl_ptr, instruction_head, const std::byte* payload);
	using function_signature_update_state_entry = void(host_impl_ptr, state_push_config config, state_tag tag, std::span<const std::byte> payload, unsigned offset);

private:
	host_impl_ptr host;
	std::add_pointer_t<function_signature_buffer_acquire> fptr_push;
	std::add_pointer_t<function_signature_update_state_entry> state_handle;


public:
	[[nodiscard]] constexpr batch_backend_interface() = default;

	template <typename HostT,
	std::invocable<HostT&, instruction_head, const std::byte*> AcqFn,
	std::invocable<HostT&, state_push_config, state_tag, std::span<const std::byte>, unsigned> StateHandleFn
	>
	[[nodiscard]] constexpr batch_backend_interface(
		HostT& host,
		AcqFn,
		StateHandleFn
	) noexcept : host(std::addressof(host)), fptr_push(+[](host_impl_ptr host, instruction_head head, const std::byte* instr){
		return AcqFn::operator()(*static_cast<HostT*>(host), head, instr);
	}), state_handle(+[](host_impl_ptr host, state_push_config config, state_tag tag, std::span<const std::byte> payload, unsigned offset) static {
		StateHandleFn::operator()(*static_cast<HostT*>(host), config, tag, payload, offset);
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

	void push(instruction_head head, const std::byte* instr) const {
		CHECKED_ASSUME(fptr_push != nullptr);
		fptr_push(host, head, instr);
	}


	void update_state(state_push_config config, state_tag tag, std::span<const std::byte> payload, unsigned offset = 0) const{
		state_handle(host, config, tag, payload, offset);
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
