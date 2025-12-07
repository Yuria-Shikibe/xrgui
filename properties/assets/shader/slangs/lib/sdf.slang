module sdf;

import general_lib;

namespace sdf {

[ForceInline]
float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

[[ForceInline]]
float median(float3 v) {
    return median(v.r, v.g, v.b);
}

[[ForceInline]]
public float msdf(Sampler2D samp, float2 uv) {
    let fw = fwidth(uv);
    let lod = min(fw.x, fw.y);

    float2 size;
    float _;
    samp.GetDimensions(uint(lod), size.x, size.y, _);

    let col = samp.SampleLevel(uv, lod).rgb;
    float sigDist = median(col) - 0.5f;

    sigDist *= max(0.5 * dot(rcp(size), rcp(fw)), 1.0);;

    return sigDist;
}

[[ForceInline]]
public float4 visualize(float distance, float scale){
    // 1. 应用缩放
    // 距离值除以缩放因子，以控制颜色过渡的“厚度”。
    // 较大的scale会使过渡更锐利。
    float d = distance / scale;

    // 2. 颜色定义
    // 定义用于渐变的几种颜色

    // 形状内部的颜色 (例如: 蓝色/深色)
    float4 colorInside = float4(0.1, 0.2, 0.6, 1.0);

    // 形状表面的颜色 (例如: 白色/亮色)
    float4 colorSurface = float4(1.0, 1.0, 1.0, 1.0);

    // 形状外部近处的颜色 (例如: 浅蓝色/灰色)
    float4 colorOutsideNear = float4(0.6, 0.8, 1.0, 1.0);

    // 形状外部远处的颜色 (例如: 绿色/背景色)
    float4 colorOutsideFar = float4(0.1, 0.6, 0.1, 1.0);

    // 3. 颜色混合

    float4 finalColor;

    // a) 内部颜色混合 (d <= 0.0)
    // 使用 smoothstep 将内部颜色 colorInside 渐变到表面颜色 colorSurface
    // 这里的 d 在 d < 0 的范围内，-d 则是正值。
    // smoothstep(0.0, 1.0, -d) 使得 d=0 时混合因子为1 (表面颜色)，d=-1 时混合因子接近0 (内部颜色)。
    if (d <= 0.0)
    {
        // 混合因子 k: d=0时k=1 (表面), d=-1时k~0 (内部)
        float k = smoothstep(0.0, 1.0, -d);
        finalColor = lerp(colorInside, colorSurface, k);
    }
    // b) 外部颜色混合 (d > 0.0)
    // 外部颜色从表面颜色 colorSurface -> 外部近处 colorOutsideNear -> 外部远处 colorOutsideFar 渐变。
    else
    {
        // 表面到近处过渡 (d 从 0.0 到 1.0)
        float k1 = smoothstep(0.0, 1.0, d);

        // 混合 colorSurface 和 colorOutsideNear
        float4 nearColor = lerp(colorSurface, colorOutsideNear, k1);

        // 近处到远处过渡 (d 从 1.0 到 2.0)
        float k2 = smoothstep(1.0, 2.0, d);

        // 混合 nearColor 和 colorOutsideFar
        finalColor = lerp(nearColor, colorOutsideFar, k2);
    }

    return finalColor;
}

}