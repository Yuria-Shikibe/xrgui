module ui_structs;

namespace gui {

public struct ubo_screen_info {
    public float3x3 screen_to_uniform;
};

public struct scissor {
    public float2 src;
    public float2 dst;
    public float margin;
    uint32_t _c1, _c2, _c3;
}

public struct ubo_layer_info {
    public float3x3 element_to_screen;
    public scissor scissor;
};

public struct ubo_ui_state {
    public float2 screen_extent;
    public float time;
    uint32_t _cap;
};

namespace io {

public struct VSInput {
    public float3 position;
    public uint timeline_index;
    public float4 color;
    public float2 uv;
};

// 定义传递给 FS 的数据
public struct VSOutput {
    public float4 position : SV_Position;
    public float4 color;
    public float2 uv;

    // [[mutating]]
    // public void resolve_outpus_pos(
    //     uint index,
    //     StructuredBuffer<uint32_t[2]> timelines: __ref,
    //     StructuredBuffer<ubo_screen_info, Std140DataLayout> screen_info: __ref,
    //     StructuredBuffer<ubo_layer_info, Std140DataLayout> layer_info: __ref,
    // ) {
    //     // 1. 获取当前顶点的 Timeline 索引数据
    //     let timeline = timelines[index];
    //     let screen_idx = timeline[0] - 1; get_timeline_index(timeline[0]);
    //     let layer_idx = timeline[1] - 1; get_timeline_index(timeline[1]);

    //     // 2. 提取当前元素所对应的变换矩阵
    //     let layer = layer_info[layer_idx];
    //     let screen = screen_info[screen_idx];

    //     // 3. 执行坐标变换
    //     let screen_coord = mul(layer.element_to_screen, float3(position.xy, 1.0));
    //     let uniformed_coord = mul(screen.screen_to_uniform, screen_coord);

    //     position.xy = uniformed_coord.xy;
    // }
};

}

public enum draw_mode_type : uint32_t {
    none = 0u,
    slide_line = 1u << 0u,
    target_background = 1u << 1u
};

public enum global_draw_mode_type {
    def,
    sdf
}

public struct pipeline_flags {
    public uint mode;
}

[[ForceInline]]
uint get_timeline_index(uint idx) {
    return idx - 1;
}

// public float2 resolve_uniform_position(
//     uint timeline_index,
//     float2 local_pos,
//     StructuredBuffer<uint32_t[2]> timelines : __ref,
//     StructuredBuffer<ubo_screen_info, Std140DataLayout> screen_info : __ref,
//     StructuredBuffer<ubo_layer_info, Std140DataLayout> layer_info : __ref,
// )
// {
//     // 1. 获取当前顶点的 Timeline 索引数据
//     let timeline = timelines[timeline_index];
//     let screen_idx = 0;timeline [0] - 1; get_timeline_index(timeline[0]);
//     let layer_idx = 0;timeline[1] - 1; get_timeline_index(timeline[1]);

//     // 2. 提取当前元素所对应的变换矩阵
//     let layer = layer_info[layer_idx];
//     let screen = screen_info[screen_idx];

//     // 3. 执行坐标变换
//     let screen_coord = mul(layer.element_to_screen, float3(local_pos, 1.0));
//     let uniformed_coord = mul(screen.screen_to_uniform, screen_coord);

//     return uniformed_coord.xy;
// }

}
