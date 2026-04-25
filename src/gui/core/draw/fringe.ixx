module;

#include <mo_yanxi/adapted_attributes.hpp>


#ifndef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
#include <beman/inplace_vector.hpp>
#endif


export module mo_yanxi.gui.fx.fringe;

export import mo_yanxi.gui.renderer.frontend;
export import mo_yanxi.graphic.draw.instruction;

import mo_yanxi.byte_pool;

import std;

#ifdef XRGUI_FUCK_MSVC_INCLUDE_CPP_HEADER_IN_MODULE
import <beman/inplace_vector.hpp>;
#endif


namespace mo_yanxi::gui::fx::fringe{
using namespace mo_yanxi::graphic::draw;

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

export
struct poly_fringe_at_from_draw{
	instruction::poly instr;
	float fringe = fringe_size;

	void operator()(graphic::draw::emit_t emit, auto& sink) const {
		auto instr_inner = instr;
		instr_inner.radius.to = instr_inner.radius.from - fringe;
		instr_inner.color.to = instr_inner.color.from.make_transparent();
		emit(sink, instr_inner);
	}
};

export
struct poly_fringe_at_to_draw{
	instruction::poly instr;
	float fringe = fringe_size;

	void operator()(graphic::draw::emit_t emit, auto& sink) const {
		auto instr_outer = instr;
		instr_outer.radius.from = instr_outer.radius.to + fringe;
		instr_outer.color.from = instr_outer.color.to.make_transparent();
		emit(sink, instr_outer);
	}
};

export
struct poly_fringe_only_draw{
	instruction::poly instr;
	float fringe = fringe_size;

	void operator()(graphic::draw::emit_t emit, auto& sink) const {
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
	instruction::poly instr;
	float fringe = fringe_size;

	void operator()(graphic::draw::emit_t emit, auto& sink) const {
		emit(sink, instr);
		emit(sink, poly_fringe_only_draw{instr, fringe});
	}
};

export
struct poly_partial_draw{
	instruction::poly_partial instr;
	float fringe = fringe_size;

	void operator()(graphic::draw::emit_t emit, auto& sink) const {
		emit(sink, instr);

		if(is_draw_meaningful(instr.radius.from)){
			auto instr_inner = instr;
			instr_inner.radius.to = instr_inner.radius.from - fringe;
			instr_inner.color.v10 = instr.color.v00.make_transparent();
			instr_inner.color.v11 = instr.color.v01.make_transparent();
			emit(sink, instr_inner);
		}

		if(is_draw_meaningful(instr.radius.to)){
			auto instr_outer = instr;
			instr_outer.radius.from = instr_outer.radius.to + fringe;
			instr_outer.color.v00 = instr.color.v10.make_transparent();
			instr_outer.color.v01 = instr.color.v11.make_transparent();
			emit(sink, instr_outer);
		}
	}
};

export
struct poly_partial_with_cap_draw{
	instruction::poly_partial instr;
	float src_cap_fringe = fringe_size;
	float dst_cap_fringe = fringe_size;
	float fringe = fringe_size;

	void operator()(graphic::draw::emit_t emit, auto& sink) const {
		auto instr_src = instr;
		auto instr_dst = instr;
		const auto radius = instr.radius.mid();

		emit(sink, poly_partial_draw{instr, fringe});

		if(is_draw_meaningful(src_cap_fringe)) [[likely]] {
			const auto radscl_src = src_cap_fringe / radius / math::pi_2;
			instr_src.range.extent = std::copysign(radscl_src, -instr.range.extent);
			instr_src.color.v01 = instr_src.color.v00.make_transparent();
			instr_src.color.v11 = instr_src.color.v10.make_transparent();
			instr_src.segments = 1;
			emit(sink, poly_partial_draw{instr_src, fringe});
		}

		if(is_draw_meaningful(dst_cap_fringe)) [[likely]] {
			const auto radscl_dst = dst_cap_fringe / radius / math::pi_2;
			const auto off = std::copysign(radscl_dst, instr.range.extent);
			instr_dst.range.base = instr.range.dst() + off;
			instr_dst.range.extent = -off;
			instr_dst.color.v00 = instr_dst.color.v01.make_transparent();
			instr_dst.color.v10 = instr_dst.color.v11.make_transparent();
			instr_dst.segments = 1;
			emit(sink, poly_partial_draw{instr_dst, fringe});
		}
	}
};

export
struct curve_draw{
	instruction::parametric_curve instr;
	float fringe = fringe_size;

