module;

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.fringe;

export import mo_yanxi.gui.renderer.frontend;
export import mo_yanxi.graphic.draw.instruction;

import std;

namespace mo_yanxi::graphic{
struct float4;
}

namespace mo_yanxi::gui::fringe{
using namespace mo_yanxi::graphic::draw;

template <std::floating_point T>
FORCE_INLINE CONST_FN bool is_nearly_zero(T f) noexcept{
	return std::abs(f) < std::numeric_limits<T>::epsilon();
}

template <std::floating_point T>
FORCE_INLINE CONST_FN bool is_nearly_zero_assume_positive(T f) noexcept{
	assert(f >= 0);
	return f < std::numeric_limits<T>::epsilon();
}

export
FORCE_INLINE void poly_partial(renderer_frontend& r, const instruction::poly_partial& instr, float fringe = 2.0f){
	r.push(instr);

	if(is_nearly_zero(instr.radius.from)){
		auto instr_inner = instr;
		instr_inner.radius.to = instr_inner.radius.from - fringe;
		instr_inner.color.v10 = {};
		instr_inner.color.v11 = {};
		r.push(instr_inner);
	}

	if(is_nearly_zero(instr.radius.to)){
		auto instr_outer = instr;
		instr_outer.radius.from = instr_outer.radius.to + fringe;
		instr_outer.color.v00 = {};
		instr_outer.color.v01 = {};
		r.push(instr_outer);
	}

}

export
FORCE_INLINE void poly_partial_with_cap(renderer_frontend& r, const instruction::poly_partial& instr, float src_cap_fringe = 2.f, float dst_cap_fringe = 2.f, float fringe = 2.0f){
	auto instr_src = instr;
	auto instr_dst = instr;
	const auto radius = instr.radius.mid();

	poly_partial(r, instr, fringe);

	if(src_cap_fringe > std::numeric_limits<float>::epsilon()) [[likely]] {
		const auto radscl_src = src_cap_fringe / radius / math::pi_2;
		instr_src.range.extent = std::copysign(radscl_src, -instr.range.extent);
		instr_src.color.v01 = {};
		instr_src.color.v11 = {};
		instr_src.segments = 1;
		poly_partial(r, instr_src, fringe);

	}

	if(dst_cap_fringe > std::numeric_limits<float>::epsilon()) [[likely]] {
		const auto radscl_dst = dst_cap_fringe / radius / math::pi_2;
		const auto off = std::copysign(radscl_dst, instr.range.extent);
		instr_dst.range.base = instr.range.dst() + off;
		instr_dst.range.extent = -off;
		instr_dst.color.v00 = {};
		instr_dst.color.v10 = {};
		instr_src.segments = 1;
		poly_partial(r, instr_dst, fringe);
	}
}

export
FORCE_INLINE void curve(renderer_frontend& r, const instruction::parametric_curve& instr, float fringe = 2.0f){
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

	r.push(instr);

	r.push(instr_outer);
	r.push(instr_inner);
}

export
FORCE_INLINE void curve_with_cap(
	renderer_frontend& r, const instruction::parametric_curve& instr,
	float cap_length_src = 2.0f, float cap_length_dst = 2.0f, float fringe = 2.0f) {
    // 1. 渲染中间的主体部分（包含侧向抗锯齿）
    curve(r, instr, fringe);

    // 如果没有线头长度，直接返回
    if (cap_length_src <= 0.0f && cap_length_dst <= 0.0f) return;

    // 2. 准备线头数据
    // 线头不需要很高的细分，因为它很短，通常 1-2 段即可近似
    static constexpr std::uint32_t CAP_SEGMENTS = 1;

    // -----------------------------------------------------------------
    // 处理起点头 (Start Cap) - 向后延伸
    // -----------------------------------------------------------------
    if (cap_length_src > 0.0f) {
        const auto t_start = instr.margin.from;

        // 计算起点处的切线速度
        const auto vel = instr.param.calculate_derivative(t_start);
        const auto speed = math::sqrt(vel.length2());//no need for hypot in this rough case

        // 防止速度过小导致除零 (近似认为它是直线或由极小dt处理)
        if (speed > std::numeric_limits<float>::epsilon()) {
            const auto dt = cap_length_src / speed;

            auto cap_start = instr;
            cap_start.segments = CAP_SEGMENTS;
            // 参数范围：[t_start - dt, t_start]
        	cap_start.margin.from = instr.margin.from - dt;
        	// 新的结束点：必须等于原起始点 -> (1.0 - margin.to = margin.from) -> margin.to = 1.0 - margin.from
        	cap_start.margin.to = 1.0f - instr.margin.from;
        	cap_start.stroke.to = cap_start.stroke.from;

            // 颜色处理：纵向淡入 (Longitudinal Fade In)
            // 假设 v00, v10 是该段几何体的“起始端”(Away from body)
            // 假设 v01, v11 是该段几何体的“结束端”(Connected to body)
            // 我们需要让起始端透明
        	cap_start.color.v00 = cap_start.color.v01;
        	cap_start.color.v10 = cap_start.color.v11;
            cap_start.color.v00.a = {}; // Transparent
            cap_start.color.v10.a = {}; // Transparent
            // v01, v11 保持原色 (Opaque)，与主体连接

            // 递归调用 curve，为线头添加侧向抗锯齿 (Lateral AA)
            curve(r, cap_start, fringe);
        }
    }

    // -----------------------------------------------------------------
    // 处理终点头 (End Cap) - 向前延伸
    // -----------------------------------------------------------------
    if (cap_length_dst > 0.0f) {
        const auto t_end = instr.margin.to;

        // 计算终点处的切线速度
        const auto vel = instr.param.calculate_derivative(t_end);
    	const auto speed = math::sqrt(vel.length2());

        if (speed > std::numeric_limits<float>::epsilon()) {
            const auto dt = cap_length_dst / speed;

            auto cap_end = instr;
            cap_end.segments = CAP_SEGMENTS;
            // 参数范围：[t_end, t_end + dt]
        	cap_end.margin.from = 1.f - t_end; // 即 1.0f - instr.margin.to
        	// 新的结束点：原终点往前延伸 dt -> (margin.to 减小 dt)
        	cap_end.margin.to = instr.margin.to - dt;

            // 颜色处理：纵向淡出 (Longitudinal Fade Out)
            // 起始端 (Connected to body) 保持实色
            // 结束端 (Away from body) 设为透明
            cap_end.color.v01 = cap_end.color.v00; // Transparent
            cap_end.color.v11 = cap_end.color.v10; // Transparent
            cap_end.color.v01.a = {}; // Transparent
            cap_end.color.v11.a = {}; // Transparent

            // 递归调用 curve
            curve(r, cap_end, fringe);
        }
    }
}

export
struct line_context{
private:
	renderer_frontend* renderer_{};
	instruction::line_node* buffer_{};
	unsigned count_{};
	unsigned capacity_{};

