module;

#include <cassert>
#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.graphic.trail;

export import mo_yanxi.math.vector2;
export import mo_yanxi.math.rect_ortho;

import mo_yanxi.math;
import mo_yanxi.timer;
import mo_yanxi.concepts;
import mo_yanxi.circular_queue;
import mo_yanxi.slide_window_buf;

import std;

namespace mo_yanxi::graphic{

struct node{
	math::vec2 pos;
	float scale;

	FORCE_INLINE constexpr friend node lerp(const node lhs, const node rhs, const float p) noexcept{
		return node{
			math::lerp(lhs.pos, rhs.pos, p),
			math::lerp(lhs.scale, rhs.scale, p)
		};
	}
};

template <typename T>
concept trail_foreach_func = std::invocable<T, const node&, unsigned, unsigned>;

template <std::size_t N, std::size_t Stride, std::ranges::contiguous_range R>
constexpr auto make_sliding_spans(R&& range) noexcept {
    static_assert(Stride > 0, "Stride must be greater than 0");

    using ElementType = std::remove_reference_t<std::ranges::range_reference_t<R>>;

    const std::size_t total_size = std::ranges::size(range);

    const std::ptrdiff_t window_count = (total_size >= N) ? ((total_size - N + 1) / Stride) : 0;

    ElementType* base_ptr = std::ranges::data(range);

    return std::views::iota(std::ptrdiff_t{0}, window_count) |
           std::views::transform([base_ptr](std::ptrdiff_t i) {
               return std::span<ElementType, N>(base_ptr + i * Stride, N);
           });
}

export
struct trail{
	using vec_t = math::vec2;

	using node_type = node;
	using data_type = circular_queue<node_type, false, unsigned>;

	using size_type = data_type::size_type;

protected:
	data_type points{};

public:
	/**
	 * @return Distance between head and tail
	 */
	[[nodiscard]] float get_dst() const noexcept {
		if (points.empty()) return 0.0f;
		return head().pos.dst(tail().pos);
	}

	/**
	 * @brief Not accurate, but enough
	 */
	[[nodiscard]] math::frect get_bound() const noexcept {
		if (points.empty()) return math::frect{};
		return math::frect{tags::from_vertex, head().pos, tail().pos};
	}

	explicit operator bool() const noexcept{
		return points.capacity() > 0;
	}

	[[nodiscard]] trail() = default;

	[[nodiscard]] explicit trail(const data_type::size_type length)
		: points(length){
	}

	[[nodiscard]] float last_scale() const noexcept{
		return head().scale;
	}

	[[nodiscard]] node_type head() const noexcept{
		return points.front_or({});
	}

	[[nodiscard]] node_type tail() const noexcept{
		return points.back_or({});
	}

	[[nodiscard]] math::vec2 head_pos_or(math::vec2 p) const noexcept{
		if(points.empty()) return p;
		return points.front().pos;
	}

	[[nodiscard]] math::vec2 tail_pos_or(math::vec2 p) const noexcept{
		if(points.empty()) return p;
		return points.back().pos;
	}


	[[nodiscard]] float head_angle() const noexcept {
		if(points.size() < 2) return 0.0f;

		const vec_t secondPoint = points[1].pos;
		return secondPoint.angle_to_rad(head().pos);
	}

	template <bool ShrinkOnStall = false>
	void push(const vec_t pos, float scale = 1.0f, float min_sqr_dst = 1.0f) {
		assert(points.capacity() > 0);

		if (!points.empty() && min_sqr_dst > 0.0f) {
			if (head().pos.dst2(pos) < min_sqr_dst) {
				if constexpr (ShrinkOnStall){
					points.pop_back();
				}
				return;
			}
		}

		push_unchecked(pos, scale);
	}

	void push_unchecked(const vec_t pos, float scale = 1.0f) {
		assert(points.capacity() > 0);

		if (points.full()) {
			points.pop_back();
		}

		points.emplace_front(pos, scale);
	}

	void pop() noexcept{
		if(!points.empty()){
			points.pop_back();
		}
	}

	data_type drop() && noexcept{
		return std::move(points);
	}

	void clear() noexcept{
		points.clear();
	}

	[[nodiscard]] auto size() const noexcept{
		return points.size();
	}

	[[nodiscard]] auto capacity() const noexcept{
		return points.capacity();
	}

	[[nodiscard]] vec_t get_head_pos() const noexcept{
		return head().pos;
	}