	void operator()(graphic::draw::emit_t emit, auto& sink) const {
		auto instr_inner = instr;
		instr_inner.offset += instr.stroke / 2;
		instr_inner.offset += fringe / 2;
		instr_inner.stroke = {fringe, fringe};
		instr_inner.color.v00 = instr_inner.color.v10.make_transparent();
		instr_inner.color.v01 = instr_inner.color.v11.make_transparent();

		auto instr_outer = instr;
		instr_outer.offset -= instr.stroke / 2;
		instr_outer.offset -= fringe / 2;
		instr_outer.stroke = {fringe, fringe};
		instr_outer.color.v10 = instr_inner.color.v00.make_transparent();
		instr_outer.color.v11 = instr_inner.color.v01.make_transparent();

		emit(sink, instr);
		emit(sink, instr_outer);
		emit(sink, instr_inner);
	}
};

export
struct curve_with_cap_draw{
	instruction::parametric_curve instr;
	float cap_length_src = fringe_size;
	float cap_length_dst = fringe_size;
	float fringe = fringe_size;

	void operator()(graphic::draw::emit_t emit, auto& sink) const {
		emit(sink, curve_draw{instr, fringe});

		if(cap_length_src <= 0.0f && cap_length_dst <= 0.0f) return;

		static constexpr std::uint32_t CAP_SEGMENTS = 1;

		if(cap_length_src > 0.0f) {
			const auto t_start = instr.margin.from;
			const auto vel = instr.param.calculate_derivative(t_start);
			const auto speed = math::sqrt(vel.length2());

			if(speed > std::numeric_limits<float>::epsilon()) {
				const auto dt = cap_length_src / speed;

				auto cap_start = instr;
				cap_start.segments = CAP_SEGMENTS;
				cap_start.margin.from = instr.margin.from - dt;
				cap_start.margin.to = 1.0f - instr.margin.from;
				cap_start.stroke.to = cap_start.stroke.from;
				cap_start.color.v00 = cap_start.color.v01;
				cap_start.color.v10 = cap_start.color.v11;
				cap_start.color.v00.a = {};
				cap_start.color.v10.a = {};

				emit(sink, curve_draw{cap_start, fringe});
			}
		}

		if(cap_length_dst > 0.0f) {
			const auto t_end = instr.margin.to;
			const auto vel = instr.param.calculate_derivative(t_end);
			const auto speed = math::sqrt(vel.length2());

			if(speed > std::numeric_limits<float>::epsilon()) {
				const auto dt = cap_length_dst / speed;

				auto cap_end = instr;
				cap_end.segments = CAP_SEGMENTS;
				cap_end.margin.from = 1.f - t_end;
				cap_end.margin.to = instr.margin.to - dt;
				cap_end.color.v01 = cap_end.color.v00;
				cap_end.color.v11 = cap_end.color.v10;
				cap_end.color.v01.a = {};
				cap_end.color.v11.a = {};

				emit(sink, curve_draw{cap_end, fringe});
			}
		}
	}
};

export
FORCE_INLINE auto poly_fringe_at_from(const instruction::poly& instr, float fringe = fringe_size){
	return poly_fringe_at_from_draw{instr, fringe};
}

export
FORCE_INLINE auto poly_fringe_at_to(const instruction::poly& instr, float fringe = fringe_size){
	return poly_fringe_at_to_draw{instr, fringe};
}

export
FORCE_INLINE auto poly_fringe_only(const instruction::poly& instr, float fringe = fringe_size){
	return poly_fringe_only_draw{instr, fringe};
}

export
FORCE_INLINE auto poly(const instruction::poly& instr, float fringe = fringe_size){
	return poly_draw{instr, fringe};
}

export
FORCE_INLINE auto poly_partial(const instruction::poly_partial& instr, float fringe = fringe_size){
	return poly_partial_draw{instr, fringe};
}

export
FORCE_INLINE auto poly_partial_with_cap(const instruction::poly_partial& instr, float src_cap_fringe = fringe_size, float dst_cap_fringe = fringe_size, float fringe = fringe_size){
	return poly_partial_with_cap_draw{instr, src_cap_fringe, dst_cap_fringe, fringe};
}

export
FORCE_INLINE auto curve(const instruction::parametric_curve& instr, float fringe = fringe_size){
	return curve_draw{instr, fringe};
}

export
FORCE_INLINE auto curve_with_cap(const instruction::parametric_curve& instr, float cap_length_src = fringe_size, float cap_length_dst = fringe_size, float fringe = fringe_size){
	return curve_with_cap_draw{instr, cap_length_src, cap_length_dst, fringe};
}

export
FORCE_INLINE void poly_fringe_at_from(renderer_frontend& r, const instruction::poly& instr, float fringe = fringe_size){
	graphic::draw::emit(r, poly_fringe_at_from(instr, fringe));
}

export
FORCE_INLINE void poly_fringe_at_to(renderer_frontend& r, const instruction::poly& instr, float fringe = fringe_size){
	graphic::draw::emit(r, poly_fringe_at_to(instr, fringe));
}

export
FORCE_INLINE void poly_fringe_only(renderer_frontend& r, const instruction::poly& instr, float fringe = fringe_size){
	graphic::draw::emit(r, poly_fringe_only(instr, fringe));
}

export
FORCE_INLINE void poly(renderer_frontend& r, const instruction::poly& instr, float fringe = fringe_size){
	graphic::draw::emit(r, poly(instr, fringe));
}

export
FORCE_INLINE void poly_partial(renderer_frontend& r, const instruction::poly_partial& instr, float fringe = fringe_size){
	graphic::draw::emit(r, poly_partial(instr, fringe));
}

export
FORCE_INLINE void poly_partial_with_cap(renderer_frontend& r, const instruction::poly_partial& instr, float src_cap_fringe = fringe_size, float dst_cap_fringe = fringe_size, float fringe = fringe_size){
	graphic::draw::emit(r, poly_partial_with_cap(instr, src_cap_fringe, dst_cap_fringe, fringe));
}

export
FORCE_INLINE void curve(renderer_frontend& r, const instruction::parametric_curve& instr, float fringe = fringe_size){
	graphic::draw::emit(r, curve(instr, fringe));
}

export
FORCE_INLINE void curve_with_cap(renderer_frontend& r, const instruction::parametric_curve& instr, float cap_length_src = fringe_size, float cap_length_dst = fringe_size, float fringe = fringe_size) {
	graphic::draw::emit(r, curve_with_cap(instr, cap_length_src, cap_length_dst, fringe));
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

		void operator()(graphic::draw::emit_t, auto& sink) const {
			sink(head, ctx->get_nodes());
		}
	};

