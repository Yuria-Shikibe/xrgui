module;

#if defined(__has_include) && __has_include(<vulkan/vulkan.h>)
#include <vulkan/vulkan.h>
#define BACKEND_HAS_VULKAN
#endif

#if defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1) || defined(__SSE__)
#define XRGUI_G2D_QUAD_GROUP_HAS_SSE 1
#include <immintrin.h>
#endif

#if defined(__FMA__)
#define XRGUI_G2D_QUAD_GROUP_HAS_FMA 1
#endif

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.graphic.g2d.general;

export import mo_yanxi.graphic.image_view_registry;

import std;
import mo_yanxi.meta_programming;
import mo_yanxi.concepts;
import mo_yanxi.binary_trace;
import mo_yanxi.aligned_allocator;
import mo_yanxi.raw_byte_buffer;

namespace mo_yanxi::graphic::g2d{

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
	using storage_type = raw_buffer<std::byte, aligned_allocator<std::byte, align>, std::size_t>;

	storage_type storage_{};

	static std::size_t align_size_(std::size_t byte_size) noexcept{
		return ((byte_size + (align - 1)) / align) * align;
	}

public:
	[[nodiscard]] instruction_buffer() noexcept = default;
	[[nodiscard]] explicit instruction_buffer(std::size_t byte_size){
		const auto actual_size = align_size_(byte_size);
		storage_.resize_and_overwrite(actual_size, [](std::byte* data, std::size_t, std::size_t requested_size) noexcept{
			if(requested_size != 0){
				std::memset(data, 0, requested_size);
			}
			return requested_size;
		});
	}

	~instruction_buffer() = default;

	[[nodiscard]] FORCE_INLINE CONST_FN std::size_t size() const noexcept{
		return storage_.size();
	}

	[[nodiscard]] FORCE_INLINE CONST_FN std::byte* begin() const noexcept{
		return std::assume_aligned<align>(const_cast<std::byte*>(storage_.data()));
	}

	[[nodiscard]] FORCE_INLINE CONST_FN std::byte* data() const noexcept{
		return std::assume_aligned<align>(const_cast<std::byte*>(storage_.data()));
	}

	void clear() const noexcept{
		if(size() != 0){
			std::memset(begin(), 0, size());
		}
	}

	[[nodiscard]] FORCE_INLINE CONST_FN std::byte* end() const noexcept{
		auto* const first = const_cast<std::byte*>(storage_.data());
		return first == nullptr ? nullptr : std::assume_aligned<align>(first + storage_.size());
	}

	instruction_buffer(const instruction_buffer& other)
		: instruction_buffer(other.size()){
		if(size() != 0){
			std::memcpy(data(), other.data(), size());
		}
	}

	instruction_buffer(instruction_buffer&& other) noexcept = default;

	instruction_buffer& operator=(const instruction_buffer& other){
		if(this == &other) return *this;
		*this = instruction_buffer{other};
		return *this;
	}

	instruction_buffer& operator=(instruction_buffer&& other) noexcept = default;

	template <bool noCheck = false>
	void resize(const std::size_t new_size){
		if constexpr (!noCheck){
			if(size() == new_size){
				return;
			}
		}
		storage_.resize_and_overwrite(new_size, [](std::byte*, std::size_t, std::size_t requested_size) noexcept{
			return requested_size;
		});
	}

