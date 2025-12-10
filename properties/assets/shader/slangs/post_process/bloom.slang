import post_process_lib;
import general_lib;

struct bloom_param {
    uint mip_index;
    uint up_scaling;
    uint total_layer;
    float scale;
    float strength_src;
    float strength_dst;
    float align_cap1;
    float align_cap2;
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

float4 sample_input(const float2 where, const uint raw_mip_index){
    if (raw_mip_index == 0) {
        return input_image.SampleLevel(where, 0);
    } else {
        return down_mipmap_sample.SampleLevel(where, raw_mip_index - 1);
    }
}


float4 down_blur(const float2 where, const float2 size_inv, const float scale, const uint raw_mip_index){
    var color = float4(0);
    float weight = 0;

    for (int i = 0; i < 9; ++i) {
        let ker = tent_kernal[i];
        let sampled = sample_input(where + ker.xy * scale * size_inv, raw_mip_index);
        let l = raw_mip_index == 0 ? ker.z / (1 + pp::luma(sampled)) : ker.z;
        color += sampled * l;
        weight += l;
    }

    return color /= weight;
}

float4 up_blur(const float2 where, const float2 size_inv, const float scale, const uint raw_mip_index){
    var color = float4(0);
    float weight = 0;

    for (int i = 0; i < 9; ++i) {
        let ker = tent_kernal[i];
        let sampled = sample_input(where + ker.xy * scale * size_inv, raw_mip_index);
        let l = ker.z;
        color += sampled * l;
        weight += l;
    }

    return color /= weight;
}

//TODO
float4 screen(float4 src, float4 dst) {
    let color = 1.0 - (1.0 - src.rgb) * (1.0 - dst.rgb);
    let scr = float4(color, (src.a + dst.a) * .65f);
    return lerp(src + dst, scr, 0.65f);
}

[[shader("compute")]]
[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(
    uint3 dispatchThreadID: SV_DispatchThreadID
) {
    let current_index = param.mip_index;
    let div = 1 << (current_index + int(current_index < param.total_layer) - 1);

    let size_inv = float2(1) / float2(target.imageSize2D());

    let mipPos = int2(dispatchThreadID.xy);
    let uv = (float2(mipPos) + float2(0.5f, 0.5f)) * size_inv;

    if (bool(param.up_scaling)){
        float4 colorSrc;


        let colorDst = up_blur(uv, size_inv / 2, param.scale, current_index);
        if (current_index == param.total_layer - 1) {
            colorSrc = down_mipmap_sample.SampleLevel(uv, current_index);
        } else {
            colorSrc = up_mipmap_sample.SampleLevel(uv, current_index + 1);
        }

        let src = screen(
            float4(colorSrc.rgb * param.strength_src, colorSrc.a), 
            float4(colorDst.rgb * param.strength_dst, colorDst.a));

        target.Store(mipPos, src);
    }else{
        let src = down_blur(uv, size_inv * 2, param.scale, param.mip_index);

        target.Store(mipPos, src);
    }
}