	float area_sum_{};

public:
	[[nodiscard]] line_context() = default;

	[[nodiscard]] explicit(false) line_context(renderer_frontend* renderer)
		: renderer_(renderer){
	}

	[[nodiscard]] explicit(false) line_context(renderer_frontend& renderer)
		: renderer_(&renderer){
	}

	FORCE_INLINE void push(const instruction::line_node& node){
		if (!empty()) {
			// 实时累加当前线段的叉积贡献
			// Cross product: x1*y2 - x2*y1
			const auto last = back().pos;
			area_sum_ += last.cross(node.pos);
		}

		if(count_ == capacity_){
			acquire_new((capacity_ + 1) * 2);

		}
		std::memcpy(buffer_ + count_, &node, sizeof(node));
		++count_;
	}

	FORCE_INLINE std::span<const instruction::line_node> get_nodes() const noexcept{
		return {buffer_, count_};
	}

	FORCE_INLINE void push(const math::vec2 pos, float stroke, graphic::color color){
		push({pos, stroke, 0, {color, color}});
	}

	FORCE_INLINE void dump_mid(const instruction::line_segments& head){
		renderer_->push(head, get_nodes());
	}

	FORCE_INLINE void dump_mid(const instruction::line_segments_closed& head){
		renderer_->push(head, get_nodes());
	}

	FORCE_INLINE instruction::line_node& add_cap_src(float stroke){
		const auto tan_front = (front_at(0).pos - front_at(1).pos).set_length(stroke);

		if(size() + 2 > capacity_){
			acquire_new(size() + 2);
			count_ += 2;
		}

		std::memmove(buffer_ + 2, buffer_, size() * sizeof(instruction::line_node));
		return patch_cap_src(tan_front);
	}

	FORCE_INLINE instruction::line_node& add_cap_dst(float stroke){
		const auto tan_back = (back_at(0).pos - back_at(1).pos).set_length(stroke);

		if(size() + 2 > capacity_){
			acquire_new(size() + 2);
			count_ += 2;
		}

		return patch_cap_dst(tan_back);
	}

	FORCE_INLINE math::section<instruction::line_node&> add_cap(float cap_src, float cap_dst) noexcept {
		const auto tan_front = (front_at(0).pos - front_at(1).pos).set_length(cap_src);
		const auto tan_back = (back_at(0).pos - back_at(1).pos).set_length(cap_dst);

		if(size() + 4 > capacity_){
			acquire_new(size() + 4);
			count_ += 4;
		}

		std::memmove(buffer_ + 2, buffer_, size() * sizeof(instruction::line_node));
		return {patch_cap_src(tan_front), patch_cap_dst(tan_back)};
	}