	template <typename Func>
	FORCE_INLINE void each(const float radius, Func consumer, float percent = 1.f) const noexcept{
		//TODO make it a co-routine to maintain better reserve and callback?
		if(points.empty()) return;
		percent = math::clamp(percent);

		float lastAngle{};
		const float capSize = static_cast<float>(this->size() - 1) * percent;

		auto drawImpl = [&] FORCE_INLINE (
			int index,
			const node_type prev,
			const node_type next,
			const float prevProg,
			const float nextProg

		) -> float{
			const auto dst = next.pos - prev.pos;
			const auto scl = math::curve(dst.length(), 0.f, 0.5f) * radius * prev.scale / capSize;
			const float z2 = -(dst).angle_rad();

			const float z1 = lastAngle;

			const vec_t c = vec_t::from_polar_rad(math::pi_half - z1, scl * prevProg);
			const vec_t n = vec_t::from_polar_rad(math::pi_half - z2, scl * nextProg);

			if(n.equals({}, 0.01f) && c.equals({}, 0.01f)){
				return z2;
			}

			if constexpr(std::invocable<Func, int, vec_t, vec_t, vec_t, vec_t, float, float>){
				float progressFormer = prevProg / capSize;
				float progressLatter = nextProg / capSize;

				consumer(index, prev.pos - c, prev.pos + c, next.pos + n, next.pos - n, progressFormer, progressLatter);
			} else if constexpr(std::invocable<Func, int, vec_t, vec_t, vec_t, vec_t>){
				consumer(index, prev.pos - c, prev.pos + c, next.pos + n, next.pos - n);
			} else if constexpr(std::invocable<Func, vec_t, vec_t, vec_t, vec_t, float, float>){
				float progressFormer = prevProg / capSize;
				float progressLatter = nextProg / capSize;

				consumer(prev.pos - c, prev.pos + c, next.pos + n, next.pos - n, progressFormer, progressLatter);
			} else if constexpr(std::invocable<Func, vec_t, vec_t, vec_t, vec_t>){
				consumer(prev.pos - c, prev.pos + c, next.pos + n, next.pos - n);
			} else{
				static_assert(static_assert_trigger<Func>::value, "consumer not supported");
			}

			return z2;
		};

		const float pos = (1 - percent) * size();
		const float ceil = std::floor(pos) + 1;
		const auto initial = static_cast<size_type>(ceil);
		const auto initialFloor = initial - 1;
		const auto prog = ceil - pos;
		data_type::size_type i = initial;

		if(i >= points.size()) return;

		node_type nodeNext = points[i];
		node_type nodeCurrent = lerp(nodeNext, points[initialFloor], prog);

		lastAngle = drawImpl(0, nodeCurrent, nodeNext, 0, prog);

		for(; i < points.size() - 1; ++i){
			nodeCurrent = points[i];
			nodeNext = points[i + 1];

			if(nodeCurrent.scale <= 0.001f && nodeNext.scale <= 0.001f) continue;

			const float cur = i - initial + prog;
			lastAngle = drawImpl(i - initialFloor, nodeCurrent, nodeNext, cur, cur + 1.f);
		}
	}

	template <template <typename > typename Generator, std::invocable<node_type, size_type, size_type, std::uintptr_t>
		Func>
	FORCE_INLINE Generator<std::invoke_result_t<Func, node_type, size_type, size_type, std::uintptr_t>> trivial_each(
		float percent,
		Func consumer
	) const noexcept{
		percent = math::clamp(percent);

		const auto len = static_cast<size_type>(points.size() * percent);
		if(!len) co_return;

		for(size_type idx{}; idx < len; ++idx){
			auto ptr = points.data_at(idx);
			co_yield std::invoke(consumer, *ptr, idx, len, std::bit_cast<std::uintptr_t>(ptr));
		}
	}

	template <trail_foreach_func Func>
	FORCE_INLINE void trivial_each_2(
		float percent,
		Func consumer
	) const noexcept{
		percent = math::clamp(percent);

		const auto len = static_cast<size_type>(points.size() * percent);
		if(!len) return;

		for(size_type idx{}; idx < len; ++idx){
			auto ptr = points.data_at(idx);
			std::invoke(consumer, *ptr, idx, len);
		}
	}

	template <trail_foreach_func Func, std::invocable<std::invoke_result_t<Func, const node_type&, size_type, size_type>> CallBack>
	FORCE_INLINE void trivial_each(
		float percent,
		Func consumer,
		CallBack output_callback
	) const noexcept{
		percent = math::clamp(percent);

		const auto len = static_cast<size_type>(points.size() * percent);
		if(!len) return;

		for(size_type idx{}; idx < len; ++idx){
			auto ptr = points.data_at(idx);
			std::invoke(output_callback, std::invoke(consumer, *ptr, idx, len));
		}
	}


	template <typename Func>
	FORCE_INLINE void slide_each(
		const math::vec2 head_indicator_vec,
		Func consumer,
		float percent = 1.f) const noexcept{
		percent = math::clamp(percent);

		const auto len = static_cast<size_type>(points.size() * percent);
		if(len < 3U) return;
		const auto total = len - 3U;
		const auto factor_total = total + 1;

		size_type idx{};
		for(; idx < total; ++idx){
			std::invoke(consumer, points[idx].pos, points[idx + 1].pos, points[idx + 2].pos, points[idx + 3].pos, idx,
				factor_total, points[idx + 1].scale, points[idx + 2].scale);
		}

		std::invoke(consumer, points[idx].pos, points[idx + 1].pos, points[idx + 2].pos,
			points[idx + 2].pos + head_indicator_vec, idx, factor_total, points[idx + 1].scale, points[idx + 2].scale);
	}

