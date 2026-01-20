module;

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.gui.fringe;

export import mo_yanxi.gui.renderer.frontend;
export import mo_yanxi.graphic.draw.instruction;

import byte_pool;

import std;


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
template <typename Alloc>
struct line_context{
private:
	// 使用 byte_borrow 管理生命周期和内存
	byte_borrow<Alloc> buf_{};

	// 叉积累加，用于判断顺逆时针
	float area_sum_{};

public:
	[[nodiscard]] line_context() = default;

	// 需要 pool 来进行内存分配
	[[nodiscard]] explicit(false) line_context(byte_pool<Alloc>& pool, renderer_frontend* renderer = nullptr)
		: buf_(pool.borrow(0)){
	}


	FORCE_INLINE void push(const instruction::line_node& node){
		// 确保有空间
		const auto current_idx = size();
		// acquire_new 会处理容量检查、扩容和数据迁移
		// 注意：根据上一轮的修复，这里传入的是需要的元素总数
		buf_.template acquire_new<instruction::line_node>(current_idx + 1);

		if (current_idx > 0) {
			// 实时累加当前线段的叉积贡献
			// Cross product: x1*y2 - x2*y1
			const auto& last = data()[current_idx - 1].pos;
			area_sum_ += last.cross(node.pos);
		}

		// 写入新数据
		data()[current_idx] = node;
	}

	FORCE_INLINE std::span<const instruction::line_node> get_nodes() const noexcept{
		return buf_.get().template to_span<const instruction::line_node>();
	}

	FORCE_INLINE void push(const math::vec2 pos, float stroke, graphic::color color){
		push({pos, stroke, 0, {color, color}});
	}

	FORCE_INLINE void dump_mid(renderer_frontend& renderer, const instruction::line_segments& head){
		renderer.push(head, get_nodes());
	}

	FORCE_INLINE void dump_mid(renderer_frontend& renderer, const instruction::line_segments_closed& head){
		renderer.push(head, get_nodes());
	}

	FORCE_INLINE instruction::line_node& add_cap_src(float stroke){
		const auto sz = size();
		// 确保有两个额外的空间
		buf_.template acquire_new<instruction::line_node>(sz + 2);

		auto* ptr = data();
		// 移动现有数据腾出头部两个位置: [0...N] -> [2...N+2]
		std::memmove(ptr + 2, ptr, sz * sizeof(instruction::line_node));

		// 计算切线并填充头部
		// 注意：移动后原数据在 ptr+2 位置
		// 原来的 front(0) 现在是 ptr[2], front(1) 是 ptr[3]
		const auto tan_front = (ptr[2].pos - ptr[3].pos).set_length(stroke);
		return patch_cap_src(tan_front);
	}

	FORCE_INLINE instruction::line_node& add_cap_dst(float stroke){
		const auto sz = size();
		buf_.template acquire_new<instruction::line_node>(sz + 2);

		auto* ptr = data();
		// 尾部增加不需要移动数据，直接计算
		// 原来的 back(0) 是 ptr[sz-1], back(1) 是 ptr[sz-2]
		const auto tan_back = (ptr[sz - 1].pos - ptr[sz - 2].pos).set_length(stroke);

		return patch_cap_dst(tan_back, sz); // 传入旧的大小作为基准
	}

	FORCE_INLINE math::section<instruction::line_node&> add_cap(float cap_src, float cap_dst) noexcept {
		const auto sz = size();
		// 确保有四个额外的空间
		buf_.template acquire_new<instruction::line_node>(sz + 4);

		auto* ptr = data();

		// 1. 保存需要计算切线的原始点 (移动前)
		// 头部切线向量：原 [0] - [1]
		auto vec_front = ptr[0].pos - ptr[1].pos;
		// 尾部切线向量：原 [sz-1] - [sz-2]
		auto vec_back = ptr[sz-1].pos - ptr[sz-2].pos;

		// 2. 整体移动数据腾出头部2个位置 [0...N] -> [2...N+2]
		// 尾部留出的2个位置自然在 [N+2, N+3]
		std::memmove(ptr + 2, ptr, sz * sizeof(instruction::line_node));

		// 3. 计算切线
		const auto tan_front = vec_front.set_length(cap_src);
		const auto tan_back = vec_back.set_length(cap_dst);

		// 4. Patch
		// patch_cap_src 处理头部 (index 0, 1)，引用 index 2
		auto& src_node = patch_cap_src(tan_front);

		// patch_cap_dst 处理尾部 (index N+2, N+3)，引用 index N+1
		// 现在的总大小是 sz + 4
		auto& dst_node = patch_cap_dst(tan_back, sz + 2);

		return {src_node, dst_node};
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
		using namespace graphic::draw::instruction;
		auto [src, dst] = add_cap(cap_stroke_src, cap_stroke_dst);
		src.color.invoke(&graphic::color::set_a, 0);
		dst.color.invoke(&graphic::color::set_a, 0);
	}

	FORCE_INLINE void dump_fringe_inner(renderer_frontend& renderer, const instruction::line_segments_closed& head, float stroke){
		dump_fringe_impl(renderer, head, stroke, true);
	}

	FORCE_INLINE void dump_fringe_outer(renderer_frontend& renderer, const instruction::line_segments_closed& head, float stroke){
		dump_fringe_impl(renderer, head, stroke, false);
	}

	FORCE_INLINE void dump_fringe_inner(renderer_frontend& renderer, const instruction::line_segments& head, float stroke){
		dump_fringe_impl(renderer, head, stroke, true);
	}

