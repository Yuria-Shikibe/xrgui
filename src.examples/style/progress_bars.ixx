//

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
    float thickness{ 6.0f };

    using progress_drawer::progress_drawer;

protected:
    void draw_layer_impl(const progress_bar& element, math::frect region, float opacityScl, fx::layer_param layer_param) const override{

    if (layer_param != 0) return;

    using namespace graphic;

    const auto prog = element.progress.get_current();
    const auto state = element.progress.get_state();
    const auto center = region.get_center();


    const auto available_radius = std::min(region.extent().x, region.extent().y) / 2.0f;
    const auto radius = math::fdim(available_radius, thickness / 2.0f);

    if (radius <= 0.0f) return;

    const auto inner = radius - thickness / 2.0f;
    const auto outer = radius + thickness / 2.0f;

    float start_angle = 0.0f;
    float sweep_angle = 0.0f;


    color vertex_color_start = element.draw_config.color.src.copy().mul_a(opacityScl);
    color vertex_color_end = element.draw_config.color.dst.copy().mul_a(opacityScl);

    if (state == progress_state::rough) {

        const float time = element.get_scene().get_current_time() / 60.f;


        const float cycle_duration = 1.5f;
        const float t_normalized = std::fmod(time, cycle_duration) / cycle_duration;
        const float cycle_count = std::floor(time / cycle_duration);


        const float base_rot = cycle_count * 0.75f + time * 0.2f;


        if (t_normalized < 0.5f) {

            const float local_t = t_normalized * 2.0f;
            const float ease = (1.0f - std::cos(local_t * math::pi)) * 0.5f;
            sweep_angle = 0.05f + 0.75f * ease;
            start_angle = base_rot;
        } else {

            const float local_t = (t_normalized - 0.5f) * 2.0f;
            const float ease = (1.0f - std::cos(local_t * math::pi)) * 0.5f;
            sweep_angle = 0.8f - 0.75f * ease;
            start_angle = base_rot + 0.75f * ease;
        }
    } else {

        if (prog < 0.001f) return;

        start_angle = -0.25f;
        sweep_angle = prog;
        vertex_color_end = element.draw_config.color[prog];
    }


    std::uint32_t count = fx::get_smooth_circle_vertex_count(radius, std::abs(sweep_angle));
    const auto cap_size = math::curve(std::abs(sweep_angle), 0.0f, 0.05f);


	if (state != progress_state::rough && prog >= 1.0f) {
		graphic::draw::emit(element.renderer(), fx::fringe::poly(draw::instruction::poly{
			.pos = center,
			.segments = count,
			.initial_angle = start_angle,
			.radius = {inner, outer},
			.color = {
				vertex_color_start, vertex_color_end
			}
		}, 1.5f));
		return;
	}


	graphic::draw::emit(element.renderer(), fx::fringe::poly_partial_with_cap(draw::instruction::poly_partial{
			.pos = center,
			.segments = count,
			.range = {start_angle, sweep_angle},
			.radius = {inner, outer},
			.color = {
				vertex_color_start, vertex_color_start,
				vertex_color_end, vertex_color_end
			}
		}, 1.5f * cap_size, 1.5f * cap_size, 1.5f));
    }
};
}
