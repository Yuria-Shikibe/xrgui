module;

#include <mo_yanxi/adapted_attributes.hpp>


#ifndef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
#include <beman/inplace_vector.hpp>
#endif


export module mo_yanxi.graphic.g2d.fringe;

export import mo_yanxi.gui.renderer.frontend;
export import mo_yanxi.graphic.g2d;

import mo_yanxi.byte_pool;

import std;

#ifdef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
import <beman/inplace_vector.hpp>;
#endif


namespace mo_yanxi::graphic::g2d::fringe{
using namespace mo_yanxi::graphic::g2d;

template <std::floating_point T>
FORCE_INLINE CONST_FN bool is_draw_meaningful(T f) noexcept{
	return std::abs(f) > .5f;
}

template <std::floating_point T>
FORCE_INLINE CONST_FN bool is_nearly_zero_assume_positive(T f) noexcept{
	assert(f >= 0);
	return f < std::numeric_limits<T>::epsilon();
}

export
inline constexpr float fringe_size = 1.12f;

enum struct edge_position : bool{
	from,
	to,
};

enum struct stroke_band : bool{
	inner,
	outer,
};

[[nodiscard]] FORCE_INLINE poly make_poly_fringe_instr(
	const poly& instr,
	float fringe,
	edge_position edge
) noexcept{
	auto fringe_instr = instr;

	if(edge == edge_position::from){
		fringe_instr.radius.to = fringe_instr.radius.from - fringe;
		fringe_instr.color.to = fringe_instr.color.from.make_transparent();
	} else{
		fringe_instr.radius.from = fringe_instr.radius.to + fringe;
		fringe_instr.color.from = fringe_instr.color.to.make_transparent();
	}

	return fringe_instr;
}

[[nodiscard]] FORCE_INLINE poly_partial make_poly_partial_fringe_instr(
	const poly_partial& instr,
	float fringe,
	edge_position edge
) noexcept{
	auto fringe_instr = instr;

	if(edge == edge_position::from){
		fringe_instr.radius.to = fringe_instr.radius.from - fringe;
		fringe_instr.color[1] = fringe_instr.color[0].make_transparent();
		fringe_instr.color[3] = fringe_instr.color[2].make_transparent();
	} else{
		fringe_instr.radius.from = fringe_instr.radius.to + fringe;
		fringe_instr.color[0] = fringe_instr.color[1].make_transparent();
		fringe_instr.color[2] = fringe_instr.color[3].make_transparent();
	}

	return fringe_instr;
}

[[nodiscard]] FORCE_INLINE poly_partial make_poly_partial_cap_instr(
	const poly_partial& instr,
	float cap_fringe,
	edge_position edge
) noexcept{
	auto cap_instr = instr;
	const auto radius = instr.radius.mid();
	const auto rad_scale = cap_fringe / radius / math::pi_2;

	if(edge == edge_position::from){
		cap_instr.range.extent = std::copysign(rad_scale, -instr.range.extent);
		cap_instr.color[2] = cap_instr.color[0].make_transparent();
		cap_instr.color[3] = cap_instr.color[1].make_transparent();
	} else{
		const auto off = std::copysign(rad_scale, instr.range.extent);
		cap_instr.range.base = instr.range.dst() + off;
		cap_instr.range.extent = -off;
		cap_instr.color[0] = cap_instr.color[2].make_transparent();
		cap_instr.color[1] = cap_instr.color[3].make_transparent();
	}

	cap_instr.segments = 1;
	return cap_instr;
}

[[nodiscard]] FORCE_INLINE parametric_curve make_curve_fringe_instr(
	const parametric_curve& instr,
	float fringe,
	stroke_band band
) noexcept{
	auto fringe_instr = instr;
	const math::range half_stroke{instr.stroke.from * .5f, instr.stroke.to * .5f};

	if(band == stroke_band::inner){
		fringe_instr.offset += half_stroke;
		fringe_instr.offset += fringe / 2;
		fringe_instr.color[0] = fringe_instr.color[1].make_transparent();
		fringe_instr.color[2] = fringe_instr.color[3].make_transparent();
	} else{
		fringe_instr.offset -= half_stroke;
		fringe_instr.offset -= fringe / 2;
		fringe_instr.color[1] = fringe_instr.color[1].make_transparent();
		fringe_instr.color[3] = fringe_instr.color[3].make_transparent();
	}

	fringe_instr.stroke = {fringe, fringe};
	return fringe_instr;
}

export
struct poly_fringe_at_from_draw{
	poly instr;
	float fringe = fringe_size;

	FORCE_INLINE void operator()(emit_t emit, auto& sink) const {
		emit(sink, make_poly_fringe_instr(instr, fringe, edge_position::from));
	}
};

export
struct poly_fringe_at_to_draw{
	poly instr;
	float fringe = fringe_size;

