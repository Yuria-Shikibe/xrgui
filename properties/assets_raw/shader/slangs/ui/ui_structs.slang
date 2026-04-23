module ui_structs;

import mesh_instructions;
import mesh_instruction_lib_cs;
import slide_line;
import sdf;

namespace gui {

public static const float2 msdf_image_extent = { 4096.f, 4096.f };

public struct timeline<uint N> : IArray<uint> {
    uint[N] timeline;

    int getCount() {
        return N;
    }

    public __subscript(int index) -> uint
    {
        get {
            return timeline[index] - 1;
        }
    }
}

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

public struct color_accumulated_state {
    float4 overlay_color;
    float4 base_mult;

    public float4 apply(float4 original_color, float original_intensity) {
        // 由于 C++ 端已经把强度倍率烘焙进了 RGB，这里直接按常规乘加处理即可
        // 记得将顶点的 original_intensity 分配给它对应的输入项
        float3 orig_rgb_with_intensity = original_color.rgb * original_intensity;

        float4 blended_color;
        // Alpha 通道保持原样，不受发光强度影响
        blended_color.a = fma(original_color.a, base_mult.a, overlay_color.a);
        // RGB 通道应用折叠后的数学逻辑 (注意 overlay_color.rgb 依然要乘以原始强度)
        blended_color.rgb = fma(orig_rgb_with_intensity, base_mult.rgb, overlay_color.rgb * original_intensity);

        return blended_color;
    }

    public float4 apply(float4 original_color) {
        return apply(original_color, 1.0);
    }
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
    public global_draw_mode_type mode;
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

public mesh_instr::Primitive get_current_primitive(
    uint primitiveID,
    ConstantBuffer<mesh_instr::dispatch_config> config,
    StructuredBuffer<mesh_instr::Primitive> primitives
) {
    // return  {};
    uint global_prm_idx = config.global_primitive_offset + primitiveID;
    return primitives[global_prm_idx];
}

public float4 evaluate_primitive_base_color(
    mesh_instr::Primitive primit,
    io::VSOutput vtx,
    uniform Sampler2D textures[],
    global_draw_mode_type draw_mode_type 
) {
    let no_tex = bool(primit.texture_index == ~0U);
    float4 out_color;

    switch (draw_mode_type) {
    case global_draw_mode_type::def:
        out_color = (no_tex ? float4(1.f) : textures[primit.texture_index].Sample(vtx.uv)) * vtx.color;
        break;
    case global_draw_mode_type::sdf:
        if (no_tex) {
            out_color = float4(1.f) * vtx.color;
        } else {
            let rst = sdf::msdf(textures[primit.texture_index], vtx.uv, msdf_image_extent);
            let rng = rst.scale * primit.sdf_expand;
            out_color = float4(1.f, 1.f, 1.f, smoothstep(-0.6f, 0.6f, rst.factor - rng)) * vtx.color;
        }
        break;
    default:
        out_color = vtx.color;
        break;
    }
    return out_color;
}

public void apply_slide_line_effect(
    inout float4 color,
    mesh_instr::Primitive primit,
    float2 position_xy,
    ConstantBuffer<slide_line::slide_line_style> style,
    float time
) {
    if (bool(primit.mode.value & gui::draw_mode_type::slide_line)) {
        float scl = slide_line::is_in_slide_line_smooth(position_xy, style, time);
        color.a *= scl;
    }
}

}