	template <
		typename Cons,
		typename Prov
	>
		requires requires{
			requires std::invocable<
				Cons,
				math::vec2, math::vec2, math::vec2, math::vec2,
				size_type, size_type,
				float, float,
				std::array<std::invoke_result_t<Prov, size_type, size_type, std::uintptr_t>, 4>
			>;
			requires std::invocable<Prov, size_type, size_type, std::uintptr_t>;
		}
	FORCE_INLINE void slide_each(
		const math::vec2 head_indicator_vec,
		Cons consumer,
		Prov prov,
		float percent = 1.f) const noexcept{
		percent = math::clamp(percent);

		const auto len = static_cast<size_type>(points.size() * percent);
		if(len < 3U) return;
		const auto adjoinLen = len + 1;
		const auto total = len - 3U;
		const auto factor_total = total + 1;

		std::array adjoint{
				prov(0, adjoinLen, std::bit_cast<std::uintptr_t>(points.data_at(0))),
				prov(1, adjoinLen, std::bit_cast<std::uintptr_t>(points.data_at(1))),
				prov(2, adjoinLen, std::bit_cast<std::uintptr_t>(points.data_at(2))),
				prov(3, adjoinLen, std::bit_cast<std::uintptr_t>(points.data_at(3))),
			};

		size_type idx{};
		for(; idx < total; ++idx){
			std::invoke(
				consumer,
				points[idx].pos, points[idx + 1].pos, points[idx + 2].pos, points[idx + 3].pos,
				idx, factor_total,
				points[idx + 1].scale, points[idx + 2].scale,
				adjoint
			);

			std::ranges::move(++std::ranges::begin(adjoint), std::ranges::end(adjoint), std::ranges::begin(adjoint));
			adjoint[3] = prov(idx + 4, adjoinLen, std::bit_cast<std::uintptr_t>(points.data_at(idx + 4)));
		}

		std::invoke(
			consumer,
			points[idx].pos, points[idx + 1].pos, points[idx + 2].pos, points[idx + 2].pos + head_indicator_vec,
			idx, factor_total,
			points[idx + 1].scale, points[idx + 2].scale,
			adjoint
		);
	}

	/**
	 * @tparam Func void(CapPos, radius, angle)
	 */
	template <std::invocable<vec_t, float, float> Func>
	void cap(const float width, Func consumer) const noexcept{
		if(size() > 0){
			const auto size = this->size();
			auto [pos, w] = points.back();
			w = w * width * (1.0f - 1.0f / static_cast<float>(size)) * 2.0f;
			if(w <= 0.001f) return;

			std::invoke(consumer, pos, w, head_angle());
		}
	}

	template <
		std::size_t Stride = 1,
		std::size_t WindowSize = 4,
		trail_foreach_func NodeProv,
		std::invocable<
			std::span<std::invoke_result_t<NodeProv, const node_type&, size_type, size_type>,
				WindowSize>> NodeCons>
	void iterate(float percent, NodeProv nodeProv, NodeCons nodeCons) const {
		using ResultType = std::invoke_result_t<NodeProv, const node_type&, size_type, size_type>;

		using BufferTy = slide_window_buffer<ResultType, WindowSize + Stride * 4 + (Stride - 1), WindowSize - 1>;
		BufferTy buffer{};

		this->trivial_each(percent, std::move(nodeProv), [&](ResultType&& rst){
			if (buffer.push_back(std::move(rst))) {
				for(auto&& nodes : graphic::make_sliding_spans<WindowSize, Stride>(buffer)){
					std::invoke(nodeCons, nodes);
				}
				buffer.advance();
			}
		});

		if(buffer.finalize()){
			for(auto&& nodes : graphic::make_sliding_spans<WindowSize, Stride>(buffer)){
				std::invoke(nodeCons, nodes);
			}
		}
	}
};

export
struct uniformed_trail : trail{
private:
	timer<float, 2> interval{};

public:
	float spacing{1.f};
	float shrink_interval{1.f};

	using trail::trail;

	[[nodiscard]] uniformed_trail(const data_type::size_type length, const float spacing_in_tick)
		: trail(length),
		spacing(spacing_in_tick), shrink_interval{spacing_in_tick}{
	}


	[[nodiscard]] float estimate_duration(float speed) const noexcept{
		return get_dst() / speed * spacing * size() * 16;
	}

	void update(const float delta_tick, const vec_t pos, float scale = 1.0f, float min_sqr_dst = 1.0f) {
		// 判断是否处于有效移动状态
		bool is_moving = points.empty() || min_sqr_dst <= 0.0f || head().pos.dst2(pos) >= min_sqr_dst;

		if (is_moving) {
			// 移动时重置消散计时器，防止后续停止时发生时间跳跃
			interval.clear(1);

			if (!spacing || interval.update_and_get(0, spacing, delta_tick)) {
				// 调用底层已做过校验的 push
				push_unchecked(pos, scale);
			}
		} else {
			// 停止时重置生成计时器，防止重新起步时瞬间堆积点
			interval.clear(0);

			// 运用 Strict = true 模式，由 timer 自动基于真实时间进行追帧循环
			interval.update_and_run<true>(1, shrink_interval, delta_tick, [this]() noexcept {
				if (!points.empty()) {
					points.pop_back();
				}
			});
		}
	}
};


}
