module;

#include <vulkan/vulkan.h>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.graphic.draw.instruction;

export import mo_yanxi.graphic.draw.instruction.general;
export import mo_yanxi.math.vector2;
export import mo_yanxi.math.vector4;
export import mo_yanxi.graphic.color;
export import mo_yanxi.math.matrix4;
import mo_yanxi.hlsl_alias;
import mo_yanxi.math;
import std;

namespace mo_yanxi::graphic::draw::instruction{

constexpr inline float CircleVertPrecision{12};

export
using  quad_vert_color = quad_group<float4>;

export
FORCE_INLINE constexpr std::uint32_t get_circle_vertices(const float radius) noexcept{
	return math::clamp<std::uint32_t>(static_cast<std::uint32_t>(radius * math::pi / CircleVertPrecision), 10U, 256U);
}

export struct triangle{
	primitive_generic generic;
	float2 p0, p1, p2;
	float2 uv0, uv1, uv2;
	float4 c0, c1, c2;


	[[nodiscard]] FORCE_INLINE CONST_FN constexpr std::uint32_t get_vertex_count(
		this const triangle& instruction) noexcept{ return 3; }

	[[nodiscard]] FORCE_INLINE CONST_FN constexpr std::uint32_t get_primitive_count(
		this const triangle& instruction) noexcept{ return 1; }
};

struct quad_like{
	[[nodiscard]] FORCE_INLINE CONST_FN static constexpr std::uint32_t get_vertex_count() noexcept{ return 4; }

	[[nodiscard]] FORCE_INLINE CONST_FN static constexpr std::uint32_t get_primitive_count() noexcept{ return 2; }
};

export struct quad : quad_like{
	primitive_generic generic;
	float2 v00, v10, v01, v11;
	float2 uv00, uv10, uv01, uv11;

	quad_vert_color vert_color;
};

export struct rectangle : quad_like{
	primitive_generic generic;
	float2 pos;
	float angle;
	float scale; //TODO uses other?

	quad_vert_color vert_color;
	float2 extent;
	float2 uv00, uv11;


};


export struct rectangle_ortho : quad_like{
	primitive_generic generic;
	float2 v00, v11;
	float2 uv00, uv11;
	quad_vert_color vert_color;

};

export struct rectangle_ortho_outline{
	primitive_generic generic;
	float2 v00, v11;
	quad_group<float> stroke;

	quad_vert_color vert_color;



	[[nodiscard]] FORCE_INLINE CONST_FN static constexpr std::uint32_t get_vertex_count() noexcept{ return 10; }

	[[nodiscard]] FORCE_INLINE CONST_FN static constexpr std::uint32_t get_primitive_count() noexcept{ return 8; }
};


export struct line : quad_like{
	primitive_generic generic;
	float2 src, dst;
	math::section<float4> color;
	float stroke;

	std::uint32_t _cap[3];
};


export struct line_node{
	float2 pos;
	float stroke;
	float offset; //TODO ?
	float4 color;

	//TODO uv?
};

template <typename ...Args>
struct frist_of_pack;

template <typename T, typename ...Args>
struct frist_of_pack<T, Args...> : std::type_identity<T>{};

template <typename ...Args>
using frist_of_pack_t = frist_of_pack<Args...>::type;

export struct line_segments{
	primitive_generic generic;

	[[nodiscard]] FORCE_INLINE CONST_FN static constexpr std::uint32_t get_vertex_count(
		std::size_t node_payload_size
	) noexcept{
		assert(node_payload_size >= 4);
		return (node_payload_size - 2) * 2;
	}


	[[nodiscard]] FORCE_INLINE CONST_FN static constexpr std::uint32_t get_primitive_count(
		std::size_t node_payload_size
	) noexcept{
		assert(node_payload_size >= 4);
		return (node_payload_size - 3) * 2;
	}

