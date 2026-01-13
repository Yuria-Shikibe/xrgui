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
restrict readonly RWTexture2D input;

[[vk::binding(1, 1)]]
restrict RWTexture2D output;


[shader("compute")]
[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(
    uint3 dispatchThreadID: SV_DispatchThreadID
) {
    let pos = dispatchThreadID.xy + ubo.offset;

    let sourceColor = input.Load(pos);
    let targetColor = output.Load(pos);
    let result = pp::blend::alpha_blend(sourceColor, targetColor);

    output.Store(pos, result);
}