	FORCE_INLINE math::section<instruction::line_node&> add_cap() noexcept {
		 return add_cap(front().stroke / 2.f, back().stroke / 2.f);
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
	FORCE_INLINE void add_fringe_cap(float cap_stroke_src, float cap_stroke_dst){
		using namespace instruction;

		auto [src, dst] = add_cap(cap_stroke_src, cap_stroke_dst);
		src.color.invoke(&graphic::color::set_a, 0);
		dst.color.invoke(&graphic::color::set_a, 0);
	}

	FORCE_INLINE void dump_fringe_inner(const instruction::line_segments_closed& head, float stroke){
		auto cpy = acquire_copy();
		for_each_element(cpy, [&](const instruction::line_node& src, instruction::line_node& dst){
			dst.offset = src.offset - (src.stroke + stroke) * .5f;
			dst.stroke = stroke;
			dst.color.from = dst.color.to.make_transparent();
		});
		renderer_->push(head, std::span{cpy, size()});
	}

	FORCE_INLINE void dump_fringe_outer(const instruction::line_segments_closed& head, float stroke){
		auto cpy = acquire_copy();
		for_each_element(cpy, [&](const instruction::line_node& src, instruction::line_node& dst){
			dst.offset = src.offset + (src.stroke + stroke) * .5f;
			dst.stroke = stroke;
			dst.color.to = dst.color.from.make_transparent();
		});
		renderer_->push(head, std::span{cpy, size()});
	}

	FORCE_INLINE void dump_fringe_inner(const instruction::line_segments& head, float stroke){
		auto cpy = acquire_copy();
		for_each_element(cpy, [&](const instruction::line_node& src, instruction::line_node& dst){
			dst.offset = src.offset - (src.stroke + stroke) * .5f;
			dst.stroke = stroke;
			dst.color.from = dst.color.to.make_transparent();
		});
		renderer_->push(head, std::span{cpy, size()});
	}

	FORCE_INLINE void dump_fringe_outer(const instruction::line_segments& head, float stroke){
		auto cpy = acquire_copy();
		for_each_element(cpy, [&](const instruction::line_node& src, instruction::line_node& dst){
			dst.offset = src.offset + (src.stroke + stroke) * .5f;
			dst.stroke = stroke;
			dst.color.to = dst.color.from.make_transparent();
		});
		renderer_->push(head, std::span{cpy, size()});
	}

	FORCE_INLINE std::size_t size() const noexcept{
		return count_;
	}
		FORCE_INLINE auto& front(this auto& self) noexcept{
		assert(self.size() > 0);
		return self.buffer_[0];
	}

	FORCE_INLINE auto& front_at(this auto& self, unsigned where) noexcept{
		assert(self.size() > where);
		return self.buffer_[where];
	}

	FORCE_INLINE auto& back_at(this auto& self, unsigned where) noexcept{
		assert(self.size() > where);
		return self.buffer_[self.size() - where - 1];
	}

	FORCE_INLINE auto& back(this auto& self) noexcept{
		auto sz = self.size();
		assert(sz > 0);
		sz -= 1;
		return self.buffer_[sz];
	}

	FORCE_INLINE bool empty() const noexcept{
		return count_ == 0;
	}


private:
	instruction::line_node& patch_cap_src(math::vec2 mov) noexcept{
		buffer_[1] = buffer_[2];
		buffer_[1].pos += mov;
		buffer_[0] = buffer_[1];
		buffer_[0].pos += mov;
		return buffer_[1];
	}

	instruction::line_node& patch_cap_dst(math::vec2 mov) noexcept{
		buffer_[count_ - 2] = buffer_[count_ - 3];
		buffer_[count_ - 2].pos += mov;
		buffer_[count_ - 1] = buffer_[count_ - 2];
		buffer_[count_ - 1].pos += mov;
		return buffer_[count_ - 2];
	}

	void acquire_new(std::size_t new_capacity){
		capacity_ = new_capacity;
		buffer_ = renderer_->acquire_buffer<instruction::line_node>(new_capacity);
	}

	FORCE_INLINE instruction::line_node* acquire_copy(){
		const auto sz = size();

		buffer_ = renderer_->acquire_buffer<instruction::line_node>(sz * 2);
		const auto buf = buffer_ + count_;
		std::memcpy(buf, buffer_, count_ * sizeof(instruction::line_node));
		return buf;
	}

	template <std::invocable<instruction::line_node&, instruction::line_node&> Fn>
	FORCE_INLINE void for_each_element(instruction::line_node* copy_src, Fn fn) noexcept(std::is_nothrow_invocable_v<Fn, instruction::line_node&, instruction::line_node&>){
		auto sz = size();
		for(unsigned i = 0; i < sz; ++i){
			std::invoke(fn, buffer_[i], copy_src[i]);
		}
	}


	template <bool closed>
	[[nodiscard]] FORCE_INLINE bool is_ccw() const noexcept {
		float final_area = area_sum_;
		if (closed) {
			assert(!empty());
			final_area += back().pos.cross(front().pos);
		}
		return final_area > 0.0f;
	}
};

}