	template <typename... Args>
	[[nodiscard]] FORCE_INLINE CONST_FN static constexpr std::uint32_t get_vertex_count(
		const Args&... args
	) noexcept{
		if constexpr (sizeof...(Args) == 1 && contiguous_range_of<frist_of_pack_t<Args...>, line_node>){
			return line_segments::get_vertex_count(std::ranges::size(args...));
		}else{
			return line_segments::get_vertex_count(sizeof...(Args));
		}

	}

	template <typename... Args>
	[[nodiscard]] FORCE_INLINE CONST_FN static constexpr std::uint32_t get_primitive_count(
		const Args&... args
	) noexcept{
		if constexpr (sizeof...(Args) == 1 && contiguous_range_of<frist_of_pack_t<Args...>, line_node>){
			return line_segments::get_primitive_count(std::ranges::size(args...));
		}else{
			return line_segments::get_primitive_count(sizeof...(Args));
		}
	}
};

export struct line_segments_closed : line_segments{

	[[nodiscard]] FORCE_INLINE CONST_FN static constexpr std::uint32_t get_vertex_count(
		std::size_t node_payload_size
	) noexcept{
		assert(node_payload_size >= 3);
		return (node_payload_size) * 2 + 2;
	}


	[[nodiscard]] FORCE_INLINE CONST_FN static constexpr std::uint32_t get_primitive_count(
		std::size_t node_payload_size
	) noexcept{
		assert(node_payload_size >= 3);
		return (node_payload_size) * 2;
	}

	template <typename... Args>
	[[nodiscard]] FORCE_INLINE CONST_FN static constexpr std::uint32_t get_vertex_count(
		const Args&... args
	) noexcept{
		if constexpr (sizeof...(Args) == 1 && contiguous_range_of<frist_of_pack_t<Args...>, line_node>){
			return line_segments_closed::get_vertex_count(std::ranges::size(args...));
		}else{
			return line_segments_closed::get_vertex_count(sizeof...(Args));
		}
	}

	template <typename... Args>
	[[nodiscard]] FORCE_INLINE CONST_FN static constexpr std::uint32_t get_primitive_count(
		const Args&... args
	) noexcept{
		if constexpr (sizeof...(Args) == 1 && contiguous_range_of<frist_of_pack_t<Args...>, line_node>){
			return line_segments_closed::get_primitive_count(std::ranges::size(args...));
		}else{
			return line_segments_closed::get_primitive_count(sizeof...(Args));
		}
	}
};


export struct poly{
	primitive_generic generic;
	float2 pos;
	std::uint32_t segments;
	float initial_angle;

	//TODO native dashline support
	std::uint32_t cap1;
	std::uint32_t cap2;

	math::range radius;
	float2 uv00, uv11;
	math::section<float4> color;


	[[nodiscard]] FORCE_INLINE CONST_FN constexpr std::uint32_t get_vertex_count(
		this const poly& instruction) noexcept{
		return instruction.radius.from == 0 ? instruction.segments + 2 : (instruction.segments + 1) * 2;
	}

	[[nodiscard]] FORCE_INLINE CONST_FN constexpr std::uint32_t get_primitive_count(
		this const poly& instruction) noexcept{
		return instruction.segments << unsigned(instruction.radius.from != 0);
	}
};

export struct poly_partial{
	primitive_generic generic;
	float2 pos;
	std::uint32_t segments;
	std::uint32_t cap;

	math::range radius;
	math::based_section<float> range;
	float2 uv00, uv11;
	math::section<float4> color;

	[[nodiscard]] FORCE_INLINE CONST_FN constexpr std::uint32_t get_vertex_count(
		this const poly_partial& instruction) noexcept{
		return instruction.radius.from == 0 ? instruction.segments + 2 : (instruction.segments + 1) * 2;
	}

	[[nodiscard]] FORCE_INLINE CONST_FN constexpr std::uint32_t get_primitive_count(
		this const poly_partial& instruction) noexcept{
		return instruction.segments << unsigned(instruction.radius.from != 0);
	}
};

export struct curve_parameter{
	std::array<math::vec4, 2> constrain_vector;
	// std::array<math::vec4, 2> constrain_vector_derivative;
};

export struct constrained_curve{
	primitive_generic generic;
	curve_parameter param;

