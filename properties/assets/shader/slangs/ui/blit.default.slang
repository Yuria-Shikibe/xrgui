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


    let color_base_src = input_base.Load(pos);
    let color_back_src = input_back.Load(pos);

    let color_base_dst = output_base.Load(pos);
    let color_back_dst = output_back.Load(pos);




    const float FADE_ALPHA_INTENSITY = 0.8;
    const float FADE_LUMA_INTENSITY = 0.1;


    float alpha_fade_factor = 1.0 - (color_back_src.a * FADE_ALPHA_INTENSITY);
    float luma_fade_factor = 1.0 - (color_back_src.a * FADE_LUMA_INTENSITY);

    float4 faded_base_dst = color_base_dst;
    faded_base_dst.a *= alpha_fade_factor;

    faded_base_dst.rgb *= (alpha_fade_factor * luma_fade_factor);





    static const float BACK_BLEED_RATIO = 0.35;


    float4 bleed_src = color_back_src;


    bleed_src.a *= BACK_BLEED_RATIO;
    bleed_src.rgb *= BACK_BLEED_RATIO;



    bleed_src *= faded_base_dst.a;


    float4 temp_base_dst = pp::blend::pre_scaled_alpha_blend(bleed_src, faded_base_dst);


    temp_base_dst *= faded_base_dst.a;





    float4 final_back_dst = pp::blend::pre_scaled_alpha_blend(color_back_src, color_back_dst);


    float4 final_base_dst = pp::blend::pre_scaled_alpha_blend(color_base_src, temp_base_dst);


    output_base.Store(pos, final_base_dst);
    output_back.Store(pos, final_back_dst);
}