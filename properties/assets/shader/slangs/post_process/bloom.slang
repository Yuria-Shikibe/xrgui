import post_process_lib;
import general_lib;

// ==========================================
// 特化常量 (Specialization Constant) 控制开关
// ==========================================
[[vk::constant_id(0)]]
const int IS_PURE_BLUR = false;

struct bloom_defines {
    uint2 currentLayerExtent;
    uint mip_index;
    uint up_scaling;
    uint total_layer;
}

[[vk::push_constant]]
bloom_defines defines;

struct bloom_param {
    float scale;
    float strength_src;
    float strength_dst;
    float mix_factor;
}

[[vk::binding(0, 0)]]
Sampler2D input_image;

[[vk::binding(1, 0)]]
Sampler2D down_mipmap_sample;

[[vk::binding(2, 0)]]
Sampler2D up_mipmap_sample;

[[vk::binding(3, 0)]]
[[format("rgba16f")]]
restrict WTexture2D target;

[[vk::binding(4, 0)]]
ConstantBuffer<bloom_param> param;

static constexpr float3[9] tent_kernal = {
    float3(-1,  1, 1), float3(0,  1, 2), float3(1,  1, 1),
    float3(-1,  0, 2), float3(0,  0, 4), float3(1,  0, 2),
    float3(-1, -1, 1), float3(0, -1, 2), float3(1, -1, 1)
};

[[ForceInline]]
float4 sample_input(const float2 where, const uint raw_mip_index){
    if (raw_mip_index == 0) {
        return input_image.SampleLevel(where, 0);
    } else {
        return down_mipmap_sample.SampleLevel(where, raw_mip_index - 1);
    }
}

[[ForceInline]]
float4 down_sample_optimized(float2 uv, float2 size_inv, uint mip) {
    float4 d = size_inv.xyxy * float4(-1, -1, 1, 1) * 0.5f;
    float4 s1 = sample_input(uv + d.xy, mip);
    float4 s2 = sample_input(uv + d.zy, mip);
    float4 s3 = sample_input(uv + d.xw, mip);
    float4 s4 = sample_input(uv + d.zw, mip);
    return (s1 + s2 + s3 + s4) * 0.25;
}

[[ForceInline]]
float4 down_blur(const float2 where, const float2 size_inv, const float scale, const uint raw_mip_index) {
    // 恢复原版逻辑：确保降采样阶段 100% 覆盖像素，不丢失任何亮度能量（拒绝变灰）
    if (raw_mip_index == 0) {
        var color = float4(0);
        float weight = 0;
        [[ForceUnroll]]
        for (int i = 0; i < 9; ++i) {
            let ker = tent_kernal[i];
            let sampled = sample_input(where + ker.xy * scale * size_inv, raw_mip_index);

            float l;
            if (IS_PURE_BLUR != 0) {
                // 纯模糊模式：不压制高光，保持线性
                l = ker.z;
            } else {
                l = ker.z * rcp(1 + pp::luma(sampled));
            }

            color += sampled * l;
            weight += l;
        }
        return color / weight;
    } else {
        return down_sample_optimized(where, size_inv * scale, raw_mip_index);
    }
}

[[ForceInline]]
float4 up_blur(const float2 where, const float2 size_inv, const float scale, const uint raw_mip_index) {
    // 升采样阶段：纯模糊模式下采用 9 采样 Tent Filter 替代 4 采样 Box Filter
    if (IS_PURE_BLUR != 0) {
        var color = float4(0);
        float weight = 0;

        // 注意：这里不再乘以 1.5，使用原始 size_inv 确保双线性插值的采集范围完美互锁，没有缝隙
        [[ForceUnroll]]
        for (int i = 0; i < 9; ++i) {
            let ker = tent_kernal[i];
            let sampled = sample_input(where + ker.xy * scale * size_inv, raw_mip_index);
            color += sampled * ker.z;
            weight += ker.z;
        }
        return color / weight;
    } else {
        return down_sample_optimized(where, size_inv * scale, raw_mip_index);
    }
}

[[ForceInline]]
float4 screen(float4 src, float4 dst) {
    let screen = 1.0 - (1.0 - src.rgb) * (1.0 - dst.rgb);
    let scr = float4(screen, (src.a + dst.a) * .65f);
    return lerp(src + dst, scr, param.mix_factor);
}

[[shader("compute")]]
[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(
    uint3 dispatchThreadID: SV_DispatchThreadID
) {
    let current_index = defines.mip_index;
    let div = 1 << (current_index + int(current_index < defines.total_layer) - 1);

    let size_inv = float2(1) / float2(defines.currentLayerExtent);
    let mipPos = int2(dispatchThreadID.xy);
    let uv = (float2(mipPos) + float2(0.5f, 0.5f)) * size_inv;

    if (bool(defines.up_scaling)){
        float4 colorSrc;
        // 这里的 up_blur 现在在纯模糊模式下将返回更柔和的结果
        let colorDst = up_blur(uv, size_inv / 2, param.scale, current_index);

        if (current_index == defines.total_layer - 1) {
            colorSrc = down_mipmap_sample.SampleLevel(uv, current_index);
        } else {
            colorSrc = up_mipmap_sample.SampleLevel(uv, current_index + 1);
        }

        float4 src;
        if (IS_PURE_BLUR != 0) {
            src = lerp(colorSrc * param.strength_src, colorDst * param.strength_dst, param.mix_factor);
        } else {
            src = screen(
                float4(colorSrc.rgb * param.strength_src, colorSrc.a),
                float4(colorDst.rgb * param.strength_dst, colorDst.a));
        }

        target.Store(mipPos, src);
    } else {
        // down_blur 现在在纯模糊模式下也能输出更大的模糊半径
        let src = down_blur(uv, size_inv * 2, param.scale, defines.mip_index);
        target.Store(mipPos, src);
    }
}