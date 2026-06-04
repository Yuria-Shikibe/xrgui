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
    uint width;
    uint height;
    output.GetDimensions(width, height);
    if(pos.x >= width || pos.y >= height) {
        return;
    }

    let color_base = input_base.Load(pos);
    let color_light = input_light.Load(pos);
    let color_bloom = input_bloom.Load(pos);

    let scaled_light = color_light.rgb * color_light.a;
    let part_light = float4(pp::blend::screen(scaled_light, color_bloom.rgb), max(color_light.a, color_bloom.a));

    let merged_rgb = color_base.rgb - scaled_light + part_light.rgb;
    output.Store(pos, float4(merged_rgb, max(color_base.a, part_light.a)));
}
