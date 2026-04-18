import mesh_instruction_lib_cs;
import ui_structs;

// --- Gfx Set 0 Bindings ---
[[vk::binding(0, 0)]] StructuredBuffer<mesh_instr::Primitive> global_primitives;
[[vk::binding(1, 0)]] StructuredBuffer<uint[2]> timelines;
[[vk::binding(3, 0)]] StructuredBuffer<gui::ubo_screen_info, Std140DataLayout> screen_info;
[[vk::binding(4, 0)]] StructuredBuffer<gui::ubo_layer_info, Std140DataLayout> layer_info;

// --- Gfx Set 1 Bindings ---
[[vk::binding(0, 1)]] ConstantBuffer<mesh_instr::dispatch_config> dispatch_config_ubo;

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
    output.uv = input.position.xy;

    return output;
}

struct FragOut 
{
    float4 out_base : SV_Target0;
};

static const float GRID_SIZE = 200.0f;
static const float GRID_WIDTH = 2.5f;
static const float4 GRID_COLOR = float4(0.76f, 0.76f, 0.76f, 0.8f);

static const float DASH_LEN = 8.0f;
static const float DASH_GAP = 6.0f;

static const float AXIS_WIDTH = 5.0f;
static const float4 AXIS_X_COLOR = float4(1.0f, 0.2f, 0.2f, 1.0f);
static const float4 AXIS_Y_COLOR = float4(0.2f, 0.8f, 0.2f, 1.0f);

[shader("fragment")]
FragOut main_frag(
    gui::io::VSOutput vtx,
    uint primitiveID : SV_PrimitiveID
)
{
    // 对齐整体管线结构提取 primitive 数据（即使当前着色暂不需要也可保留抓取逻辑以防日后扩展）
    uint global_prm_idx = dispatch_config_ubo.global_primitive_offset + primitiveID;
    mesh_instr::Primitive primit = global_primitives[global_prm_idx];

    // 从 Vertex 阶段接管本地坐标
    float2 local_pos = vtx.uv;
    float2 df = max(fwidth(local_pos), 1e-5);

    float2 grid_dist = abs(frac(local_pos / GRID_SIZE + 0.5f) - 0.5f) * GRID_SIZE;
    float2 grid_pixel_dist = grid_dist / df;

    float dash_pattern_y = frac(local_pos.y / (DASH_LEN + DASH_GAP)) * (DASH_LEN + DASH_GAP);
    float dash_alpha_y   = (dash_pattern_y < DASH_LEN) ? 1.0f : 0.0f;

    float dash_pattern_x = frac(local_pos.x / (DASH_LEN + DASH_GAP)) * (DASH_LEN + DASH_GAP);
    float dash_alpha_x   = (dash_pattern_x < DASH_LEN) ? 1.0f : 0.0f;

    float grid_v = (1.0f - smoothstep(GRID_WIDTH * 0.5f - 0.5f, GRID_WIDTH * 0.5f + 0.5f, grid_pixel_dist.x)) * dash_alpha_y;
    float grid_h = (1.0f - smoothstep(GRID_WIDTH * 0.5f - 0.5f, GRID_WIDTH * 0.5f + 0.5f, grid_pixel_dist.y)) * dash_alpha_x;
    
    float grid_alpha = max(grid_v, grid_h);

    float2 axis_pixel_dist = abs(local_pos) / df;

    float axis_y_line = 1.0f - smoothstep(AXIS_WIDTH * 0.5f - 0.5f, AXIS_WIDTH * 0.5f + 0.5f, axis_pixel_dist.x);
    float axis_x_line = 1.0f - smoothstep(AXIS_WIDTH * 0.5f - 0.5f, AXIS_WIDTH * 0.5f + 0.5f, axis_pixel_dist.y);

    float4 final_color = float4(0.0f, 0.0f, 0.0f, 0.0f);

    final_color = lerp(final_color, GRID_COLOR, grid_alpha * GRID_COLOR.a);
    final_color = lerp(final_color, AXIS_X_COLOR, axis_x_line * AXIS_X_COLOR.a);
    final_color = lerp(final_color, AXIS_Y_COLOR, axis_y_line * AXIS_Y_COLOR.a);

    FragOut out;
    out.out_base = final_color;
    out.out_base.rgb *= out.out_base.a;
    
    return out;
}