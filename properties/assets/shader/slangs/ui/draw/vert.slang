import mesh_instruction_lib_cs;
import ui_structs;

// --- Gfx Set 0 Bindings ---
[[vk::binding(1, 0)]] StructuredBuffer<uint[2]> timelines;
[[vk::binding(3, 0)]] StructuredBuffer<gui::ubo_screen_info, Std140DataLayout> screen_info;
[[vk::binding(4, 0)]] StructuredBuffer<gui::ubo_layer_info, Std140DataLayout> layer_info;

[shader("vertex")]
gui::io::VSOutput main_vert(gui::io::VSInput input) {
    gui::io::VSOutput output;

    let timeline = timelines[input.timeline_index];
    uint screen_idx = timeline[0] - 1; 
    uint layer_idx = timeline[1] - 1;  

    let layer = layer_info[layer_idx];
    let screen = screen_info[screen_idx];

    let screen_coord = mul(layer.element_to_screen, float3(input.position.xy, 1.0));
    let uniformed_coord = mul(screen.screen_to_uniform, screen_coord);

    output.position = float4(uniformed_coord.xy, input.position.z, 1.0);
    output.color = input.color;
    output.uv = input.uv;

    return output;
}