	void operator()(emit_t emit, auto& sink) const {
		emit(sink, make_poly_fringe_instr(instr, fringe, edge_position::to));
	}
};

export
struct poly_fringe_only_draw{
	poly instr;
	float fringe = fringe_size;

	void operator()(emit_t emit, auto& sink) const {
		if(is_draw_meaningful(instr.radius.from)){
			emit(sink, poly_fringe_at_from_draw{instr, fringe});
		}

		if(is_draw_meaningful(instr.radius.to)){
			emit(sink, poly_fringe_at_to_draw{instr, fringe});
		}
	}
};

export
struct poly_draw{
	poly instr;
	float fringe = fringe_size;

	void operator()(emit_t emit, auto& sink) const {
		emit(sink, instr);
		emit(sink, poly_fringe_only_draw{instr, fringe});
	}
};

export
struct poly_partial_draw{
	poly_partial instr;
	float fringe = fringe_size;

	void operator()(emit_t emit, auto& sink) const {
		emit(sink, instr);

		if(is_draw_meaningful(instr.radius.from)){
			emit(sink, make_poly_partial_fringe_instr(instr, fringe, edge_position::from));
		}

		if(is_draw_meaningful(instr.radius.to)){
			emit(sink, make_poly_partial_fringe_instr(instr, fringe, edge_position::to));
		}
	}
};

export
struct poly_partial_with_cap_draw{
	poly_partial instr;
	float src_cap_fringe = fringe_size;
	float dst_cap_fringe = fringe_size;
	float fringe = fringe_size;

	void operator()(emit_t emit, auto& sink) const {
		emit(sink, poly_partial_draw{instr, fringe});

		if(is_draw_meaningful(src_cap_fringe)) [[likely]] {
			emit(sink, poly_partial_draw{make_poly_partial_cap_instr(instr, src_cap_fringe, edge_position::from), fringe});
		}

		if(is_draw_meaningful(dst_cap_fringe)) [[likely]] {
			emit(sink, poly_partial_draw{make_poly_partial_cap_instr(instr, dst_cap_fringe, edge_position::to), fringe});
		}
	}
};

export
struct curve_draw{
	parametric_curve instr;
	float fringe = fringe_size;

	void operator()(emit_t emit, auto& sink) const {
		emit(sink, instr);
		emit(sink, make_curve_fringe_instr(instr, fringe, stroke_band::outer));
		emit(sink, make_curve_fringe_instr(instr, fringe, stroke_band::inner));
	}
};

template <typename Sink>
FORCE_INLINE void emit_curve_cap(
	emit_t emit,
	Sink& sink,
	const parametric_curve& instr,
	float cap_length,
	float fringe,
	edge_position edge
){
	if(cap_length <= 0.0f) return;

	static constexpr std::uint32_t CAP_SEGMENTS = 1;
	const auto t = edge == edge_position::from ? instr.margin.from : instr.margin.to;
	const auto vel = instr.param.calculate_derivative(t);
	const auto speed = math::sqrt(vel.length2());

	if(speed <= std::numeric_limits<float>::epsilon()) return;

	const auto dt = cap_length / speed;
	auto cap_instr = instr;
	cap_instr.segments = CAP_SEGMENTS;

	if(edge == edge_position::from){
		cap_instr.margin.from = instr.margin.from - dt;
		cap_instr.margin.to = 1.0f - instr.margin.from;
		cap_instr.stroke.to = cap_instr.stroke.from;
		cap_instr.color[0] = cap_instr.color[2];
		cap_instr.color[1] = cap_instr.color[3];
		cap_instr.color[0].a = {};
		cap_instr.color[1].a = {};
	} else{
		cap_instr.margin.from = 1.f - t;
		cap_instr.margin.to = instr.margin.to - dt;
		cap_instr.color[2] = cap_instr.color[0];
		cap_instr.color[3] = cap_instr.color[1];
		cap_instr.color[2].a = {};
		cap_instr.color[3].a = {};
	}

	emit(sink, curve_draw{cap_instr, fringe});
}

export
struct curve_with_cap_draw{
	parametric_curve instr;
	float cap_length_src = fringe_size;
	float cap_length_dst = fringe_size;
	float fringe = fringe_size;

