
import mesh_instruction_lib;
import general_lib;
import slide_line;
import sdf;

[[vk::binding(4, 0)]]
uniform Sampler2D textures[];

struct ubo_ui_state {
    float2 screen_extent;
    float time;
    uint32_t _cap;
};

[[vk::binding(1, 1)]]
ConstantBuffer<ubo_ui_state> ui_state;

// [[vk::binding(2, 1)]]
// ConstantBuffer<slide_line::slide_line_style> slide_line_style;

struct FragOut {
    float4 out_base : SV_Target0;
}

enum draw_mode_type : uint32_t {
    none = 0u,

    slide_line = 1u << 0u,
    target_background = 1u << 1u
};

enum global_draw_mode_type {
    def,
    sdf
}

struct outline_params {
    float stroke;
}

[[vk::push_constant]]
outline_params outline_arg;

// Slang / HLSL
float4 DrawSDFStroke(float sdf, float strokeWidthPixels)
{
    const static float gradient_val = .6f;
    // 1. 获取 SDF 在屏幕空间的像素级变化率 (Gradient)
    // 这是消除锯齿和统一像素单位的关键
    float sdfGradient = fwidth(sdf);

    // 防止除以 0 导致 NaN
    sdfGradient = max(sdfGradient, 1e-5);

    // 2. 将 SDF 转换到以“屏幕像素”为单位的距离
    // pixelDist < 0 为图形内部， pixelDist = 0 为边界， pixelDist > 0 为图形外部
    float pixelDist = sdf / sdfGradient;

    // 3. 处理内部（白色）到描边（黑色）的过渡
    // smoothstep(-0.5, 0.5) 表示在边界处用 1 个像素的宽度进行颜色混合，实现抗锯齿
    float edgeBlend = smoothstep(-gradient_val, gradient_val, pixelDist);
    float3 fillColor = float3(1.0f);   // 内部白色
    float3 strokeColor = float3(0.13f); // 描边黑色
    float3 finalColor = lerp(fillColor, strokeColor, edgeBlend);

    // 4. 处理描边外部边缘到透明的过渡
    // 从 strokeWidthPixels - 0.5 到 strokeWidthPixels + 0.5 平滑过渡
    float alphaBlend = smoothstep(strokeWidthPixels - gradient_val, strokeWidthPixels + gradient_val, pixelDist);
    // 内部和描边部分 alpha 为 1，超出描边宽度后 alpha 降至 0
    float finalAlpha = 1.0 - alphaBlend;

    return float4(finalColor, finalAlpha);
}

[shader("fragment")]
FragOut main_frag(
    in Vertex vtx,
    in Primitive primit
) {

    let no_tex = bool(primit.texture_index == ~0U);
    float dst;

    if (no_tex) {
        dst = vtx.color.a * 2 - 1.f;
    } else {
        let rst = sdf::msdf(textures[primit.texture_index], vtx.uv, float2(4096.f, 4096.f));
        let rng = rst.scale * primit.sdf_expand;
        dst = rst.factor - rng;
    }

    FragOut out;
    out.out_base = DrawSDFStroke(-dst, outline_arg.stroke);
    out.out_base.rgb *= out.out_base.a;
    
    return out;
}