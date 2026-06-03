import mesh_instruction_lib_cs;
import ui_structs;

// --- Gfx Set 0 Bindings ---
[[vk::binding(0, 0)]] StructuredBuffer<mesh_instr::Primitive> global_primitives;

[[vk::binding(1, 0)]] StructuredBuffer<gui::timeline<3>> timelines;
[[vk::binding(4, 0)]] StructuredBuffer<gui::ubo_screen_info, Std140DataLayout> screen_info;
[[vk::binding(5, 0)]] StructuredBuffer<gui::ubo_layer_info, Std140DataLayout> layer_info;
[[vk::binding(6, 0)]] StructuredBuffer<gui::color_accumulated_state, Std140DataLayout> accumulated_state;

// --- Gfx Set 1 Bindings ---
[[vk::binding(0, 1)]] ConstantBuffer<mesh_instr::dispatch_config> dispatch_config_ubo;

[shader("vertex")]
gui::io::VSOutput main_vert(gui::io::VSInput input) {
    gui::io::VSOutput output;

    let timeline = timelines[input.timeline_index];

    let screen = screen_info[timeline[0]];
    let layer = layer_info[timeline[1]];
    let color_a = accumulated_state[timeline[2]];

    let screen_coord = mul(layer.element_to_screen, float3(input.position.xy, 1.0));
    let uniformed_coord = mul(screen.screen_to_uniform, screen_coord);

    output.position = float4(uniformed_coord.xy, input.position.z, 1.0);

    output.color = color_a.apply(input.color);
    output.uv = input.position.xy;

    return output;
}

struct FragOut 
{
    float4 out_base : SV_Target0;
};

static const float EDITOR_GRID_SIZE = 100.0f;
static const float GRID_WIDTH = 1.0f;
static const float AXIS_WIDTH = 2.25f;
static const float AA_WIDTH = 0.75f;
static const float4 GRID_COLOR = float4(0.36f, 0.38f, 0.40f, 0.34f);
static const float4 AXIS_X_COLOR = float4(0.95f, 0.18f, 0.20f, 0.78f);
static const float4 AXIS_Y_COLOR = float4(0.28f, 0.82f, 0.30f, 0.78f);

float line_coverage(float pixel_distance, float width_pixels)
{
    float half_width = width_pixels * 0.5f;
    return 1.0f - smoothstep(half_width - AA_WIDTH, half_width + AA_WIDTH, pixel_distance);
}

float periodic_line_distance_pixels(float coord, float step, float world_per_pixel)
{
    float wrapped = abs(frac(coord / step + 0.5f) - 0.5f) * step;
    return wrapped / max(abs(world_per_pixel), 1e-5f);
}

float4 premultiply_with_tint(float4 color, float coverage, float4 tint)
{
    float alpha = saturate(color.a * coverage * tint.a);
    return float4(color.rgb * tint.rgb * alpha, alpha);
}

float4 over(float4 dst, float4 src)
{
    return src + dst * (1.0f - src.a);
}

[shader("fragment")]
FragOut main_frag(
    gui::io::VSOutput vtx,
    uint primitiveID : SV_PrimitiveID
)
{
    uint global_prm_idx = dispatch_config_ubo.global_primitive_offset + primitiveID;
    mesh_instr::Primitive primit = global_primitives[global_prm_idx];

    float2 world_pos = vtx.uv;
    float2 world_per_pixel = max(abs(fwidth(world_pos)), float2(1e-5f, 1e-5f));

    float grid_v = line_coverage(
        periodic_line_distance_pixels(world_pos.x, EDITOR_GRID_SIZE, world_per_pixel.x),
        GRID_WIDTH);
    float grid_h = line_coverage(
        periodic_line_distance_pixels(world_pos.y, EDITOR_GRID_SIZE, world_per_pixel.y),
        GRID_WIDTH);
    float grid_alpha = max(grid_v, grid_h);

    float axis_x_alpha = line_coverage(abs(world_pos.y) / world_per_pixel.y, AXIS_WIDTH);
    float axis_y_alpha = line_coverage(abs(world_pos.x) / world_per_pixel.x, AXIS_WIDTH);

    float4 final_color = premultiply_with_tint(GRID_COLOR, grid_alpha, vtx.color);
    final_color = over(final_color, premultiply_with_tint(AXIS_X_COLOR, axis_x_alpha, vtx.color));
    final_color = over(final_color, premultiply_with_tint(AXIS_Y_COLOR, axis_y_alpha, vtx.color));

    FragOut out;
    out.out_base = final_color;
    return out;
}