	template <typename HeadType>
	struct fringe_draw{
		line_context* ctx;
		HeadType head;
		float stroke;
		bool is_inner;

		void operator()(graphic::draw::emit_t, auto& sink) const {
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

	FORCE_INLINE void push(const instruction::line_node& node){
		const auto current_idx = size();
		resize(current_idx + 1);

		data()[current_idx] = node;
	}

	FORCE_INLINE std::span<const instruction::line_node> get_nodes() const noexcept{
		return buf_;
	}

	FORCE_INLINE void push(const math::vec2 pos, float stroke, graphic::color color){
		push({pos, stroke, 0, {color, color}});
	}

	FORCE_INLINE void dump_mid(renderer_frontend& renderer, const instruction::line_segments& head){
		graphic::draw::emit(renderer, mid(head));
	}

	FORCE_INLINE void dump_mid(renderer_frontend& renderer, const instruction::line_segments_closed& head){
		graphic::draw::emit(renderer, mid(head));
	}

	FORCE_INLINE instruction::line_node& add_cap_src(float stroke){
		const auto sz = size();

		resize(sz + 2);

		auto* ptr = data();

		std::memmove(ptr + 2, ptr, sz * sizeof(instruction::line_node));




		const auto tan_front = (ptr[2].pos - ptr[3].pos).set_length(stroke);
		return patch_cap_src(tan_front);
	}

	FORCE_INLINE instruction::line_node& add_cap_dst(float stroke){
		const auto sz = size();
		resize(sz + 2);

		auto* ptr = data();


		const auto tan_back = (ptr[sz - 1].pos - ptr[sz - 2].pos).set_length(stroke);

		return patch_cap_dst(tan_back, sz);
	}

	FORCE_INLINE math::section<instruction::line_node&> add_cap(float cap_src, float cap_dst) noexcept {
		const auto sz = size();

		resize(sz + 4);

		auto* ptr = data();



		auto vec_front = ptr[0].pos - ptr[1].pos;

		auto vec_back = ptr[sz-1].pos - ptr[sz-2].pos;



		std::memmove(ptr + 2, ptr, sz * sizeof(instruction::line_node));


		const auto tan_front = vec_front.set_length(cap_src);
		const auto tan_back = vec_back.set_length(cap_dst);



		auto& src_node = patch_cap_src(tan_front);



		auto& dst_node = patch_cap_dst(tan_back, sz + 2);

		return {src_node, dst_node};
	}

	FORCE_INLINE math::section<instruction::line_node&> add_cap() noexcept {
		 return this->add_cap(front().stroke / 2.f, back().stroke / 2.f);
	}

	FORCE_INLINE void add_fringe_cap_src(float cap_stroke){
		using namespace instruction;
		auto& node = add_cap_src(cap_stroke);
		node.color.invoke(&graphic::color::set_a, 0);
	}

	FORCE_INLINE void add_fringe_cap_dst(float cap_stroke){
		using namespace instruction;
		auto& node = add_cap_dst(cap_stroke);
		node.color.invoke(&graphic::color::set_a, 0);
	}

	FORCE_INLINE void add_fringe_cap(float cap_stroke_src = fringe_size, float cap_stroke_dst = fringe_size){
		using namespace graphic::draw::instruction;
		auto [src, dst] = add_cap(cap_stroke_src, cap_stroke_dst);
		src.color.invoke(&graphic::color::set_a, 0);
		dst.color.invoke(&graphic::color::set_a, 0);
	}

	FORCE_INLINE void dump_fringe_inner(renderer_frontend& renderer, const instruction::line_segments_closed& head, float stroke){
		graphic::draw::emit(renderer, fringe_inner(head, stroke));
	}

	FORCE_INLINE void dump_fringe_outer(renderer_frontend& renderer, const instruction::line_segments_closed& head, float stroke){
		graphic::draw::emit(renderer, fringe_outer(head, stroke));
	}

	FORCE_INLINE void dump_fringe_inner(renderer_frontend& renderer, const instruction::line_segments& head, float stroke = fringe_size){
		graphic::draw::emit(renderer, fringe_inner(head, stroke));
	}

	FORCE_INLINE void dump_fringe_outer(renderer_frontend& renderer, const instruction::line_segments& head, float stroke = fringe_size){
		graphic::draw::emit(renderer, fringe_outer(head, stroke));
	}

	FORCE_INLINE std::size_t size() const noexcept{
		return std::ranges::size(buf_);
	}

	FORCE_INLINE instruction::line_node* data() noexcept{
		return std::ranges::data(buf_);
	}

	FORCE_INLINE const instruction::line_node* data() const noexcept{
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

	instruction::line_node& patch_cap_src(math::vec2 mov) noexcept{
		auto* ptr = data();
		ptr[1] = ptr[2];
		ptr[1].pos += mov;
		ptr[0] = ptr[1];
		ptr[0].pos += mov;
		return ptr[1];
	}

	instruction::line_node& patch_cap_dst(math::vec2 mov, std::size_t base_idx) noexcept{
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
using inplace_line_context = line_context<beman::inplace_vector::inplace_vector<instruction::line_node, N>>;

}
