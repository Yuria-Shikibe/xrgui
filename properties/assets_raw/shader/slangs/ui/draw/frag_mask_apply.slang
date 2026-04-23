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

[[format("r8")]]
[[vk::binding(0, 2)]] uniform Texture2D alpha_input_attachment;

struct FragOut {
    float4 out_base : SV_Target0;
}

public enum mask_mode : uint32_t {
    def,
    inv,
    igr,
}

struct Constants {
    gui::global_draw_mode_type draw_mode;
    float draw_defilade;

    mask_mode mode;
};

[[vk::push_constant]]
Constants constants;

[shader("fragment")]
FragOut main_frag(
    gui::io::VSOutput vtx,
    uint primitiveID: SV_PrimitiveID,
) {
    var primit = gui::get_current_primitive(primitiveID, dispatch_config_ubo, global_primitives);
    var final_color = gui::evaluate_primitive_base_color(primit, vtx, textures, constants.draw_mode);
    gui::apply_slide_line_effect(final_color, primit, vtx.position.xy, slide_line_style, ui_state.time);
    final_color.a *= constants.draw_defilade;
    
    FragOut out;
    out.out_base = final_color;
    
    switch (constants.mode) {
    case mask_mode::def: {
        float input_alpha_multiplier = alpha_input_attachment.Load(int3(int2(vtx.position.xy), 0)).r;
        out.out_base.a *= input_alpha_multiplier; break;}
    case mask_mode::inv: {
        float input_alpha_multiplier = alpha_input_attachment.Load(int3(int2(vtx.position.xy), 0)).r;
        out.out_base.a *= 1.f - input_alpha_multiplier; break;
    }
    default: break;
    }
    out.out_base.rgb *= out.out_base.a;
    out.out_base = float4(alpha_input_attachment.Load(int3(int2(vtx.position.xy), 0)).r);

    return out;
}