import post_process_lib;
import general_lib;

[[vk::binding(0, 0)]]
restrict RWTexture2D output;

[[vk::binding(1, 0)]]
Texture2D input_ui_base;

[[vk::binding(2, 0)]]
Texture2D input_ui_back;

[[vk::binding(3, 0)]]
Texture2D<uint> input_ui_back_coverage;

[[vk::binding(4, 0)]]
Texture2D input_background_bloom;

[[vk::binding(5, 0)]]
Texture2D input_background;

[[vk::binding(6, 0)]]
Sampler2D input_background_blurred;

static const float BACK_COVERAGE_UNIT = 255.0;
static const float BACK_DIM_EXPONENT = 1.3219281;

[ForceInline]
float coverage_strength(const in uint coverage_units, const in float fallback_alpha)
{
    let coverage_layers = max(float(coverage_units) / BACK_COVERAGE_UNIT, saturate(fallback_alpha));
    return saturate(1.0 - exp2(-coverage_layers * BACK_DIM_EXPONENT));
}

[ForceInline]
float4 premultiplied_back_tint(const in float4 src_ui_back, const in float strength)
{
    if (src_ui_back.a <= 0.0 || strength <= 0.0) {
        return float4(0.0);
    }

    let straight_rgb = src_ui_back.rgb / src_ui_back.a;
    return float4(straight_rgb * strength, strength);
}

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
    let src_ui_back_coverage = input_ui_back_coverage.Load(uint3(pos, 0));
    let src_background = input_background.Load(uint3(pos, 0));
    let src_background_blurred = input_background_blurred.SampleLevel(uv, 0);

    let back_strength = coverage_strength(src_ui_back_coverage, src_ui_back.a);
    let blur_strength = saturate(back_strength / 0.6);

    let background = src_background;
    let background_blurred = float4(src_background_blurred.rgb, src_background.a);
    let dst_background = lerp(background, background_blurred, blur_strength);
    let blend1 = pp::blend::pre_scaled_alpha_blend(premultiplied_back_tint(src_ui_back, back_strength), dst_background);

    src_ui_base.rgb *= fma(src_ui_base.a, .6f, .4f);
    var blend2 = pp::blend::pre_scaled_alpha_blend(src_ui_base, blend1);
    blend2.rgb += src_background_bloom.rgb;

    output.Store(pos, blend2);
}
