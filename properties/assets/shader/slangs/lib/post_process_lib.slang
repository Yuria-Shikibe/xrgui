module post_process_lib;

public static const uint GROUP_SIZE = 16;

namespace pp
{
namespace colors {
public static float4 black = { 0, 0, 0, 1 };
}


[ForceInline]
public float luma(float3 rgb)
{
    return dot(rgb, float3(0.299, 0.587, 0.114));
}

namespace blend
{

[ForceInline]
public float3 screen(float3 src, float3 dst)
{
    return float3(1.0) - (1.0 - src.rgb) * (1.0 - dst.rgb);
}

[ForceInline]
public float4 alpha_blend(const in float4 src, const in float4 dst)
{
    let rgb = lerp(dst.rgb, src.rgb, src.a);
    let a = fma(dst.a, 1 - src.a, src.a);
    return float4(rgb, a);
}

[ForceInline]
public float4 pre_scaled_alpha_blend(const in float4 src, const in float4 dest)
{
    float4 result;
    result.rgb = fma(dest.rgb, (1.0 - src.a), src.rgb);
    result.a = fma(dest.a, (1.0 - src.a), src.a);
    return result;
}

}

[ForceInline]
public float3 hdr_to_sdr(float3 color_in_hdr, const float exposure, const float gamma) {
    let mapped = float3(1.0) - exp(-color_in_hdr * exposure);

    // 或者使用其他色调映射算子：
    // vec3 mapped = hdrColor / (hdrColor + vec3(1.0)); // Reinhard简单版本
    // vec3 mapped = hdrColor * exposure / (hdrColor * exposure + vec3(1.0)); // 带曝光的Reinhard

    return pow(mapped, float3(1.0 / gamma));
}

[ForceInline]
public float2 uniform_to_normalize(float2 uniformed) {
    return (uniformed + 1.f) * .5f;
}

[ForceInline]
public float2 coord_to_uniform(float2 coord, float2 extent) {
    return ((coord / extent) - float2(.5f)) * 2.f;
}

[ForceInline]
public float2 coord_to_uniform(uint2 coord, uint2 extent) {
    return coord_to_uniform(float2(coord) + float2(.5f, .5f), float2(extent));
}

}
