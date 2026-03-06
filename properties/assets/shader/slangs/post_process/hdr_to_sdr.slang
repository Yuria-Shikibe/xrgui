import post_process_lib;

[[vk::binding(0, 0)]]
restrict WTexture2D output;

[[vk::binding(1, 0)]]
[[format("rgba16f")]]
Texture2D input;

// 将外部控制参数移入 ConstantBuffer
struct TonemapParams {
    float exposure;
    float contrast;
    float gamma;
    float saturation;
};

[[vk::binding(2, 0)]] 
ConstantBuffer<TonemapParams> params;

// 业界标准的 Narkowicz ACES 拟合函数
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
    // 1. 曝光控制
    float3 color = hdrColor * exp;
    
    // 2. 核心色调映射 (ACES)
    color = ACESFilm(color);
    
    // 3. 饱和度控制 (使用 Rec.709 亮度权重)
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    color = lerp(float3(luma), color, sat);
    
    // 4. 对比度调整 (围绕中性灰 0.18)
    color = (color - 0.18f) * cont + 0.18f;
    
    // 5. Gamma 校正
    color = max(color, 0.0f); // 确保进行 pow 计算前没有负数
    color = pow(color, float3(1.0f / gam));
    
    return saturate(color);
}

[shader("compute")]
[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(
    uint3 dispatchThreadID: SV_DispatchThreadID
) {
    let pos = dispatchThreadID.xy;
    let hdrColor = input.Load(uint3(pos, 0));

    // 从 ConstantBuffer 读取参数
    let exp = params.exposure;
    let cont = params.contrast;
    let gam = params.gamma;
    let sat = params.saturation;

    // 应用高级色调映射管线
    let mapped = hdr_to_sdr_advanced(hdrColor.rgb, exp, cont, gam, sat);

    // 输出最终映射的 SDR 颜色
    output.Store(pos, float4(mapped, hdrColor.a));
}