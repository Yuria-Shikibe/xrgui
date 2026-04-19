export module mo_yanxi.gui.renderer.frontend:color_stack;

import mo_yanxi.math;
import mo_yanxi.graphic.color;
import mo_yanxi.gui.alloc;
import std;

namespace mo_yanxi::gui {

export
struct alignas(32) color_modifier {
	graphic::color color;
	float influence;
	float intensity;            // 相对 1 的偏移值
	float intensity_influence;  // 专属强度的等效累积权重
};

export
struct alignas(32) accumulated_state {
	using tag_vertex_only = void;

	graphic::color overlay_color; // 16B: 累积的叠加色 (RGB 已预乘 intensity_mult)
	graphic::color base_mult;     // 16B: 累积的底色乘数 (RGB 已预乘 intensity_mult)
};

export
class color_stack {
private:
	mr::vector<accumulated_state> stack_states;

public:
	color_stack() {
		stack_states.emplace_back(accumulated_state{
			.overlay_color = graphic::color{},
			.base_mult = graphic::color{1.0f, 1.0f, 1.0f, 1.0f}
		});
	}

	// 同时推入颜色和强度修饰符 (Alpha覆盖混合 + 强度折叠)
	void push(const color_modifier& mod) {
		const auto& current = stack_states.back();

		float eff_mult = math::cpo::fma(mod.intensity, mod.intensity_influence, 1.0f);
		float remain_ratio = 1.0f - mod.influence;

		accumulated_state next_state;

		// 统一处理：整体乘以 remain_ratio，RGB 通道额外折叠 eff_mult
		next_state.base_mult = current.base_mult.copy()
		                              .mul(remain_ratio)
		                              .mul_rgb(eff_mult);

		next_state.overlay_color = current.overlay_color.copy()
		                                  .mul(remain_ratio)
		                                  .add(mod.color.copy().mul(mod.influence))
		                                  .mul_rgb(eff_mult);

		stack_states.push_back(next_state);
	}

	// 仅推入颜色和影响 (常规 Alpha 覆盖混合，保持当前强度比例)
	void push_color(const graphic::color& col, float influence) {
		const auto& current = stack_states.back();
		float remain_ratio = 1.0f - influence;

		accumulated_state next_state;

		next_state.base_mult = current.base_mult.copy().mul(remain_ratio);
		next_state.overlay_color = current.overlay_color.copy()
		                                  .mul(remain_ratio)
		                                  .add(col.copy().mul(influence));

		stack_states.push_back(next_state);
	}

	// 新增：推入乘算滤镜色 (正片叠底 / Tint)
	void push_multiply_color(const graphic::color& mult) {
		const auto& current = stack_states.back();


		accumulated_state next_state;
		next_state.base_mult = current.base_mult.copy().mul(mult);
		next_state.overlay_color = current.overlay_color.copy().mul(mult);

		stack_states.push_back(next_state);
	}

	// 仅推入强度和影响 (只折叠进 RGB，不改变 Alpha)
	void push_intensity(float intensity_offset, float influence) {
		const auto& current = stack_states.back();
		float eff_mult = math::cpo::fma(intensity_offset, influence, 1.0f);

		accumulated_state next_state = current; // 拷贝Alpha通道
		next_state.base_mult.mul_rgb(eff_mult);
		next_state.overlay_color.mul_rgb(eff_mult);

		stack_states.push_back(next_state);
	}

	void pop() {
		if (stack_states.size() > 1) {
			stack_states.pop_back();
		}
	}

	void clear() {
		stack_states.resize(1);
	}

	[[nodiscard]] const accumulated_state& top() const noexcept {
		return stack_states.back();
	}

	[[nodiscard]] std::size_t size() const noexcept {
		return stack_states.size() - 1;
	}
};

} // namespace mo_yanxi::gui