	math::range margin;
	math::range stroke;

	std::uint32_t segments;


	std::uint32_t _cap1;
	std::uint32_t _cap2;
	std::uint32_t _cap3;

	float2 uv00, uv11;
	math::section<float4> color;

	[[nodiscard]] FORCE_INLINE CONST_FN constexpr std::uint32_t get_vertex_count(
		this const constrained_curve& instruction) noexcept{
		return instruction.segments * 2 + 2;
	}

	[[nodiscard]] FORCE_INLINE CONST_FN constexpr std::uint32_t get_primitive_count(
		this const constrained_curve& instruction) noexcept{
		return instruction.segments * 2;
	}
};

export
using curve_ctrl_handle = std::array<math::vec2, 4>;

export
struct curve_trait_matrix{
	static constexpr math::matrix4 dt{
			{}, {0, 1}, {0, 0, 2}, {0, 0, 0, 3}
		};


	math::matrix4 trait;

	[[nodiscard]] curve_trait_matrix() = default;

	[[nodiscard]] explicit(false) constexpr curve_trait_matrix(const math::matrix4& trait)
		: trait(trait){
	}

	[[nodiscard]] FORCE_INLINE constexpr curve_parameter apply_to(math::vec2 p0, math::vec2 p1, math::vec2 p2, math::vec2 p3) const noexcept{
		return {
			trait.c0 * p0.x + trait.c1 * p1.x + trait.c2 * p2.x + trait.c3 * p3.x,
			trait.c0 * p0.y + trait.c1 * p1.y + trait.c2 * p2.y + trait.c3 * p3.y
		};
	}


	FORCE_INLINE constexpr friend curve_parameter operator*(const curve_trait_matrix& lhs, const curve_ctrl_handle& rhs) noexcept{
		return lhs.apply_to(rhs[0], rhs[1], rhs[2], rhs[3]);
	}

	FORCE_INLINE constexpr friend curve_parameter operator*(const curve_trait_matrix& lhs, std::span<const math::vec2, 4> rhs) noexcept{
		return lhs.apply_to(rhs[0], rhs[1], rhs[2], rhs[3]);
	}

	template <std::ranges::random_access_range Rng>
		requires std::convertible_to<std::ranges::range_value_t<Rng>, math::vec2>
	FORCE_INLINE constexpr friend curve_parameter operator*(const curve_trait_matrix& lhs, Rng&& rhs) noexcept{
		assert(std::ranges::size(rhs) >= 4);
		return lhs.apply_to(rhs[0], rhs[1], rhs[2], rhs[3]);
	}
};

namespace curve_trait_mat{
export constexpr inline curve_trait_matrix bezier{
		{{1.f, -3.f, 3.f, -1.f}, {0, 3, -6, 3}, {0, 0, 3, -3}, {0, 0, 0, 1}}
	};

export constexpr inline curve_trait_matrix hermite{
		{{1, 0, -3, 2}, {0, 1, -2, 1}, {0, 0, 3, -2}, {0, 0, -1, 1}}
	};

export
template <float tau = .5f>
constexpr inline curve_trait_matrix catmull_rom{
		math::matrix4{
			{0, -tau, 2 * tau, -tau}, {1, 0, tau - 3, 2 - tau}, {0, tau, 3 - 2 * tau, tau - 2}, {0, 0, -tau, tau}
		}
	};

export
constexpr inline curve_trait_matrix b_spline{
		(1.f / 6.f) * math::matrix4{{1, -3, 3, -1}, {4, 0, -6, 3}, {1, 3, 3, -3}, {0, 0, 0, 1}}
	};
}

export struct row_patch{
	primitive_generic generic;

	/**
	 * @brief Equivalent Layout
	 * @code
	 * float x[4];
	 * float y[2];
	 * @endcode
	 */
	std::array<float, 6> coords;


	/**
	 * @brief Equivalent Layout
	 * @code
	 * float uv_y[2];
	 * float uv_x[4];
	 * @endcode
	 */
	std::array<float, 6> uvs;


