import post_process_lib;

[[vk::binding(0, 0)]]
[[format("rgba16f")]]
restrict WTexture2D output;

[[vk::binding(1, 0)]]
Texture2D input;

struct HighlightParams {
    float threshold;
    float smoothness;
    float max_brightness; // 新增：用于抑制超高亮像素，防止 Bloom 闪烁
};

[[vk::binding(2, 0)]]
ConstantBuffer<HighlightParams> u_params;

[shader("compute")]
[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(
    uint3 dispatchThreadID: SV_DispatchThreadID
) {
    let pos = dispatchThreadID.xy;
    
    // 注意：Texture2D 的 Load 需要传入带有 mipmap 层级的 int3 或 uint3
    let sourceColor = input.Load(uint3(pos, 0));
    let luma = pp::luma(sourceColor.rgb);

    let t = u_params.threshold;
    let s = u_params.smoothness;
    let softKnee = max(s, 0.0001);
    
    // Soft Knee 高光提取
    let rq = clamp(luma - t + softKnee, 0.0, 2.0 * softKnee);
    let soft = (rq * rq) / (4.0 * softKnee);
    
    // multiplier 的范围是从 0 开始，随着高光强度增加而趋近于 1
    let multiplier = max(luma - t, soft) / max(luma, 0.0001);
    
    // 提取出的初步高光颜色
    float3 highlight = sourceColor.rgb * multiplier;
    
    // 【新增逻辑】计算过渡后的 Alpha 值
    // 将 multiplier 钳制在 0 到 1 之间，并与原图 Alpha 相乘
    // 这样不仅暗部完全透明，还能正确保留原图中本就透明的区域
    float highlightAlpha = clamp(multiplier, 0.0, 1.0) * sourceColor.a;

    // 优化 2: 抑制“萤火虫”效应 (Firefly Reduction)
    // 保持色彩比例不变的前提下，限制最大亮度
    let highlightLuma = pp::luma(highlight);
    if (highlightLuma > u_params.max_brightness) {
        // 使用缩放因子，避免硬截断导致色偏 (Color Shift)
        let clampMultiplier = u_params.max_brightness / max(highlightLuma, 0.0001);
        highlight *= clampMultiplier;
        
        // 可选：如果萤火虫效应被压制，Alpha 是否也需要同步压制？
        // 如果你需要极高亮度被抑制时 Alpha 依然保持为 1，则不要乘以下面这行：
        // highlightAlpha *= clampMultiplier; 
    }

    // 写入结果，将提取后的高光与动态计算的 Alpha 组合
    output.Store(pos, float4(highlight, highlightAlpha));
}