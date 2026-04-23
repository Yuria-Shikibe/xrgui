import post_process_lib;

[[vk::binding(0, 0)]]
[[format("rgba16f")]]
restrict WTexture2D output;

[[vk::binding(1, 0)]]
Texture2D input;

struct HighlightParams {
    float threshold;
    float smoothness;
    float max_brightness;
};

[[vk::binding(2, 0)]]
ConstantBuffer<HighlightParams> u_params;

[shader("compute")]
[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(
    uint3 dispatchThreadID: SV_DispatchThreadID
) {
    let pos = dispatchThreadID.xy;
    

    let sourceColor = input.Load(uint3(pos, 0));
    let luma = pp::luma(sourceColor.rgb);

    let t = u_params.threshold;
    let s = u_params.smoothness;
    let softKnee = max(s, 0.0001);
    

    let rq = clamp(luma - t + softKnee, 0.0, 2.0 * softKnee);
    let soft = (rq * rq) / (4.0 * softKnee);
    

    let multiplier = max(luma - t, soft) / max(luma, 0.0001);
    

    float3 highlight = sourceColor.rgb * multiplier;
    



    float highlightAlpha = clamp(multiplier, 0.0, 1.0) * sourceColor.a;



    let highlightLuma = pp::luma(highlight);
    if (highlightLuma > u_params.max_brightness) {

        let clampMultiplier = u_params.max_brightness / max(highlightLuma, 0.0001);
        highlight *= clampMultiplier;
        



    }


    output.Store(pos, float4(highlight, highlightAlpha));
}