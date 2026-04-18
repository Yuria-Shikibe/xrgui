import mesh_instruction_lib_cs;
import general_lib;
import ui_structs;
import slide_line;
import sdf;


// --- Gfx Set 0 Bindings ---
[[vk::binding(0, 0)]] StructuredBuffer<mesh_instr::Primitive> global_primitives;
[[vk::binding(2, 0)]] uniform Sampler2D textures[];


[[vk::binding(0, 1)]] ConstantBuffer<mesh_instr::dispatch_config> dispatch_config_ubo;

struct FragOut {
    float4 out_base : SV_Target0;
}


struct outline_params {
    float stroke;
}

[[vk::push_constant]]
outline_params outline_arg;

float4 DrawSDFStroke(float sdf, float strokeWidthPixels)
{
    const static float gradient_val = .6f;
    float sdfGradient = fwidth(sdf);
    sdfGradient = max(sdfGradient, 1e-5);

    float pixelDist = sdf / sdfGradient;

    float edgeBlend = smoothstep(-gradient_val, gradient_val, pixelDist);
    float3 fillColor = float3(1.0f);
    float3 strokeColor = float3(0.13f);
    float3 finalColor = lerp(fillColor, strokeColor, edgeBlend);

    float alphaBlend = smoothstep(strokeWidthPixels - gradient_val, strokeWidthPixels + gradient_val, pixelDist);
    float finalAlpha = 1.0 - alphaBlend;

    return float4(finalColor, finalAlpha);
}

[shader("fragment")]
FragOut main_frag(
    gui::io::VSOutput vtx,
    uint primitiveID : SV_PrimitiveID
) {
    // 获取全局图元索引
    uint global_prm_idx = dispatch_config_ubo.global_primitive_offset + primitiveID;
    mesh_instr::Primitive primit = global_primitives[global_prm_idx];

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