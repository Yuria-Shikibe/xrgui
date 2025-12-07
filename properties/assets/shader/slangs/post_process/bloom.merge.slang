import post_process_lib;

[[vk::binding(0, 0)]]
restrict WTexture2D output;

[[vk::binding(1, 0)]]
restrict readonly RWTexture2D input_base;

[[vk::binding(2, 0)]]
restrict readonly RWTexture2D input_light;

[[vk::binding(3, 0)]]
restrict readonly RWTexture2D input_bloom;

[[shader("compute")]]
[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(
    uint3 dispatchThreadID: SV_DispatchThreadID
) {
    let pos = dispatchThreadID.xy;
    let color_base = input_base.Load(pos);
    let color_light = input_light.Load(pos);
    let color_bloom = input_bloom.Load(pos);

    let scaled_light = color_light.rgb * color_light.a;
    let part_light = float4(pp::blend::screen(scaled_light, color_bloom.rgb), max(color_light.a, color_bloom.a));

    output.Store(pos, color_bloom + color_base - float4(scaled_light, 0.f));
}