	void operator()(emit_t emit, auto& sink) const {
		emit(sink, curve_draw{instr, fringe});

		if(cap_length_src <= 0.0f && cap_length_dst <= 0.0f) return;

		emit_curve_cap(emit, sink, instr, cap_length_src, fringe, edge_position::from);
		emit_curve_cap(emit, sink, instr, cap_length_dst, fringe, edge_position::to);
	}
};

export
FORCE_INLINE auto poly_fringe_at_from(const mo_yanxi::graphic::g2d::poly& instr, float fringe_width = fringe_size){
	return poly_fringe_at_from_draw{instr, fringe_width};
}

export
FORCE_INLINE auto poly_fringe_at_to(const mo_yanxi::graphic::g2d::poly& instr, float fringe_width = fringe_size){
	return poly_fringe_at_to_draw{instr, fringe_width};
}

export
FORCE_INLINE auto poly_fringe_only(const mo_yanxi::graphic::g2d::poly& instr, float fringe_width = fringe_size){
	return poly_fringe_only_draw{instr, fringe_width};
}

export
FORCE_INLINE auto poly(const mo_yanxi::graphic::g2d::poly& instr, float fringe_width = fringe_size){
	return poly_draw{instr, fringe_width};
}

export
FORCE_INLINE auto poly_partial(const mo_yanxi::graphic::g2d::poly_partial& instr, float fringe_width = fringe_size){
	return poly_partial_draw{instr, fringe_width};
}

export
FORCE_INLINE auto poly_partial_with_cap(const mo_yanxi::graphic::g2d::poly_partial& instr, float src_cap_fringe = fringe_size, float dst_cap_fringe = fringe_size, float fringe_width = fringe_size){
	return poly_partial_with_cap_draw{instr, src_cap_fringe, dst_cap_fringe, fringe_width};
}

export
FORCE_INLINE auto curve(const mo_yanxi::graphic::g2d::parametric_curve& instr, float fringe_width = fringe_size){
	return curve_draw{instr, fringe_width};
}

export
FORCE_INLINE auto curve_with_cap(const mo_yanxi::graphic::g2d::parametric_curve& instr, float cap_length_src = fringe_size, float cap_length_dst = fringe_size, float fringe_width = fringe_size){
	return curve_with_cap_draw{instr, cap_length_src, cap_length_dst, fringe_width};
}

template <typename T>
concept container_buffer = requires(T& t, std::size_t sz){
	t.resize(sz);
	requires std::ranges::contiguous_range<T>;
	requires std::ranges::sized_range<T>;
};

export
template <typename T, std::size_t N>
struct static_array_buffer : std::array<T, N>{
	using array = std::array<T, N>;
	using array::array;

	static void resize(array::size_type sz){
		if(sz > N){
			throw std::bad_alloc{};
		}
	}
};

export
template <container_buffer Buffer>
struct line_context{
private:
	Buffer buf_{};

public:
	[[nodiscard]] line_context() = default;

	[[nodiscard]] explicit(false) line_context(Buffer&& buf)
		: buf_(std::move(buf)){
	}

	[[nodiscard]] explicit(false) line_context(const Buffer& buf)
		: buf_(std::move(buf)){
	}

	template <typename ...Args>
		requires (std::constructible_from<Buffer, Args&&...>)
	[[nodiscard]] explicit(false) line_context(std::in_place_type_t<Buffer>, Args&& ...args)
		: buf_(std::forward<Args>(args)...){
	}

	template <typename HeadType>
	struct mid_draw{
		line_context* ctx;
		HeadType head;

		void operator()(emit_t, auto& sink) const {
			sink(head, ctx->get_nodes());
		}
	};

	template <typename HeadType>
	struct fringe_draw{
		line_context* ctx;
		HeadType head;
		float stroke;
		bool is_inner;

		void operator()(emit_t, auto& sink) const {
			ctx->emit_fringe_impl(sink, head, stroke, is_inner);
		}
	};

	template <typename HeadType>
	[[nodiscard]] auto mid(const HeadType& head) noexcept {
		return mid_draw<HeadType>{this, head};
	}

	template <typename HeadType>
	[[nodiscard]] auto fringe_inner(const HeadType& head, float stroke = fringe_size) noexcept {
		return fringe_draw<HeadType>{this, head, stroke, true};
	}

	template <typename HeadType>
	[[nodiscard]] auto fringe_outer(const HeadType& head, float stroke = fringe_size) noexcept {
		return fringe_draw<HeadType>{this, head, stroke, false};
	}

	FORCE_INLINE void clear() noexcept{
		resize(0);
	}

	FORCE_INLINE void push(const line_node& node){
		const auto current_idx = size();
		resize(current_idx + 1);

		data()[current_idx] = node;
	}

	FORCE_INLINE std::span<const line_node> get_nodes() const noexcept{
		return buf_;
	}

	FORCE_INLINE void push(const math::vec2 pos, float stroke, graphic::color color){
		push({pos, stroke, 0, {color, color}});
	}

	FORCE_INLINE line_node& add_cap_src(float stroke){
		const auto sz = size();

		resize(sz + 2);

		auto* ptr = data();

		std::memmove(ptr + 2, ptr, sz * sizeof(line_node));




		const auto tan_front = (ptr[2].pos - ptr[3].pos).set_length(stroke);
		return patch_cap_src(tan_front);
	}