	quad_vert_color vert_color;

	[[nodiscard]] FORCE_INLINE CONST_FN static constexpr std::uint32_t get_vertex_count() noexcept{
		return 8;
	}

	[[nodiscard]] FORCE_INLINE CONST_FN static constexpr std::uint32_t get_primitive_count() noexcept{
		return 6;
	}
};


template <std::derived_from<line_segments> T>
struct is_valid_consequent_argument<T, line_node> : std::true_type{};

template <std::derived_from<line_segments> T, contiguous_range_of<line_node> Rng>
struct is_valid_consequent_argument<T, Rng> : std::true_type{};

template <>
constexpr inline instr_type instruction_type_of<triangle> = instr_type::triangle;

template <>
constexpr inline instr_type instruction_type_of<quad> = instr_type::quad;

template <>
constexpr inline instr_type instruction_type_of<rectangle> = instr_type::rectangle;

template <>
constexpr inline instr_type instruction_type_of<line> = instr_type::line;

template <>
constexpr inline instr_type instruction_type_of<line_segments> = instr_type::line_segments;

template <>
constexpr inline instr_type instruction_type_of<line_segments_closed> = instr_type::line_segments_closed;

template <>
constexpr inline instr_type instruction_type_of<rectangle_ortho> = instr_type::rect_ortho;

template <>
constexpr inline instr_type instruction_type_of<poly> = instr_type::poly;

template <>
constexpr inline instr_type instruction_type_of<poly_partial> = instr_type::poly_partial;

template <>
constexpr inline instr_type instruction_type_of<constrained_curve> = instr_type::constrained_curve;

template <>
constexpr inline instr_type instruction_type_of<rectangle_ortho_outline> = instr_type::rect_ortho_outline;

template <>
constexpr inline instr_type instruction_type_of<row_patch> = instr_type::row_patch;

export
[[nodiscard]] FORCE_INLINE CONST_FN std::uint32_t get_vertex_count(
	instr_type type,
	const std::byte* ptr_to_instr) noexcept{

	switch(type){
	case instr_type::triangle : return 3U;
	case instr_type::quad : return 4U;
	case instr_type::rectangle : return 4U;
	case instr_type::rect_ortho : return 4U;
	case instr_type::line : return 4U;
	case instr_type::line_segments:{
		const auto size = get_instr_head(ptr_to_instr).get_instr_byte_size();
		const auto payloadByteSize = (size - get_instr_size<line_segments>());
		assert(payloadByteSize % sizeof(line_node) == 0);
		const auto payloadCount = payloadByteSize / sizeof(line_node);
		return line_segments::get_vertex_count(payloadCount);
	}
	case instr_type::line_segments_closed :{
		const auto size = get_instr_head(ptr_to_instr).get_instr_byte_size();
		const auto payloadByteSize = (size - get_instr_size<line_segments_closed>());
		assert(payloadByteSize % sizeof(line_node) == 0);
		const auto payloadCount = payloadByteSize / sizeof(line_node);
		return line_segments_closed::get_vertex_count(payloadCount);
	}
	case instr_type::poly : return reinterpret_cast<const poly*>(ptr_to_instr + sizeof(
			instruction_head))->get_vertex_count();
	case instr_type::poly_partial : return reinterpret_cast<const poly_partial*>(ptr_to_instr +
			sizeof(instruction_head))->get_vertex_count();
	case instr_type::constrained_curve : return reinterpret_cast<const constrained_curve*>(ptr_to_instr +
			sizeof(instruction_head))->get_vertex_count();
	case instr_type::rect_ortho_outline: return 10;
	case instr_type::row_patch: return 8;
	default : std::unreachable();
	}
}

export
[[nodiscard]] FORCE_INLINE CONST_FN constexpr std::uint32_t get_primitive_count(instr_type type, const std::byte* ptr_to_payload, std::uint32_t vtx) noexcept{
	return vtx < 3 ? 0 : vtx - 2;
}

}
