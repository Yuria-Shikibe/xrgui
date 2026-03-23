//
// Created by Matrix on 2026/3/22.
//

export module mo_yanxi.gui.style.progress_bars;

import std;
import mo_yanxi.gui.elem.progress_bar;
import mo_yanxi.graphic.draw.instruction;
import mo_yanxi.gui.fx.fringe;
import mo_yanxi.gui.fx.instruction_extension;

namespace mo_yanxi::gui::style{
export
struct ring_progress : progress_drawer {
    float thickness{ 6.0f }; // 默认环宽度

    using progress_drawer::progress_drawer;

protected:
    void draw_layer_impl(const progress_bar& element, math::frect region, float opacityScl, fx::layer_param layer_param) const override{

    if (layer_param != 0) return;

    using namespace graphic;

    const auto prog = element.progress.get_current();
    const auto state = element.progress.get_state();
    const auto center = region.get_center();

    // 取宽高的最小值的二分之一作为可用空间半径，再减去线宽的一半以防边缘被裁剪
    const auto available_radius = std::min(region.extent().x, region.extent().y) / 2.0f;
    const auto radius = math::fdim(available_radius, thickness / 2.0f);

    if (radius <= 0.0f) return;

    const auto inner = radius - thickness / 2.0f;
    const auto outer = radius + thickness / 2.0f;

    float start_angle = 0.0f;
    float sweep_angle = 0.0f;

    // 材质颜色：若为 rough 状态，为了避免颜色闪烁通常使用固定色(src)；否则支持渐变色
    color vertex_color_start = element.draw_config.color.src;
    color vertex_color_end = element.draw_config.color.dst;

    if (state == progress_state::rough) {
        // --- 无极旋转状态 (Indeterminate Ring) ---
        const float time = element.get_scene().get_current_time() / 60.f;

        // 定义完整的收缩/展开周期时间
        const float cycle_duration = 1.5f;
        const float t_normalized = std::fmod(time, cycle_duration) / cycle_duration; // 0.0 ~ 1.0
        const float cycle_count = std::floor(time / cycle_duration);

        // 【核心修复】：每次收缩阶段尾部都会前进0.75圈，因此下一个周期的基础起点必须加上0.75以保持连续性
        const float base_rot = cycle_count * 0.75f + time * 0.2f;

        // 根据周期前半段和后半段，使用余弦缓动 (ease-in-out) 计算伸缩
        if (t_normalized < 0.5f) {
            // 展开阶段：头部前进，尾部保持在 base_rot
            const float local_t = t_normalized * 2.0f;
            const float ease = (1.0f - std::cos(local_t * math::pi)) * 0.5f;
            sweep_angle = 0.05f + 0.75f * ease;
            start_angle = base_rot;
        } else {
            // 收缩追赶阶段：尾部追赶头部，尾部额外前进 0.75 圈
            const float local_t = (t_normalized - 0.5f) * 2.0f;
            const float ease = (1.0f - std::cos(local_t * math::pi)) * 0.5f;
            sweep_angle = 0.8f - 0.75f * ease;
            start_angle = base_rot + 0.75f * ease;
        }
    } else {
        // --- 常规进度条状态 (Determinate Ring) ---
        if (prog < 0.001f) return;

        start_angle = -0.25f; // 默认从正上方 (12点钟方向) 开始，假设 0 是3点钟方向，-0.25代表 -90度
        sweep_angle = prog;
        vertex_color_end = element.draw_config.color[prog];
    }

    // 动态计算平滑边缘所需要的顶点数
    std::uint32_t count = fx::get_smooth_circle_vertex_count(radius, std::abs(sweep_angle));
    const auto cap_size = math::curve(std::abs(sweep_angle), 0.0f, 0.05f);

    // 当普通进度条达到 100% 满环时
    if (state != progress_state::rough && prog >= 1.0f) {
        fx::fringe::poly(element.renderer(), draw::instruction::poly{
            .pos = center,
            .segments = count,
            .initial_angle = start_angle,
            .radius = {inner, outer},
            .color = {
                vertex_color_start, vertex_color_end
            }
        }, 1.5f);
        return;
    }

    // 渲染带有圆角的局部弧段
    fx::fringe::poly_partial_with_cap(element.renderer(), draw::instruction::poly_partial{
            .pos = center,
            .segments = count,
            .range = {start_angle, sweep_angle},
            .radius = {inner, outer},
            .color = {
                vertex_color_start, vertex_color_start,
                vertex_color_end, vertex_color_end
            }
        }, 1.5f * cap_size, 1.5f * cap_size, 1.5f);
    }
};
}
