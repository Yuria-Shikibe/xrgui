import post_process_lib;

[[vk::push_constant]]
uint2 offset;

[[vk::binding(0, 0)]]
restrict readonly RWTexture2D input;

[[vk::binding(1, 0)]]
restrict RWTexture2D output;

[shader("compute")]
[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(
    uint3 dispatchThreadID: SV_DispatchThreadID
) {
    let pos = dispatchThreadID.xy + offset;

    var sourceColor = input.Load(pos);
    let targetColor = output.Load(pos);

    // 优化：如果 input 完全透明，则不需要做任何计算与覆写
    if (sourceColor.a > 0.0) {

        // 1. 计算颜色的欧几里得距离（差异度）
        float colorDiff = distance(sourceColor.rgb, targetColor.rgb);

        // 2. 计算相似度 (0.0 到 1.0)。差异越小，相似度越接近 1.0
        // 这里的 0.35 是颜色相似的判定阈值，你可以根据视觉需求微调
        float similarity = 1.0 - smoothstep(0.0, 1.5f, colorDiff);

        // 3. 处理透明度权重：
        // 如果 targetColor.a 越接近 0（偏透明），highlightIntensity 就越趋近于 0。
        // 这确保了在透明区域不会发生颜色扭曲，完美插值保留 input 的原始颜色。
        float highlightIntensity = similarity * targetColor.a;

        // 4. 凸显 input：使用 targetColor 的反色来保证绝对的视觉对比度
        float3 contrastColor = 1.0 - targetColor.rgb;
        sourceColor.rgb = lerp(sourceColor.rgb, contrastColor, highlightIntensity);

        // 原有的预乘 Alpha 与混合逻辑
        sourceColor.rgb *= sourceColor.a;
        let result = pp::blend::pre_scaled_alpha_blend(sourceColor, targetColor);

        output.Store(pos, result);
    }
}