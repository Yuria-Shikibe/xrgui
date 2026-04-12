
import mesh_instruction_lib;
import general_lib;
import slide_line;
import sdf;

[[vk::binding(mesh_instr::TEXTURE_BINDING_INDEX, 0)]]
uniform Sampler2D textures[];

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
    in mesh_instr::Vertex vtx,
    in mesh_instr::Primitive primit
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