import post_process_lib;

[[vk::binding(0, 0)]]
restrict WTexture2D output;

[[vk::binding(1, 0)]]
[[format("rgba16f")]]
restrict readonly RWTexture2D input;

static const float exposure = 1.f;
static const float gamma = 2.2;

[ForceInline]
float3 hdr_to_sdr_advanced(float3 hdrColor, float exp, float cont, float gam, float saturation) {
    let exposed = hdrColor * exp;

    // ACES色调映射
    let aces = exposed * 0.6 + 0.01;
    var mapped = (aces * (2.51 * aces + 0.03)) / (aces * (2.43 * aces + 0.59) + 0.14);

    // 可选饱和度控制
    let luma = pp::luma(mapped);
    mapped = lerp(luma, mapped, saturation);

    // 对比度和Gamma
    let contrasted = (mapped - 0.18) * cont + 0.18;
    let gammaCorrected = pow(contrasted, float3(1.0 / gam));

    return clamp(gammaCorrected, 0.0, 1.0);
}

[shader("compute")]
[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(
    uint3 dispatchThreadID: SV_DispatchThreadID
) {
    let pos = dispatchThreadID.xy;
    let hdrColor = input.Load(pos);

    let mapped = pp::hdr_to_sdr(hdrColor.rgb, exposure, gamma);
    // output.Store(pos, float4(pp::hdr_to_sdr(hdrColor.rgb, exposure, gamma), 1));
    output.Store(pos, float4(lerp(hdrColor.rgb, mapped, .5f), hdrColor.a));
}
