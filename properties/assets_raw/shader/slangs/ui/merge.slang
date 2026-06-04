import post_process_lib;
import general_lib;

[[vk::binding(0, 0)]]
restrict RWTexture2D output;

[[vk::binding(1, 0)]]
Texture2D input_ui_base;

[[vk::binding(2, 0)]]
Texture2D input_ui_back;

[[vk::binding(3, 0)]]
Texture2D input_background_bloom;

[[vk::binding(4, 0)]]
Texture2D input_background;

[[vk::binding(5, 0)]]
Sampler2D input_background_blurred;

[shader("compute")]
[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(
    uint3 dispatchThreadID: SV_DispatchThreadID
) {
    let pos = dispatchThreadID.xy;

    uint width;
    uint height;
    output.GetDimensions(width, height);
    if(pos.x >= width || pos.y >= height) {
        return;
    }

    let uv = (float2(pos) + float2(0.5)) * rcp(float2(width, height));

    var src_ui_base = input_ui_base.Load(uint3(pos, 0));
    let src_background_bloom = input_background_bloom.Load(uint3(pos, 0));
    let src_ui_back = input_ui_back.Load(uint3(pos, 0));
    let src_background = input_background.Load(uint3(pos, 0));
    let src_background_blurred = input_background_blurred.SampleLevel(uv, 0);

    let background = src_background;
    let background_blurred = float4(src_background_blurred.rgb, src_background.a);
    let dst_background = lerp(background, background_blurred, src_ui_back.a);
    let blend1 = pp::blend::pre_scaled_alpha_blend(float4(src_ui_back.rgb * src_ui_back.a, src_ui_back.a * .6f), dst_background);

    src_ui_base.rgb *= fma(src_ui_base.a, .6f, .4f);
    var blend2 = pp::blend::pre_scaled_alpha_blend(src_ui_base, blend1);
    blend2.rgb += src_background_bloom.rgb;

    output.Store(pos, blend2);
}
