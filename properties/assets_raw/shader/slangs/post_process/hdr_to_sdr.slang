import post_process_lib;

[[vk::binding(0, 0)]]
restrict WTexture2D output;

[[vk::binding(1, 0)]]
[[format("rgba16f")]]
Texture2D input;


struct TonemapParams {
    float exposure;
    float contrast;
    float gamma;
    float saturation;
};

[[vk::binding(2, 0)]] 
ConstantBuffer<TonemapParams> params;


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

[shader("compute")]
[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(
    uint3 dispatchThreadID: SV_DispatchThreadID
) {
    let pos = dispatchThreadID.xy;
    let hdrColorRaw = input.Load(uint3(pos, 0));
    let hdrColor = float4(max(hdrColorRaw.rgb, 0.0f), hdrColorRaw.a);


    let exp = params.exposure;
    let cont = params.contrast;
    let gam = params.gamma;
    let sat = params.saturation;


    let mapped = hdr_to_sdr_advanced(hdrColor.rgb, exp, cont, gam, sat);
    float dither = (InterleavedGradientNoise(float2(pos)) - 0.5) / 255.0;
    var result = float4(mapped, hdrColor.a);
    result += dither;

    output.Store(pos, result);
}