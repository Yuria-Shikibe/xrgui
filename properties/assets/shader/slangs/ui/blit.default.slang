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

[shader("compute")]
[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(
    uint3 dispatchThreadID: SV_DispatchThreadID
) {
    let pos = dispatchThreadID.xy + offset;

    // 读取所有源图像和目标图像数据
    let color_base_src = input_base.Load(pos);
    let color_back_src = input_back.Load(pos);

    let color_base_dst = output_base.Load(pos);
    let color_back_dst = output_back.Load(pos);

    // ==========================================
    // 阶段一：让下层图像 (output_base) 变淡
    // ==========================================
    const float FADE_ALPHA_INTENSITY = 0.8; // 透明度降低强度
    const float FADE_LUMA_INTENSITY = 0.1; // 亮度降低强度

    // 根据 input_back 的 Alpha 计算淡化因子
    float alpha_fade_factor = 1.0 - (color_back_src.a * FADE_ALPHA_INTENSITY);
    float luma_fade_factor = 1.0 - (color_back_src.a * FADE_LUMA_INTENSITY);

    float4 faded_base_dst = color_base_dst;
    faded_base_dst.a *= alpha_fade_factor;
    // 预乘Alpha规则：RGB必须跟随Alpha一同缩放，再叠加额外的亮度压暗
    faded_base_dst.rgb *= (alpha_fade_factor * luma_fade_factor);

    // ==========================================
    // 阶段二：将 input_back 混合“一部分”到变淡后的 output_base 上
    // ==========================================
    // 控制 input_back 混合到 output_base 的比例 (0.0 = 完全不混合，1.0 = 100%混合)
    static const float BACK_BLEED_RATIO = 0.35;

    // 提取 input_back 作为混合源
    float4 bleed_src = color_back_src;

    // 缩放我们要混合的这“一部分”的值（RGB和A同步缩放以保持预乘正确）
    bleed_src.a *= BACK_BLEED_RATIO;
    bleed_src.rgb *= BACK_BLEED_RATIO;

    // 遮罩处理：通常我们只希望它混合到目标图层有像素（非完全透明）的地方
    // 如果你希望它能溢出到目标图层原本透明的地方，可以注释掉下面这一行
    bleed_src *= faded_base_dst.a;

    // 将这部分背景色叠加上去
    float4 temp_base_dst = pp::blend::pre_scaled_alpha_blend(bleed_src, faded_base_dst);

    // 维持你原代码中的 Alpha 缩放逻辑结构
    temp_base_dst *= faded_base_dst.a;

    // ==========================================
    // 阶段三：处理最终的两层混合输出
    // ==========================================
    // 1. Src[1] (完整背景色) 和 Dst[1] (目标背景色) 正常预乘混合
    float4 final_back_dst = pp::blend::pre_scaled_alpha_blend(color_back_src, color_back_dst);

    // 2. Src[0] (前景色) 和 经过前面阶段处理后的 Dst[0] 进行预乘混合
    float4 final_base_dst = pp::blend::pre_scaled_alpha_blend(color_base_src, temp_base_dst);

    // 写回结果
    output_base.Store(pos, final_base_dst);
    output_back.Store(pos, final_back_dst);
}