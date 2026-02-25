import post_process_lib;

[[vk::push_constant]]
uint2 offset;

[[vk::binding(0, 0)]]
restrict readonly RWTexture2D input_base;

[[vk::binding(1, 0)]]
restrict readonly RWTexture2D input_back;

[[vk::binding(2, 0)]]
restrict RWTexture2D output_base;

[[vk::binding(3, 0)]]
restrict RWTexture2D output_back;

float4 applyShadow(float4 shadowColor, float4 baseColor) {
    let shadowIntensity = shadowColor.a;
    let darkenedRGB = lerp(shadowColor.rgb, shadowColor.rgb, shadowIntensity);
    let darkenedColor = float4(darkenedRGB * baseColor.rgb, baseColor.a);

    return darkenedColor;
}

[shader("compute")]
[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(
    uint3 dispatchThreadID: SV_DispatchThreadID
) {
    let pos = dispatchThreadID.xy + offset;

    let color_base_src = input_base.Load(pos);
    let color_back_src = input_back.Load(pos);
    var trans_back = color_back_src;

    trans_back.a *= .75f;

    let color_base_dst = output_base.Load(pos);
    let color_back_dst = output_back.Load(pos);

    let shadow1 = float4(trans_back.rgb, trans_back.a * .85f);
    let blended = pp::blend::alpha_blend(color_base_src, trans_back);

    // 【新增逻辑】：定义变暗强度 (0.0 为完全不变暗，1.0 为 Src[1] 完全不透明时 Dst[0] 变为纯黑)
    // 实际项目中可以通过 Constant Buffer 从外部传入此变量
    const float DARKEN_INTENSITY = 0.5;

    // 根据 Src[1] 的 Alpha 提取变暗因子
    float darken_factor = 1.0 - (trans_back.a * DARKEN_INTENSITY);

    // 复制 Dst[0] 并仅对其 RGB 通道进行压暗，保留其 Alpha (形状/不透明度) 不变
    float4 darkened_base_dst = color_base_dst;
    darkened_base_dst.rgb *= darken_factor;

    trans_back *= darkened_base_dst.a;
    // 步骤一：Src[1] (背景色) 作用给压暗后的 Dst[0] (目标前景色)
    float4 temp_base_dst = pp::blend::pre_scaled_alpha_blend(trans_back, darkened_base_dst);

    // 步骤二：Src[1] (背景色) 和 Dst[1] (目标背景色) 预乘混合
    float4 final_back_dst = pp::blend::pre_scaled_alpha_blend(color_back_src, color_back_dst);

    // 步骤三：Src[0] (前景色) 和 经过步骤一更新后的 Dst[0] 预乘混合
    float4 final_base_dst = pp::blend::pre_scaled_alpha_blend(color_base_src, temp_base_dst);

    output_base.Store(pos, final_base_dst);
    output_back.Store(pos, final_back_dst);
}