	void set_size(const std::size_t new_size){
		if(size() == new_size){
			return;
		}
		storage_.release();
		storage_.resize_and_overwrite(new_size, [](std::byte*, std::size_t, std::size_t requested_size) noexcept{
			return requested_size;
		});
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
	return ((get_size(args)) + ... + 0uz);
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


export
enum struct state_commit_mode : std::uint8_t{
	accumulate,
	emit_delta,
};

export
enum struct state_boundary_mode : std::uint8_t{
	defer,
	force_section_break,
};

export
enum struct depth_op_type : std::uint8_t{
	noop,
	incr,
	decr
};

export
struct state_push_config{
	state_commit_mode commit_mode{state_commit_mode::accumulate};
	state_boundary_mode boundary_mode{state_boundary_mode::defer};
	depth_op_type depth_op{depth_op_type::noop};
	std::bitset<32> to_clear{};

	[[nodiscard]] constexpr bool tracks_persistent_state() const noexcept{
		return commit_mode == state_commit_mode::accumulate;
	}

	[[nodiscard]] constexpr bool emits_immediate_delta() const noexcept{
		return commit_mode == state_commit_mode::emit_delta;
	}

	[[nodiscard]] constexpr bool forces_section_break() const noexcept{
		return boundary_mode == state_boundary_mode::force_section_break;
	}
	};


}


namespace mo_yanxi::graphic::g2d{

export struct draw_mode{
	std::uint32_t value;
};

export struct alignas(instr_required_align) primitive_generic{
	texture_binding image{};
	draw_mode mode{};
	float depth{};
};

static_assert(sizeof(primitive_generic) == 16);

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
template <typename T, typename Ctx>
concept known_meta_instruction = std::invocable<const T&, Ctx&>;


export
template <typename T, typename... Args>
	requires (std::is_trivially_copyable_v<T> && valid_consequent_argument<T, Args...>)
constexpr std::size_t get_payload_size(const Args& ...args) noexcept{
	return sizeof(T) + get_type_size_sum<Args...>(args...);
}

export
template <typename T, typename... Args>
	requires requires{
		requires !known_instruction<T> || requires(const T& instr, const Args&... args){
			{instr.get_vertex_count(args...)} -> std::convertible_to<std::uint32_t>;
		};
	}
constexpr instruction_head make_instruction_head(const T& instr, const Args&... args) noexcept{
	const auto required = get_payload_size<T, Args...>(args...);
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
	const auto total_size = get_payload_size<T, Args...>(args...);

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
	place_instr_at_impl(where, payload, args...);
	return make_instruction_head(payload, args...);
}

export
template <typename T>
	requires (std::is_trivially_copyable_v<T>)
FORCE_INLINE [[nodiscard]] instruction_head place_ubo_update_at(
	std::byte* const where,
	const T& payload,
	const user_data_indices info
) noexcept{
	place_instr_at_impl(where, payload);
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
struct emit_t;

export
template <typename Sink, typename Instr>
concept directly_emittable_draw_instruction = requires(Sink& sink, Instr&& instr){
	sink(std::forward<Instr>(instr));
};

export
template <typename Sink, typename Instr>
concept cpo_emittable_draw_instruction = requires(Instr&& instr, Sink& sink){
	std::forward<Instr>(instr)(std::declval<const emit_t&>(), sink);
};

export
struct emit_t{
	template <typename Sink, typename Instr>
		requires (known_instruction<std::remove_cvref_t<Instr>> && directly_emittable_draw_instruction<Sink, Instr>)
	FORCE_INLINE constexpr decltype(auto) operator()(Sink& sink, Instr&& instr) const
		noexcept(noexcept(sink(std::forward<Instr>(instr)))){
		ATTR_FORCEINLINE_SENTENCE
		ADAPTED_MUST_TAIL
		return sink(std::forward<Instr>(instr));
	}

	template <typename Sink, typename Instr>
		requires (!known_instruction<std::remove_cvref_t<Instr>> && cpo_emittable_draw_instruction<Sink, Instr>)
	FORCE_INLINE constexpr decltype(auto) operator()(Sink& sink, Instr&& instr) const
		noexcept(noexcept(std::forward<Instr>(instr)(*this, sink))){
		ATTR_FORCEINLINE_SENTENCE
		ADAPTED_MUST_TAIL
		return std::forward<Instr>(instr)(*this, sink);
	}
};

export inline constexpr emit_t emit{};

export
template <typename Derived>
struct emit_stream_sink{
	template <typename Instr>
		requires requires(Derived& sink, Instr&& instr){
		emit(sink, std::forward<Instr>(instr));
		}
	FORCE_INLINE friend Derived& operator<<(Derived& sink, Instr&& instr)
		noexcept(noexcept(emit(sink, std::forward<Instr>(instr)))){
		ATTR_FORCEINLINE_SENTENCE
		emit(sink, std::forward<Instr>(instr));
		return sink;
	}
};

template <typename Rng, typename T>
concept batch_push_range_of =
	std::ranges::contiguous_range<Rng> &&
	std::ranges::sized_range<Rng> &&
	std::same_as<std::remove_cv_t<std::ranges::range_value_t<Rng>>, T>;

export
struct batch_push_view{
	std::span<const instruction_head> heads{};

	//TODO directly pass raw pointer??
	std::span<const std::byte> payload{};

	void operator()(emit_t, auto& sink) const {
		sink(heads, payload.data());
	}
};

export
[[nodiscard]] FORCE_INLINE constexpr batch_push_view batch_push(
	const std::span<const instruction_head> heads,
	const std::span<const std::byte> payload
) noexcept{
	return {heads, payload};
}

export
[[nodiscard]] FORCE_INLINE constexpr batch_push_view batch_push(
	const std::span<const instruction_head> heads,
	const std::byte* payload
) noexcept{
	const auto byte_size = std::ranges::fold_left(heads, std::size_t{}, [](std::size_t size, const instruction_head& head) {
		return size + head.payload_size;
	});
	return {heads, {payload, byte_size}};
}

export
template <batch_push_range_of<instruction_head> HeadRng, batch_push_range_of<std::byte> PayloadRng>
	requires std::ranges::borrowed_range<HeadRng&&> && std::ranges::borrowed_range<PayloadRng&&>

[[nodiscard]] FORCE_INLINE constexpr batch_push_view batch_push(
	HeadRng&& heads,
	PayloadRng&& payload
) noexcept{
	return {
		std::span<const instruction_head>{
			std::ranges::data(std::forward<HeadRng>(heads)), std::ranges::size(std::forward<HeadRng>(heads))},
		std::span<const std::byte>{
			std::ranges::data(std::forward<PayloadRng>(payload)), std::ranges::size(std::forward<PayloadRng>(payload))}
	};
}

export
template <typename L, typename R = std::uint32_t>
	requires (sizeof(L) == sizeof(state_tag::major) && sizeof(R) == sizeof(state_tag::minor))
FORCE_INLINE CONST_FN [[nodiscard]] constexpr state_tag make_state_tag(L major, R minor = {}) noexcept {
	return {std::bit_cast<decltype(state_tag::major)>(major), std::bit_cast<decltype(state_tag::minor)>(minor)};
}

export
struct render_data_update_head{
	bool blit_resource;
	unsigned index;
};

export
struct batch_backend_interface{
	using host_impl_ptr = void*;

	using function_signature_push = void(host_impl_ptr, instruction_head, const std::byte* payload);
	using function_signature_update_state_entry = void(host_impl_ptr, state_push_config config, state_tag tag, std::span<const std::byte> payload, unsigned offset);
	using function_signature_push_batch = void(host_impl_ptr, std::span<const instruction_head> heads, const std::byte* payload);

private:
	host_impl_ptr host;
	std::add_pointer_t<function_signature_push> fptr_push;
	std::add_pointer_t<function_signature_push_batch> fptr_push_batch;
	std::add_pointer_t<function_signature_update_state_entry> state_handle;

public:
	[[nodiscard]] constexpr batch_backend_interface() = default;

	template <typename HostT,
	std::invocable<HostT&, instruction_head, const std::byte*> PushFn,
	std::invocable<HostT&, std::span<const instruction_head>, const std::byte*> BatchPushFn,
	std::invocable<HostT&, state_push_config, state_tag, std::span<const std::byte>, unsigned> StateHandleFn
	>
	[[nodiscard]] constexpr batch_backend_interface(
		HostT& host,
		PushFn,
		BatchPushFn,
		StateHandleFn
	) noexcept : host(std::addressof(host)), fptr_push(+[](host_impl_ptr host, instruction_head head, const std::byte* instr){
		ATTR_FORCEINLINE_SENTENCE
			return PushFn::operator()(*static_cast<HostT*>(host), head, instr);
	}), fptr_push_batch(+[](host_impl_ptr host, std::span<const instruction_head> heads, const std::byte* payload) static {
		ATTR_FORCEINLINE_SENTENCE
			BatchPushFn::operator()(*static_cast<HostT*>(host), heads, payload);
	}), state_handle(+[](host_impl_ptr host, state_push_config config, state_tag tag, std::span<const std::byte> payload, unsigned offset) static {
		ATTR_FORCEINLINE_SENTENCE
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


	void push(std::span<const instruction_head> heads, const std::byte* instr) const {
		CHECKED_ASSUME(fptr_push != nullptr);
		fptr_push_batch(host, heads, instr);
	}


	void update_state(state_push_config config, state_tag tag, std::span<const std::byte> payload, unsigned offset = 0) const{
		state_handle(host, config, tag, payload, offset);
	}
};


export
template <typename T>
struct alignas(instr_required_align) quad_group{
	using value_type = T;
	T values[4]{};

	[[nodiscard]] FORCE_INLINE constexpr quad_group() = default;



	template <typename Ty>
		requires (std::constructible_from<T, const Ty&> && !std::convertible_to<const Ty&, quad_group> && !spec_of<Ty, quad_group>)
	[[nodiscard]] FORCE_INLINE explicit(false) constexpr quad_group(const Ty& v) noexcept : values{T(v), T(v), T(v), T(v)}{}

	template <typename Ty>
		requires (std::constructible_from<T, const typename std::remove_cvref_t<Ty>::value_type&> && spec_of<std::remove_cvref_t<Ty>, quad_group>)
	[[nodiscard]] FORCE_INLINE explicit(!std::convertible_to<const typename std::remove_cvref_t<Ty>::value_type&, T> && !std::derived_from<T, typename std::remove_cvref_t<Ty>::value_type>) constexpr
	quad_group(Ty&& v) noexcept : values{
		T(std::forward_like<Ty>(v.values[0])),
		T(std::forward_like<Ty>(v.values[1])),
		T(std::forward_like<Ty>(v.values[2])),
		T(std::forward_like<Ty>(v.values[3]))
	}{
	}

	template <typename T1, typename T2, typename T3, typename T4>
		requires (std::constructible_from<T, const T1&> && std::constructible_from<T, const T2&> && std::constructible_from<T, const T3&> && std::constructible_from<T, const T4&>)
	[[nodiscard]] FORCE_INLINE constexpr quad_group(const T1& v00, const T2& v10, const T3& v01, const T4& v11) noexcept
			: values{T(v00), T(v10), T(v01), T(v11)}{
	}

	[[nodiscard]] FORCE_INLINE constexpr quad_group(const T& v00, const T& v10, const T& v01, const T& v11) noexcept
		: values{T(v00), T(v10), T(v01), T(v11)}{
	}

	constexpr bool operator==(const quad_group&) const noexcept = default;

	template <std::integral Index>
	[[nodiscard]] FORCE_INLINE constexpr T& operator[](const Index index) noexcept{
		return values[static_cast<std::uint32_t>(index) & 0b11U];
	}

	template <std::integral Index>
	[[nodiscard]] FORCE_INLINE constexpr const T& operator[](const Index index) const noexcept{
		return values[static_cast<std::uint32_t>(index) & 0b11U];
	}

private:
	[[nodiscard]] FORCE_INLINE static constexpr float fma_scalar_(const float a, const float b, const float c) noexcept
		requires std::same_as<T, float>{
		if(std::is_constant_evaluated()){
			return a * b + c;
		}
		return std::fma(a, b, c);
	}

public:
#if defined(XRGUI_G2D_QUAD_GROUP_HAS_SSE)
	[[nodiscard]] FORCE_INLINE __m128 load_m128() const noexcept requires std::same_as<T, float>{
		return _mm_load_ps(values);
	}

	FORCE_INLINE void store_m128(const __m128 v) noexcept requires std::same_as<T, float>{
		_mm_store_ps(values, v);
	}

	[[nodiscard]] FORCE_INLINE static quad_group from_m128(const __m128 v) noexcept requires std::same_as<T, float>{
		quad_group rst;
		rst.store_m128(v);
		return rst;
	}
#endif

	[[nodiscard]] FORCE_INLINE friend constexpr quad_group operator+(const quad_group& lhs, const quad_group& rhs) noexcept
		requires std::same_as<T, float>{
#if defined(XRGUI_G2D_QUAD_GROUP_HAS_SSE)
		if(!std::is_constant_evaluated()){
			return from_m128(_mm_add_ps(lhs.load_m128(), rhs.load_m128()));
		}
#endif
		return {lhs[0] + rhs[0], lhs[1] + rhs[1], lhs[2] + rhs[2], lhs[3] + rhs[3]};
	}

	[[nodiscard]] FORCE_INLINE friend constexpr quad_group operator-(const quad_group& lhs, const quad_group& rhs) noexcept
		requires std::same_as<T, float>{
#if defined(XRGUI_G2D_QUAD_GROUP_HAS_SSE)
		if(!std::is_constant_evaluated()){
			return from_m128(_mm_sub_ps(lhs.load_m128(), rhs.load_m128()));
		}
#endif
		return {lhs[0] - rhs[0], lhs[1] - rhs[1], lhs[2] - rhs[2], lhs[3] - rhs[3]};
	}

	[[nodiscard]] FORCE_INLINE friend constexpr quad_group operator*(const quad_group& lhs, const quad_group& rhs) noexcept
		requires std::same_as<T, float>{
#if defined(XRGUI_G2D_QUAD_GROUP_HAS_SSE)
		if(!std::is_constant_evaluated()){
			return from_m128(_mm_mul_ps(lhs.load_m128(), rhs.load_m128()));
		}
#endif
		return {lhs[0] * rhs[0], lhs[1] * rhs[1], lhs[2] * rhs[2], lhs[3] * rhs[3]};
	}

	[[nodiscard]] FORCE_INLINE friend constexpr quad_group operator/(const quad_group& lhs, const quad_group& rhs) noexcept
		requires std::same_as<T, float>{
#if defined(XRGUI_G2D_QUAD_GROUP_HAS_SSE)
		if(!std::is_constant_evaluated()){
			return from_m128(_mm_div_ps(lhs.load_m128(), rhs.load_m128()));
		}
#endif
		return {lhs[0] / rhs[0], lhs[1] / rhs[1], lhs[2] / rhs[2], lhs[3] / rhs[3]};
	}

	[[nodiscard]] FORCE_INLINE friend constexpr quad_group operator+(const quad_group& lhs, const T& rhs) noexcept{
#if defined(XRGUI_G2D_QUAD_GROUP_HAS_SSE)
		if constexpr(std::same_as<T, float>){
			if(!std::is_constant_evaluated()){
				return from_m128(_mm_add_ps(lhs.load_m128(), _mm_set1_ps(rhs)));
			}
		}
#endif
		return {lhs[0] + rhs, lhs[1] + rhs, lhs[2] + rhs, lhs[3] + rhs};
	}

	[[nodiscard]] FORCE_INLINE friend constexpr quad_group operator-(const quad_group& lhs, const T& rhs) noexcept{
#if defined(XRGUI_G2D_QUAD_GROUP_HAS_SSE)
		if constexpr(std::same_as<T, float>){
			if(!std::is_constant_evaluated()){
				return from_m128(_mm_sub_ps(lhs.load_m128(), _mm_set1_ps(rhs)));
			}
		}
#endif
		return {lhs[0] - rhs, lhs[1] - rhs, lhs[2] - rhs, lhs[3] - rhs};
	}

	[[nodiscard]] FORCE_INLINE friend constexpr quad_group operator*(const quad_group& lhs, const T& rhs) noexcept{
#if defined(XRGUI_G2D_QUAD_GROUP_HAS_SSE)
		if constexpr(std::same_as<T, float>){
			if(!std::is_constant_evaluated()){
				return from_m128(_mm_mul_ps(lhs.load_m128(), _mm_set1_ps(rhs)));
			}
		}
#endif
		return {lhs[0] * rhs, lhs[1] * rhs, lhs[2] * rhs, lhs[3] * rhs};
	}

	[[nodiscard]] FORCE_INLINE friend constexpr quad_group operator/(const quad_group& lhs, const T& rhs) noexcept{
#if defined(XRGUI_G2D_QUAD_GROUP_HAS_SSE)
		if constexpr(std::same_as<T, float>){
			if(!std::is_constant_evaluated()){
				return from_m128(_mm_div_ps(lhs.load_m128(), _mm_set1_ps(rhs)));
			}
		}
#endif
		return {lhs[0] / rhs, lhs[1] / rhs, lhs[2] / rhs, lhs[3] / rhs};
	}

	[[nodiscard]] FORCE_INLINE friend constexpr quad_group operator+(const T& lhs, const quad_group& rhs) noexcept{
#if defined(XRGUI_G2D_QUAD_GROUP_HAS_SSE)
		if constexpr(std::same_as<T, float>){
			if(!std::is_constant_evaluated()){
				return from_m128(_mm_add_ps(_mm_set1_ps(lhs), rhs.load_m128()));
			}
		}
#endif
		return {lhs + rhs[0], lhs + rhs[1], lhs + rhs[2], lhs + rhs[3]};
	}

	[[nodiscard]] FORCE_INLINE friend constexpr quad_group operator-(const T& lhs, const quad_group& rhs) noexcept{
#if defined(XRGUI_G2D_QUAD_GROUP_HAS_SSE)
		if constexpr(std::same_as<T, float>){
			if(!std::is_constant_evaluated()){
				return from_m128(_mm_sub_ps(_mm_set1_ps(lhs), rhs.load_m128()));
			}
		}
#endif
		return {lhs - rhs[0], lhs - rhs[1], lhs - rhs[2], lhs - rhs[3]};
	}

	[[nodiscard]] FORCE_INLINE friend constexpr quad_group operator*(const T& lhs, const quad_group& rhs) noexcept{
#if defined(XRGUI_G2D_QUAD_GROUP_HAS_SSE)
		if constexpr(std::same_as<T, float>){
			if(!std::is_constant_evaluated()){
				return from_m128(_mm_mul_ps(_mm_set1_ps(lhs), rhs.load_m128()));
			}
		}
#endif
		return {lhs * rhs[0], lhs * rhs[1], lhs * rhs[2], lhs * rhs[3]};
	}

	[[nodiscard]] FORCE_INLINE friend constexpr quad_group operator/(const T& lhs, const quad_group& rhs) noexcept{
#if defined(XRGUI_G2D_QUAD_GROUP_HAS_SSE)
		if constexpr(std::same_as<T, float>){
			if(!std::is_constant_evaluated()){
				return from_m128(_mm_div_ps(_mm_set1_ps(lhs), rhs.load_m128()));
			}
		}
#endif
		return {lhs / rhs[0], lhs / rhs[1], lhs / rhs[2], lhs / rhs[3]};
	}

	FORCE_INLINE constexpr quad_group& operator+=(const quad_group& rhs) noexcept requires std::same_as<T, float>{
		return *this = *this + rhs;
	}

	FORCE_INLINE constexpr quad_group& operator-=(const quad_group& rhs) noexcept requires std::same_as<T, float>{
		return *this = *this - rhs;
	}

	FORCE_INLINE constexpr quad_group& operator*=(const quad_group& rhs) noexcept requires std::same_as<T, float>{
		return *this = *this * rhs;
	}

	FORCE_INLINE constexpr quad_group& operator/=(const quad_group& rhs) noexcept requires std::same_as<T, float>{
		return *this = *this / rhs;
	}

	FORCE_INLINE constexpr quad_group& operator+=(const T& rhs) noexcept{
#if defined(XRGUI_G2D_QUAD_GROUP_HAS_SSE)
		if constexpr(std::same_as<T, float>){
			if(!std::is_constant_evaluated()){
				store_m128(_mm_add_ps(load_m128(), _mm_set1_ps(rhs)));
				return *this;
			}
		}
#endif
		values[0] += rhs;
		values[1] += rhs;
		values[2] += rhs;
		values[3] += rhs;
		return *this;
	}

	FORCE_INLINE constexpr quad_group& operator-=(const T& rhs) noexcept{
#if defined(XRGUI_G2D_QUAD_GROUP_HAS_SSE)
		if constexpr(std::same_as<T, float>){
			if(!std::is_constant_evaluated()){
				store_m128(_mm_sub_ps(load_m128(), _mm_set1_ps(rhs)));
				return *this;
			}
		}
#endif
		values[0] -= rhs;
		values[1] -= rhs;
		values[2] -= rhs;
		values[3] -= rhs;
		return *this;
	}

	FORCE_INLINE constexpr quad_group& operator*=(const T& rhs) noexcept{
#if defined(XRGUI_G2D_QUAD_GROUP_HAS_SSE)
		if constexpr(std::same_as<T, float>){
			if(!std::is_constant_evaluated()){
				store_m128(_mm_mul_ps(load_m128(), _mm_set1_ps(rhs)));
				return *this;
			}
		}
#endif
		values[0] *= rhs;
		values[1] *= rhs;
		values[2] *= rhs;
		values[3] *= rhs;
		return *this;
	}

	FORCE_INLINE constexpr quad_group& operator/=(const T& rhs) noexcept{
#if defined(XRGUI_G2D_QUAD_GROUP_HAS_SSE)
		if constexpr(std::same_as<T, float>){
			if(!std::is_constant_evaluated()){
				store_m128(_mm_div_ps(load_m128(), _mm_set1_ps(rhs)));
				return *this;
			}
		}
#endif
		values[0] /= rhs;
		values[1] /= rhs;
		values[2] /= rhs;
		values[3] /= rhs;
		return *this;
	}

	[[nodiscard]] FORCE_INLINE friend constexpr quad_group min(const quad_group& lhs, const quad_group& rhs) noexcept
		requires std::same_as<T, float>{
#if defined(XRGUI_G2D_QUAD_GROUP_HAS_SSE)
		if(!std::is_constant_evaluated()){
			return from_m128(_mm_min_ps(lhs.load_m128(), rhs.load_m128()));
		}
#endif
		return {
			(std::min)(lhs[0], rhs[0]),
			(std::min)(lhs[1], rhs[1]),
			(std::min)(lhs[2], rhs[2]),
			(std::min)(lhs[3], rhs[3])
		};
	}

	[[nodiscard]] FORCE_INLINE friend constexpr quad_group max(const quad_group& lhs, const quad_group& rhs) noexcept
		requires std::same_as<T, float>{
#if defined(XRGUI_G2D_QUAD_GROUP_HAS_SSE)
		if(!std::is_constant_evaluated()){
			return from_m128(_mm_max_ps(lhs.load_m128(), rhs.load_m128()));
		}
#endif
		return {
			(std::max)(lhs[0], rhs[0]),
			(std::max)(lhs[1], rhs[1]),
			(std::max)(lhs[2], rhs[2]),
			(std::max)(lhs[3], rhs[3])
		};
	}

	[[nodiscard]] FORCE_INLINE friend constexpr quad_group clamp(
		const quad_group& value,
		const quad_group& low,
		const quad_group& high) noexcept requires std::same_as<T, float>{
#if defined(XRGUI_G2D_QUAD_GROUP_HAS_SSE)
		if(!std::is_constant_evaluated()){
			return from_m128(_mm_min_ps(_mm_max_ps(value.load_m128(), low.load_m128()), high.load_m128()));
		}
#endif
		return {
			(std::clamp)(value[0], low[0], high[0]),
			(std::clamp)(value[1], low[1], high[1]),
			(std::clamp)(value[2], low[2], high[2]),
			(std::clamp)(value[3], low[3], high[3])
		};
	}

	[[nodiscard]] FORCE_INLINE friend constexpr float sum(const quad_group& value) noexcept requires std::same_as<T, float>{
#if defined(XRGUI_G2D_QUAD_GROUP_HAS_SSE)
		if(!std::is_constant_evaluated()){
			const __m128 data = value.load_m128();
			const __m128 high = _mm_movehl_ps(data, data);
			const __m128 pair_sum = _mm_add_ps(data, high);
			const __m128 swapped = _mm_shuffle_ps(pair_sum, pair_sum, _MM_SHUFFLE(1, 1, 1, 1));
			return _mm_cvtss_f32(_mm_add_ss(pair_sum, swapped));
		}
#endif
		return value[0] + value[1] + value[2] + value[3];
	}

	[[nodiscard]] FORCE_INLINE friend constexpr float horizontal_min(const quad_group& value) noexcept
		requires std::same_as<T, float>{
#if defined(XRGUI_G2D_QUAD_GROUP_HAS_SSE)
		if(!std::is_constant_evaluated()){
			const __m128 data = value.load_m128();
			const __m128 shuffled = _mm_shuffle_ps(data, data, _MM_SHUFFLE(2, 3, 0, 1));
			const __m128 pair_min = _mm_min_ps(data, shuffled);
			const __m128 pair_shuffled = _mm_shuffle_ps(pair_min, pair_min, _MM_SHUFFLE(1, 0, 3, 2));
			return _mm_cvtss_f32(_mm_min_ps(pair_min, pair_shuffled));
		}
#endif
		return (std::min)((std::min)(value[0], value[1]), (std::min)(value[2], value[3]));
	}

	[[nodiscard]] FORCE_INLINE friend constexpr float horizontal_max(const quad_group& value) noexcept
		requires std::same_as<T, float>{
#if defined(XRGUI_G2D_QUAD_GROUP_HAS_SSE)
		if(!std::is_constant_evaluated()){
			const __m128 data = value.load_m128();
			const __m128 shuffled = _mm_shuffle_ps(data, data, _MM_SHUFFLE(2, 3, 0, 1));
			const __m128 pair_max = _mm_max_ps(data, shuffled);
			const __m128 pair_shuffled = _mm_shuffle_ps(pair_max, pair_max, _MM_SHUFFLE(1, 0, 3, 2));
			return _mm_cvtss_f32(_mm_max_ps(pair_max, pair_shuffled));
		}
#endif
		return (std::max)((std::max)(value[0], value[1]), (std::max)(value[2], value[3]));
	}

	[[nodiscard]] FORCE_INLINE friend constexpr float dot(const quad_group& lhs, const quad_group& rhs) noexcept
		requires std::same_as<T, float>{
#if defined(XRGUI_G2D_QUAD_GROUP_HAS_SSE)
		if(!std::is_constant_evaluated()){
			const __m128 products = _mm_mul_ps(lhs.load_m128(), rhs.load_m128());
			const __m128 high = _mm_movehl_ps(products, products);
			const __m128 pair_sum = _mm_add_ps(products, high);
			const __m128 swapped = _mm_shuffle_ps(pair_sum, pair_sum, _MM_SHUFFLE(1, 1, 1, 1));
			return _mm_cvtss_f32(_mm_add_ss(pair_sum, swapped));
		}
#endif
		return lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2] + lhs[3] * rhs[3];
	}

	[[nodiscard]] FORCE_INLINE friend constexpr quad_group lerp(
		const quad_group& src,
		const quad_group& dst,
		const float alpha) noexcept requires std::same_as<T, float>{
#if defined(XRGUI_G2D_QUAD_GROUP_HAS_SSE)
		if(!std::is_constant_evaluated()){
			const __m128 src_data = src.load_m128();
			const __m128 delta = _mm_sub_ps(dst.load_m128(), src_data);
			return from_m128(_mm_add_ps(src_data, _mm_mul_ps(delta, _mm_set1_ps(alpha))));
		}
#endif
		return {
			src[0] + (dst[0] - src[0]) * alpha,
			src[1] + (dst[1] - src[1]) * alpha,
			src[2] + (dst[2] - src[2]) * alpha,
			src[3] + (dst[3] - src[3]) * alpha
		};
	}

	[[nodiscard]] FORCE_INLINE friend constexpr quad_group fma(
		const quad_group& a,
		const quad_group& b,
		const quad_group& c) noexcept requires std::same_as<T, float>{
#if defined(XRGUI_G2D_QUAD_GROUP_HAS_SSE) && defined(XRGUI_G2D_QUAD_GROUP_HAS_FMA)
		if(!std::is_constant_evaluated()){
			return from_m128(_mm_fmadd_ps(a.load_m128(), b.load_m128(), c.load_m128()));
		}
#endif
		return {
			fma_scalar_(a[0], b[0], c[0]),
			fma_scalar_(a[1], b[1], c[1]),
			fma_scalar_(a[2], b[2], c[2]),
			fma_scalar_(a[3], b[3], c[3])
		};
	}
};

template <typename T>
quad_group(const T&) -> quad_group<T>;

static_assert(std::is_standard_layout_v<quad_group<float>>);
static_assert(std::is_trivially_copyable_v<quad_group<float>>);
static_assert(sizeof(quad_group<float>) == sizeof(float) * 4);
static_assert(alignof(quad_group<float>) == instr_required_align);
static_assert(offsetof(quad_group<float>, values) == 0);

consteval bool quad_group_float_constexpr_ops_test(){
	const quad_group<float> a{1.0f, 2.0f, 3.0f, 4.0f};
	const quad_group<float> b{5.0f, 6.0f, 7.0f, 8.0f};
	const quad_group<float> c{2.0f, 4.0f, 5.0f, 10.0f};
	quad_group<float> mut = a;
	mut += b;
	mut -= 1.0f;
	mut *= 2.0f;
	mut /= 2.0f;

	return
		(a + b) == quad_group<float>{6.0f, 8.0f, 10.0f, 12.0f} &&
		(b - a) == quad_group<float>{4.0f, 4.0f, 4.0f, 4.0f} &&
		(a * b) == quad_group<float>{5.0f, 12.0f, 21.0f, 32.0f} &&
		(b / a) == quad_group<float>{5.0f, 3.0f, 7.0f / 3.0f, 2.0f} &&
		(10.0f - a) == quad_group<float>{9.0f, 8.0f, 7.0f, 6.0f} &&
		(20.0f / c) == quad_group<float>{10.0f, 5.0f, 4.0f, 2.0f} &&
		mut == quad_group<float>{5.0f, 7.0f, 9.0f, 11.0f} &&
		min(a, b) == a &&
		max(a, b) == b &&
		clamp(quad_group<float>{0.0f, 3.0f, 9.0f, 6.0f}, a, b) == quad_group<float>{1.0f, 3.0f, 7.0f, 6.0f} &&
		lerp(a, b, 0.5f) == quad_group<float>{3.0f, 4.0f, 5.0f, 6.0f} &&
		fma(a, b, quad_group<float>{1.0f}) == quad_group<float>{6.0f, 13.0f, 22.0f, 33.0f} &&
		sum(a) == 10.0f &&
		horizontal_min(a) == 1.0f &&
		horizontal_max(a) == 4.0f &&
		dot(a, b) == 70.0f;
}

static_assert(quad_group_float_constexpr_ops_test());

}
