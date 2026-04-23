import mesh_instruction_lib_cs;
import general_lib;
import ui_structs;
import slide_line;
import sdf;

[[vk::binding(0, 0)]] StructuredBuffer<mesh_instr::Primitive> global_primitives;
[[vk::binding(2, 0)]] uniform Sampler2D textures[];

[[vk::binding(0, 1)]] ConstantBuffer<mesh_instr::dispatch_config> dispatch_config_ubo;
[[vk::binding(1, 1)]] ConstantBuffer<gui::ubo_ui_state> ui_state;
[[vk::binding(2, 1)]] ConstantBuffer<slide_line::slide_line_style> slide_line_style;

struct FragOut {
    float4 out_base : SV_Target0;
}

struct Constants {
    gui::global_draw_mode_type draw_mode;
    float draw_defilade;
};

[[vk::push_constant]]
Constants constant;

[shader("fragment")]
FragOut main_frag(
    gui::io::VSOutput vtx,
    uint primitiveID: SV_PrimitiveID,
) {
    var primit = gui::get_current_primitive(primitiveID, dispatch_config_ubo, global_primitives);
    var final_color = gui::evaluate_primitive_base_color(primit, vtx, textures, constant.draw_mode);
    gui::apply_slide_line_effect(final_color, primit, vtx.position.xy, slide_line_style, ui_state.time);

    final_color.a *= (1 - constant.draw_defilade);
    final_color.rgb *= final_color.a;
    FragOut out;
    out.out_base = final_color;
    return out;
}