	FORCE_INLINE line_node& add_cap_dst(float stroke){
		const auto sz = size();
		resize(sz + 2);

		auto* ptr = data();


		const auto tan_back = (ptr[sz - 1].pos - ptr[sz - 2].pos).set_length(stroke);

		return patch_cap_dst(tan_back, sz);
	}

	FORCE_INLINE math::section<line_node&> add_cap(float cap_src, float cap_dst) noexcept {
		const auto sz = size();

		resize(sz + 4);

		auto* ptr = data();



		auto vec_front = ptr[0].pos - ptr[1].pos;

		auto vec_back = ptr[sz-1].pos - ptr[sz-2].pos;



		std::memmove(ptr + 2, ptr, sz * sizeof(line_node));


		const auto tan_front = vec_front.set_length(cap_src);
		const auto tan_back = vec_back.set_length(cap_dst);



		auto& src_node = patch_cap_src(tan_front);



		auto& dst_node = patch_cap_dst(tan_back, sz + 2);

		return {src_node, dst_node};
	}

	FORCE_INLINE math::section<line_node&> add_cap() noexcept {
		 return this->add_cap(front().stroke / 2.f, back().stroke / 2.f);
	}

	FORCE_INLINE void add_fringe_cap_src(float cap_stroke){
		
		auto& node = add_cap_src(cap_stroke);
		node.color.invoke(&graphic::color::set_a, 0.f);
	}

	FORCE_INLINE void add_fringe_cap_dst(float cap_stroke){
		
		auto& node = add_cap_dst(cap_stroke);
		node.color.invoke(&graphic::color::set_a, 0.f);
	}

	FORCE_INLINE void add_fringe_cap(float cap_stroke_src = fringe_size, float cap_stroke_dst = fringe_size){
		using namespace graphic::g2d;
		auto [src, dst] = add_cap(cap_stroke_src, cap_stroke_dst);
		src.color.invoke(&graphic::color::set_a, 0.f);
		dst.color.invoke(&graphic::color::set_a, 0.f);
	}

	FORCE_INLINE std::size_t size() const noexcept{
		return std::ranges::size(buf_);
	}

	FORCE_INLINE line_node* data() noexcept{
		return std::ranges::data(buf_);
	}

	FORCE_INLINE const line_node* data() const noexcept{
		return std::ranges::data(buf_);
	}

	FORCE_INLINE auto& front(this auto& self) noexcept{
		assert(self.size() > 0);
		return self.data()[0];
	}

	FORCE_INLINE auto& front_at(this auto& self, unsigned where) noexcept{
		assert(self.size() > where);
		return self.data()[where];
	}

	FORCE_INLINE auto& back_at(this auto& self, unsigned where) noexcept{
		assert(self.size() > where);
		return self.data()[self.size() - where - 1];
	}

	FORCE_INLINE auto& back(this auto& self) noexcept{
		assert(self.size() > 0);
		return self.data()[self.size() - 1];
	}

	FORCE_INLINE bool empty() const noexcept{
		return size() == 0;
	}

private:

	template<typename HeadType>
	FORCE_INLINE void emit_fringe_impl(auto& sink, const HeadType& head, float stroke, bool is_inner) {
		assert(data() != nullptr);

		const auto element_count = size();
		resize(element_count * 2);

		auto dat = data();
		std::ranges::copy_n(dat, element_count, dat + element_count);


		for(std::size_t i = 0; i < element_count; ++i){
			const auto& src = dat[i];
			auto& dst = dat[i + element_count];

			if(is_inner){
				dst.offset = src.offset - (src.stroke + stroke) * .5f;
				dst.color.from = dst.color.to.make_transparent();
			} else{
				dst.offset = src.offset + (src.stroke + stroke) * .5f;
				dst.color.to = dst.color.from.make_transparent();
			}
			dst.stroke = stroke;
		}


		sink(head, std::span{dat + element_count, element_count});
		resize(element_count);
	}

	line_node& patch_cap_src(math::vec2 mov) noexcept{
		auto* ptr = data();
		ptr[1] = ptr[2];
		ptr[1].pos += mov;
		ptr[0] = ptr[1];
		ptr[0].pos += mov;
		return ptr[1];
	}

	line_node& patch_cap_dst(math::vec2 mov, std::size_t base_idx) noexcept{
		auto* ptr = data();

		ptr[base_idx] = ptr[base_idx - 1];
		ptr[base_idx].pos += mov;

		ptr[base_idx + 1] = ptr[base_idx];
		ptr[base_idx + 1].pos += mov;

		return ptr[base_idx];
	}

	void resize(std::size_t size){
		buf_.resize(size);
	}
};

export
template <std::size_t N>
using inplace_line_context = line_context<beman::inplace_vector::inplace_vector<line_node, N>>;

}
