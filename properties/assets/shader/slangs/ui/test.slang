[[vk::binding(0, 0)]]
RWTexture2D input;

[[vk::binding(1, 0)]]
RWTexture2D output;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(
    uint3 dispatchThreadID: SV_DispatchThreadID
) {
    let pos = dispatchThreadID.xy;

    var color = input.Load(pos);

    let p1 = WaveReadLaneAt(color, pos.x);

    output.Store(pos, p1);
}
