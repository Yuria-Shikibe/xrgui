[shader("mesh")]
[numthreads(1, 1, 1)]
[outputtopology("triangle")]
void main(
    uint groupId: SV_GroupID,
    uint threadId: SV_GroupThreadID,
    vertices out float4 verts[64],
    indices out uint3 indices[126],
) {
    let handle = DescriptorHandle<ByteAddressBuffer>(0);
    if (threadId == 0) SetMeshOutputCounts(0, handle.Load<uint>(0));
}
