import post_process_lib;

[[vk::binding(0, 0)]]
[[format("rgba16f")]]
restrict WTexture2D output;

[[vk::binding(1, 0)]]
[[format("rgba16f")]]
restrict readonly RWTexture2D input;

//TODO
static const float u_threshold = 1.f;
static const float u_smoothness = 1.f;

[shader("compute")]
[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(
    uint3 dispatchThreadID: SV_DispatchThreadID
) {
    let pos = dispatchThreadID.xy;

    let sourceColor = input.Load(pos);

    let luminance = pp::luma(sourceColor.rgb);

    let highlight = max(0.0, luminance - u_threshold);

    let smoothFactor = smoothstep(0.0, u_smoothness, highlight);

    let result = float4(sourceColor.rgb, sourceColor.a) * smoothFactor;

    output.Store(pos, result);
}