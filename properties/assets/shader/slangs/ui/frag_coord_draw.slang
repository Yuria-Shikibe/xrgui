import mesh_instruction_lib;
import ui_structs;

// ==========================================
// 顶点与 Mesh Shader 阶段
// ==========================================
using namespace mesh_instr;

struct params {
    mesh_instruction_data<2> data;
    uniform Sampler2D _cap;
    StructuredBuffer<gui::ubo_screen_info, Std140DataLayout> screen_info;
    StructuredBuffer<gui::ubo_layer_info, Std140DataLayout> layer_info;
}

[[vk::binding(0, 0)]]
ParameterBlock<params> input_params;

[[vk::binding(0, 1)]]
ConstantBuffer<dispatch_config> dispatch_config_ubo;

struct trans : vertex_transformer<Vertex, 2> {
    uint[2] timeline;

    [[ForceInline]]
    uint index_at(uint slot) {
        return timeline[slot] - 1;
    }

    [[mutating]]
    void set_timeline(uint[2] timeline) {
        this.timeline = timeline;
    }

    [[ForceInline]]
    Vertex operator()(Vertex v) {
        // 【核心操作】：将图元变换前的原始局部坐标 (x, y) 存入 uv 中。
        // 这使得我们在像素着色阶段能够根据实际尺寸和原始位置计算网格与坐标轴。
        v.uv = v.position.xy;

        let layer = input_params.layer_info[index_at(1)];
        let screen_coord = mul(layer.element_to_screen, float3(v.position.xy, 1.f));
        let uniformed_coord = mul(input_params.screen_info[index_at(0)].screen_to_uniform, screen_coord);

        v.position.xy = uniformed_coord.xy;

        return v;
    }
}

[shader("mesh")]
[numthreads(GROUP_COUNT, 1, 1)]
[outputtopology("triangle")]
void main_mesh(
    uint groupId: SV_GroupID,
    uint threadId: SV_GroupThreadID,
    vertices out Vertex verts[VERTEX_PER_MESH],
    primitives out Primitive prims[PRIMITIVES_PER_MESH],
    indices out uint3 indices[PRIMITIVES_PER_MESH]
) {
    let gid = groupId + dispatch_config_ubo.group_offset;
    let pmt = resolve_instruction_v3(
        gid, threadId, input_params.data,
        trans(), 
        verts, prims, indices
    );
    if (threadId == 0) SetMeshOutputCounts(VERTEX_PER_MESH, pmt);
}

// ==========================================
// 片段 Shader 阶段 (网格与坐标轴绘制)
// ==========================================

struct FragOut {
    float4 out_base : SV_Target0;
}

// --- 硬编码常量配置 ---
// 网格参数
static const float GRID_SIZE = 200.0f;                            // 网格尺寸大小
static const float GRID_WIDTH = 2.5f;                            // 网格线宽(像素)
static const float4 GRID_COLOR = float4(0.76f, 0.76f, 0.76f, 0.8f); // 网格颜色(灰色，带透明度)

// 虚线参数
static const float DASH_LEN = 8.0f; // 虚线的实线部分长度
static const float DASH_GAP = 6.0f; // 虚线的空白部分长度

// 坐标轴参数
static const float AXIS_WIDTH = 5.0f;                              // 坐标轴线宽(像素)
static const float4 AXIS_X_COLOR = float4(1.0f, 0.2f, 0.2f, 1.0f); // X轴颜色: 醒目的红色
static const float4 AXIS_Y_COLOR = float4(0.2f, 0.8f, 0.2f, 1.0f); // Y轴颜色: 醒目的绿色

[shader("fragment")]
FragOut main_frag(
    in mesh_instr::Vertex vtx,
    in mesh_instr::Primitive primit
) {
    // 提取在 Vertex 阶段保存的原始局部坐标
    float2 local_pos = vtx.uv;

    // 获取当前局部坐标在屏幕空间对应的一个像素的尺寸变化率（用于完美的 1 像素抗锯齿）
    // max 处理是为了防止后续触发除零错误 (NaN)
    float2 df = max(fwidth(local_pos), 1e-5);


    // ----------------------
    // 1. 虚线网格绘制
    // ----------------------
    // 计算离最近网格线的距离
    float2 grid_dist = abs(frac(local_pos / GRID_SIZE + 0.5f) - 0.5f) * GRID_SIZE;
    // 将距离转换为屏幕像素单位
    float2 grid_pixel_dist = grid_dist / df;

    // 计算虚线模式：沿 Y 轴方向产生的虚线 (作用于垂直网格线)
    float dash_pattern_y = frac(local_pos.y / (DASH_LEN + DASH_GAP)) * (DASH_LEN + DASH_GAP);
    float dash_alpha_y   = (dash_pattern_y < DASH_LEN) ? 1.0f : 0.0f;

    // 计算虚线模式：沿 X 轴方向产生的虚线 (作用于水平网格线)
    float dash_pattern_x = frac(local_pos.x / (DASH_LEN + DASH_GAP)) * (DASH_LEN + DASH_GAP);
    float dash_alpha_x   = (dash_pattern_x < DASH_LEN) ? 1.0f : 0.0f;

    // 分别计算垂直方向和水平方向的抗锯齿透明度，并叠加虚线 Mask
    float grid_v = (1.0f - smoothstep(GRID_WIDTH * 0.5f - 0.5f, GRID_WIDTH * 0.5f + 0.5f, grid_pixel_dist.x)) * dash_alpha_y;
    float grid_h = (1.0f - smoothstep(GRID_WIDTH * 0.5f - 0.5f, GRID_WIDTH * 0.5f + 0.5f, grid_pixel_dist.y)) * dash_alpha_x;
    
    // 合并水平和垂直网格
    float grid_alpha = max(grid_v, grid_h);

    // ----------------------
    // 2. 主坐标轴绘制
    // ----------------------
    // 将距离原点的绝对坐标转为像素距离
    float2 axis_pixel_dist = abs(local_pos) / df;

    // Y 轴线段位于 x = 0 的位置
    float axis_y_line = 1.0f - smoothstep(AXIS_WIDTH * 0.5f - 0.5f, AXIS_WIDTH * 0.5f + 0.5f, axis_pixel_dist.x);
    // X 轴线段位于 y = 0 的位置
    float axis_x_line = 1.0f - smoothstep(AXIS_WIDTH * 0.5f - 0.5f, AXIS_WIDTH * 0.5f + 0.5f, axis_pixel_dist.y);

    // ----------------------
    // 3. 颜色图层混合
    // ----------------------
    // 初始底色：透明
    float4 final_color = float4(0.0f, 0.0f, 0.0f, 0.0f);

    // 依序进行 Alpha 混合 (Painter's Algorithm 算法: 网格在底层，坐标轴在上层)
    final_color = lerp(final_color, GRID_COLOR, grid_alpha * GRID_COLOR.a);
    final_color = lerp(final_color, AXIS_X_COLOR, axis_x_line * AXIS_X_COLOR.a);
    final_color = lerp(final_color, AXIS_Y_COLOR, axis_y_line * AXIS_Y_COLOR.a);

    FragOut out;
    out.out_base = final_color;
    
    // UI 渲染常用的预乘 Alpha (Premultiplied Alpha) 处理
    out.out_base.rgb *= out.out_base.a;
    
    return out;
}