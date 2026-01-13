import post_process_lib;

[[vk::push_constant]]
uint2 offset;

[[vk::binding(0, 0)]]
restrict readonly RWTexture2D input;

[[vk::binding(1, 0)]]
restrict RWTexture2D output;


[shader("compute")]
[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(
    uint3 dispatchThreadID: SV_DispatchThreadID
) {
    let pos = dispatchThreadID.xy + offset;

    let sourceColor = input.Load(pos);
    let targetColor = output.Load(pos);
    let result = pp::blend::alpha_blend(sourceColor, targetColor);

    output.Store(pos, result);
}