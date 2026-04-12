import post_process_lib;

[[vk::push_constant]]
uint2 offset;

[[vk::binding(0, 0)]]
restrict readonly RWTexture2D input;

[[vk::binding(1, 0)]]
restrict RWTexture2D output;

float4 ApplyMaskEffect(float4 dst, float mask)
{


    float alphaWeight = saturate(dst.a);


    float4 emptyTarget = float4(1.0, 1.0, 1.0, 1.0);




    float4 invertedTarget = float4(1.0 - dst.rgb, dst.a);


    float4 effectColor = lerp(emptyTarget, invertedTarget, alphaWeight);




    return lerp(dst, effectColor, mask);
}

[shader("compute")]
[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(
    uint3 dispatchThreadID: SV_DispatchThreadID
) {
    let pos = dispatchThreadID.xy + offset;

    var sourceColor = input.Load(pos);
    let targetColor = output.Load(pos);

    output.Store(pos, ApplyMaskEffect(targetColor, sourceColor.a));

























}