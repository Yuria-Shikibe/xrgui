import post_process_lib;

[[vk::push_constant]]
uint2 offset;

struct blit_info {
    uint2 offset;
    uint2 _cap;
}

[[vk::binding(0, 0)]]
ConstantBuffer<blit_info> ubo;



[[vk::binding(0, 1)]]
restrict readonly RWTexture2D input_base;

[[vk::binding(1, 1)]]
restrict readonly RWTexture2D input_back;

[[vk::binding(2, 1)]]
restrict RWTexture2D output_base;

[[vk::binding(3, 1)]]
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
    let pos = dispatchThreadID.xy + ubo.offset;

    // TODO make alpha ctrlable by using blend and clear color?


    let color_base_src = input_base.Load(pos);
    let color_back_src = input_back.Load(pos);

    let shadow1 = float4(color_back_src.rgb, color_back_src.a * .85f);
    let blended = pp::blend::alpha_blend(color_base_src, color_back_src);

    let color_base_dst = output_base.Load(pos);//pp::blend::alpha_blend(shadow1, output_base.Load(pos));

    output_base.Store(pos, pp::blend::alpha_blend(color_base_src, pp::blend::alpha_blend(shadow1, color_base_dst)));
    output_back.Store(pos, float4(0));
}