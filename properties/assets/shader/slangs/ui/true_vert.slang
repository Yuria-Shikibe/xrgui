import mesh_instruction_lib_cs;
import ui_structs;

// --- Gfx Set 0 Bindings ---
[[vk::binding(1, 0)]] StructuredBuffer<uint2> timelines;
[[vk::binding(3, 0)]] StructuredBuffer<gui::ubo_screen_info, Std140DataLayout> screen_info;
[[vk::binding(4, 0)]] StructuredBuffer<gui::ubo_layer_info, Std140DataLayout> layer_info;

// 定义标准的 VS 输入 (需要与 vkCmdBindVertexBuffers 匹配)
struct VSInput {
    [[vk::location(0)]] float4 position : POSITION;
    [[vk::location(1)]] float4 color : COLOR;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
    [[vk::location(3)]] uint timeline_index : BLENDINDICES;
};

// 定义传递给 FS 的数据
struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOutput main_vert(VSInput input) {
    VSOutput output;
    
    // 1. 获取当前顶点的 Timeline 索引数据
    uint2 timeline = timelines[input.timeline_index];
    uint screen_idx = timeline.x - 1; 
    uint layer_idx = timeline.y - 1;  

    // 2. 提取当前元素所对应的变换矩阵
    let layer = layer_info[layer_idx];
    let screen = screen_info[screen_idx];

    // 3. 执行坐标变换
    let screen_coord = mul(layer.element_to_screen, float3(input.position.xy, 1.0));
    let uniformed_coord = mul(screen.screen_to_uniform, screen_coord);

    // Compute Shader 中将 generic.depth 存入了 z 通道，保留它以用于深度测试
    output.position = float4(uniformed_coord.xy, input.position.z, 1.0);
    output.color = input.color;
    output.uv = input.uv;

    return output;
}