	FORCE_INLINE void dump_fringe_outer(renderer_frontend& renderer, const instruction::line_segments& head, float stroke){
		dump_fringe_impl(renderer, head, stroke, false);
	}

	FORCE_INLINE std::size_t size() const noexcept{
		return buf_.get().template size<instruction::line_node>();
	}

	FORCE_INLINE instruction::line_node* data() noexcept{
		return buf_.template data<instruction::line_node>();
	}

	FORCE_INLINE const instruction::line_node* data() const noexcept{
		return buf_.template data<instruction::line_node>();
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
	// 通用的 Fringe 处理逻辑
	template<typename HeadType>
	FORCE_INLINE void dump_fringe_impl(renderer_frontend& renderer, const HeadType& head, float stroke, bool is_inner) {

		// 1. 从 Pool 借用临时缓冲区 (Copy on write)
		// 只需要当前大小的空间
		// owner_ 可能为空吗？如果 buf_ 是默认构造的，owner_ 是 nullptr。
		// 这里假设 line_context 初始化正确。
		assert(buf_.data() != nullptr); // 确保有数据才 dump

		// 通过 owner 借用新块
		// 这是一个RAII对象，函数结束自动归还
		// 注意：我们需要 byte_pool 的指针。buf_ 有 owner_ 指针。
		// 这里假设 byte_borrow 暴露了 owner_ 或者我们可以通过某种方式访问。
		// 查看 byte_borrow 定义，owner_ 是私有的，也没有 get_owner()。
		// 但 line_context 在 byte_pool 模块的友元列表中吗？不是。
		// *修正*: byte_borrow 在 byte_pool.ixx 中定义，我们这里是 fringe.ixx。
		// 我们无法直接访问 owner_。
		// *解决方案*: 我们必须依赖 buf_.acquire_new 的行为吗？不行，那是修改自己。
		// 我们必须在 line_context 中存储 byte_pool 的引用或指针吗？
		// 为了不改变太多结构，我们可以利用 byte_borrow 的 move 语义或者假设 buf_ 提供了某种 clone 机制？
		// 既然无法访问 owner_，我们只能假设 line_context 持有 pool 引用，或者
		// 我们得改 byte_borrow 增加 get_owner()。
		// 鉴于不能改 byte_pool 定义（假设），我们这里在 Ctor 中必须保存 pool 指针吗？
		// 查看 context: byte_borrow 的 owner_ 是 private 的。
		// 但是！ `byte_buffer` 声明了 `byte_borrow` 为友元。`byte_borrow` 没有声明 `line_context` 为友元。
		// *HACK*: 让我们假设 line_context 可以保存一个 byte_pool 指针。
		// 我将在类成员中添加 `byte_pool<Alloc>* pool_{};`


		// 借用临时 buffer
		byte_borrow<Alloc> temp_borrow = buf_.owner().borrow(size() * sizeof(instruction::line_node));

		// 拷贝数据
		auto src_span = get_nodes();
		auto dst_ptr = temp_borrow.template data<instruction::line_node>();
		std::ranges::copy(src_span, dst_ptr);

		// 修改数据
		const auto sz = size();
		for(std::size_t i=0; i<sz; ++i) {
			const auto& src = src_span[i];
			auto& dst = dst_ptr[i];

			if (is_inner) {
				dst.offset = src.offset - (src.stroke + stroke) * .5f;
				dst.color.from = dst.color.to.make_transparent();
			} else {
				dst.offset = src.offset + (src.stroke + stroke) * .5f;
				dst.color.to = dst.color.from.make_transparent();
			}
			dst.stroke = stroke;
		}

		// 提交
		renderer.push(head, std::span{dst_ptr, sz});
	}

	instruction::line_node& patch_cap_src(math::vec2 mov) noexcept{
		auto* ptr = data();
		ptr[1] = ptr[2];
		ptr[1].pos += mov;
		ptr[0] = ptr[1];
		ptr[0].pos += mov;
		return ptr[1];
	}

	// base_idx 是未添加 cap 前的原始大小（或者是数据的末尾索引）
	// patch_cap_dst 实际上是在末尾追加两个点，基于原末尾点计算
	instruction::line_node& patch_cap_dst(math::vec2 mov, std::size_t base_idx) noexcept{
		auto* ptr = data();
		// base_idx 是新数据的起始位置（即原数组的 size）
		// 我们需要基于 ptr[base_idx - 1] (原最后一个点) 来生成
		// ptr[base_idx] 和 ptr[base_idx + 1]

		// 注意：原代码逻辑是
		// buffer_[count_ - 2] = buffer_[count_ - 3];
		// buffer_[count_ - 1] = buffer_[count_ - 2];
		// 这里 count_ 已经是 +2 之后的大小了。

		// 对应到现在：
		// current total size = base_idx + 2
		// target indices: base_idx, base_idx + 1
		// source index: base_idx - 1

		ptr[base_idx] = ptr[base_idx - 1];
		ptr[base_idx].pos += mov;

		ptr[base_idx + 1] = ptr[base_idx];
		ptr[base_idx + 1].pos += mov;

		return ptr[base_idx];
	}

	template <bool closed>
	[[nodiscard]] FORCE_INLINE bool is_ccw() const noexcept {
		float final_area = area_sum_;
		if (closed && !empty()) {
			final_area += back().pos.cross(front().pos);
		}
		return final_area > 0.0f;
	}

};


}
