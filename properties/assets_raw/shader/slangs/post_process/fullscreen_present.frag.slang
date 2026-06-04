[[vk::binding(0, 0)]]
[[format("rgba16f")]]
Texture2D input_hdr;

struct TonemapParams {
    float exposure;
    float contrast;
    float gamma;
    float saturation;
};

[[vk::binding(1, 0)]]
ConstantBuffer<TonemapParams> params;

struct VSOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

struct FragOut {
    float4 color : SV_Target0;
};

[ForceInline]
float3 ACESFilm(float3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

[ForceInline]
float3 hdr_to_sdr_advanced(float3 hdrColor, float exp, float cont, float gam, float sat) {
    float3 color = hdrColor * exp;
    color = ACESFilm(color);

    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    color = lerp(float3(luma), color, sat);
    color = (color - 0.18f) * cont + 0.18f;
    color = max(color, 0.0f);
    color = pow(color, float3(1.0f / gam));
    return saturate(color);
}

float InterleavedGradientNoise(float2 screenPos) {
    constexpr let magic = float3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(screenPos, magic.xy)));
}

[shader("fragment")]
FragOut main_frag(VSOut input) {
    let pos = int2(input.position.xy);
    let hdrColorRaw = input_hdr.Load(int3(pos, 0));
    let hdrColor = float4(max(hdrColorRaw.rgb, 0.0f), hdrColorRaw.a);

    let mapped = hdr_to_sdr_advanced(
        hdrColor.rgb,
        params.exposure,
        params.contrast,
        params.gamma,
        params.saturation);

    float dither = (InterleavedGradientNoise(float2(pos)) - 0.5f) / 255.0f;

    FragOut output;
    output.color = float4(mapped + dither, hdrColor.a);
    return output;
}
