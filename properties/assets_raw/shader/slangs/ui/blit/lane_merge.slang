import post_process_lib;

[[vk::push_constant]]
uint2 offset;

[[vk::binding(0, 0)]]
restrict Texture2D input_base;

[[vk::binding(1, 0)]]
restrict Texture2D input_back;

[[vk::binding(2, 0)]]
restrict RWTexture2D inout_base;

[[vk::binding(3, 0)]]
restrict RWTexture2D inout_back;

[[format("r16ui")]]
[[vk::binding(4, 0)]]
restrict RWTexture2D<uint> inout_back_coverage;

static const float BACK_BASE_TINT_RATIO = 0.75;
static const float BACK_COVERAGE_UNIT = 255.0;
static const uint BACK_COVERAGE_MAX = 65535u;

[ForceInline]
float4 sanitize_lane_color(const in float4 color)
{
    let alpha = saturate(color.a);
    if (alpha <= 0.0) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    return float4(color.rgb, alpha);
}

[ForceInline]
float4 alpha_over(const in float4 src, const in float4 dst)
{
    let inv_src_alpha = 1.0 - src.a;
    return float4(
        fma(dst.rgb, inv_src_alpha, src.rgb),
        saturate(fma(dst.a, inv_src_alpha, src.a))
    );
}

[ForceInline]
float4 darken_base_for_back_cover(const in float4 dst_base, const in float4 src_back)
{
    let darken_factor = 1.0 - src_back.a * .6f;
    return float4(dst_base.rgb * darken_factor, dst_base.a);
}

[ForceInline]
float4 tint_darkened_base_with_back(const in float4 darkened_base, const in float4 src_back)
{
    let base_alpha = saturate(darkened_base.a);
    let tint_src = float4(src_back.rgb * base_alpha, src_back.a * base_alpha);
    let tinted_base = alpha_over(tint_src, darkened_base);
    return float4(
        lerp(darkened_base.rgb, tinted_base.rgb, BACK_BASE_TINT_RATIO),
        darkened_base.a
    );
}

[ForceInline]
uint back_coverage_units_from_alpha(const in float alpha)
{
    return uint(round(saturate(alpha) * BACK_COVERAGE_UNIT));
}

[shader("compute")]
[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(
    uint3 dispatchThreadID: SV_DispatchThreadID
) {
    let pos = dispatchThreadID.xy + offset;

    let src_base = sanitize_lane_color(input_base.Load(int3(pos, 0)));
    let src_back = sanitize_lane_color(input_back.Load(int3(pos, 0)));

    if (src_base.a <= 0.0 && src_back.a <= 0.0) {
        return;
    }

    let dst_base = sanitize_lane_color(inout_base.Load(pos));

    let darkened_base = darken_base_for_back_cover(dst_base, src_back);
    let m1_base = tint_darkened_base_with_back(darkened_base, src_back);
    let m2_base = alpha_over(src_base, m1_base);

    inout_base.Store(pos, m2_base);

    if (src_back.a > 0.0) {
        let dst_back = sanitize_lane_color(inout_back.Load(pos));
        let m3_back = alpha_over(src_back, dst_back);
        inout_back.Store(pos, m3_back);

        let coverage_add = back_coverage_units_from_alpha(src_back.a);
        if (coverage_add > 0u) {
            let coverage_dst = inout_back_coverage.Load(pos);
            inout_back_coverage.Store(pos, min(BACK_COVERAGE_MAX, coverage_dst + coverage_add));
        }
    }
}
