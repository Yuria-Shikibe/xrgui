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
[[vk::binding(0, 2)]] uniform restrict Texture2D last_layer_mask;

struct FragOut {
    float4 out : SV_Target0;
}

enum mask_write_type : uint32_t {
    mul_last,
    ignore_last,
};

struct Constants {
    gui::global_draw_mode_type mode;
    mask_write_type write_type;
}

[[vk::push_constant]]
Constants constants;

[shader("fragment")]
FragOut main_frag(
    gui::io::VSOutput vtx,
    uint primitiveID: SV_PrimitiveID,
) {
    var primit = gui::get_current_primitive(primitiveID, dispatch_config_ubo, global_primitives);
    var final_color = gui::evaluate_primitive_base_color(primit, vtx, textures, constants.mode);
    gui::apply_slide_line_effect(final_color, primit, vtx.position.xy, slide_line_style, ui_state.time);


    switch (constants.write_type) {
    case mask_write_type::mul_last:
        let v = last_layer_mask.Load(int3(int2(vtx.position.xy), 0)).r;
        final_color.a *= v;
        break;
    default: break;
    }

    FragOut out;
    out.out = final_color;
    out.out.rgb *= final_color.a;

    return out;
}