import mesh_instruction_lib;

struct dispatch_config {
    uint group_offset;
    uint[3] _cap;
}

struct ubo_screen_info {
    float3x3 screen_to_uniform;
};

struct scissor {
    float2 src;
    float2 dst;
    float margin;
    uint32_t _c1, _c2, _c3;
}

struct ubo_layer_info {
    float3x3 element_to_screen;
    scissor scissor;
};


[[vk::binding(0, 0)]]
readonly StructuredBuffer<mesh_dispatch_info_v2<2>> dispatch_info;

[[vk::binding(1, 0)]]
StructuredBuffer<instruction_head, Std140DataLayout> instruction_heads;

[[vk::binding(2, 0)]]
ByteAddressBuffer instruction;

[[vk::binding(4, 0)]]
readonly StructuredBuffer<ubo_screen_info, Std140DataLayout> screen_info;

[[vk::binding(5, 0)]]
readonly StructuredBuffer<ubo_layer_info, Std140DataLayout> layer_info;

[[vk::binding(0, 1)]]
ConstantBuffer<dispatch_config> dispatch_config_ubo;

struct trans : vertex_transformer_2<Vertex, 2> {
    uint32_t[2] timeline;

    [[ForceInline]]
    uint index_at(uint slot) {
        return timeline[slot] - 1;
    }

    [[mutating]]
    void set_timeline(uint32_t[2] timeline) {
        this.timeline = timeline;
    }

    [[ForceInline]]
    Vertex operator()(Vertex v) {
        let layer = layer_info[index_at(1)];

        let screen_coord = mul(layer.element_to_screen, float3(v.position.xy, 1));

        let uniformed_coord = mul(screen_info[index_at(0)].screen_to_uniform, screen_coord);

        v.position.xy = uniformed_coord.xy;

        return v;
    }

    [[mutating]]
    [[ForceInline]]
    void operator()(uint march_index) {
        ++timeline[march_index];
    }

}

[shader("mesh")]
[numthreads(GROUP_COUNT, 1, 1)]
[outputtopology("triangle")]
void main_mesh(
    uint groupId: SV_GroupID,
    uint threadId: SV_GroupThreadID,
    vertices out Vertex verts[VERTEX_PER_MESH],
    primitives out Primitive prims[PRIMITIVES_PER_MESH],
    indices out uint3 indices[PRIMITIVES_PER_MESH]
) {
    let gid = groupId + dispatch_config_ubo.group_offset;

    let pmt = resolve_instruction_v2(
        threadId, dispatch_info[gid], instruction_heads, instruction, trans(), verts, prims, indices
    );

    if (threadId == 0) SetMeshOutputCounts(VERTEX_PER_MESH, pmt);
}
