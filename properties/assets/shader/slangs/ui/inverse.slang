import post_process_lib;

[[vk::push_constant]]
uint2 offset;

[[vk::binding(0, 0)]]
restrict readonly RWTexture2D input;

[[vk::binding(1, 0)]]
restrict RWTexture2D output;

float4 ApplyMaskEffect(float4 dst, float mask)
{
    // 1. 获取 dst 的透明度权重。
    // 使用 saturate 将 alpha 限制在 0-1 之间，防止 HDR alpha 导致 lerp 越界而产生断裂
    float alphaWeight = saturate(dst.a);

    // 2. 定义完全透明时的目标状态 (纯白)
    float4 emptyTarget = float4(1.0, 1.0, 1.0, 1.0);

    // 3. 定义有内容时的目标状态 (RGB 反相，Alpha 保持原样)
    // 注意：因为 dst.rgb 可能大于 1，这里的反相可能会产生负值。
    // 保持它的数学连续性可以避免在数值超过 1 的边界处发生色彩断裂。
    float4 invertedTarget = float4(1.0 - dst.rgb, dst.a);

    // 4. 根据 dst 的 alpha 平滑混合这两种目标效果
    float4 effectColor = lerp(emptyTarget, invertedTarget, alphaWeight);

    // 5. 根据 mask 将原图 dst 与我们计算出的 effectColor 进行混合
    // 当 mask = 0 时，lerp 直接返回 dst，确保 dst 不发生任何变化。
    // 当 dst 为完全透明 (0,0,0,0) 且 mask > 0 时，lerp 会输出 (mask, mask, mask, mask)，即 (1,1,1,1) * mask。
    return lerp(dst, effectColor, mask);
}

[shader("compute")]
[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(
    uint3 dispatchThreadID: SV_DispatchThreadID
) {
    let pos = dispatchThreadID.xy + offset;

    var sourceColor = input.Load(pos);
    let targetColor = output.Load(pos);
    // output.Store(pos, float4((1 - targetColor.rgb) * sourceColor.a, fma(targetColor.a, 1 - sourceColor.a, sourceColor.a)));
    output.Store(pos, ApplyMaskEffect(targetColor, sourceColor.a));
    // 优化：如果 input 完全透明，则不需要做任何计算与覆写
    // if (sourceColor.a > 0.0) {

    //     // 1. 计算颜色的欧几里得距离（差异度）
    //     float colorDiff = distance(sourceColor.rgb, targetColor.rgb);

    //     // 2. 计算相似度 (0.0 到 1.0)。差异越小，相似度越接近 1.0
    //     // 这里的 0.35 是颜色相似的判定阈值，你可以根据视觉需求微调
    //     float similarity = 1.0 - smoothstep(0.0, 1.5f, colorDiff);

    //     // 3. 处理透明度权重：
    //     // 如果 targetColor.a 越接近 0（偏透明），highlightIntensity 就越趋近于 0。
    //     // 这确保了在透明区域不会发生颜色扭曲，完美插值保留 input 的原始颜色。
    //     float highlightIntensity = similarity * targetColor.a;

    //     // 4. 凸显 input：使用 targetColor 的反色来保证绝对的视觉对比度
    //     float3 contrastColor = 1.0 - targetColor.rgb;
    //     sourceColor.rgb = lerp(sourceColor.rgb, contrastColor, highlightIntensity);

    //     // 原有的预乘 Alpha 与混合逻辑
    //     sourceColor.rgb *= sourceColor.a;
    //     let result = pp::blend::pre_scaled_alpha_blend(sourceColor, targetColor);

    //     output.Store(pos, result);
    // }
}