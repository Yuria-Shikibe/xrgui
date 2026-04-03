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

// 1. 绑定 0：替换为 V3 版的 Dispatch Info
[[vk::binding(0, 0)]]
StructuredBuffer<mesh_dispatch_info_v3> dispatch_info;

// 2. 绑定 1：新增的拍平顶点解析信息
[[vk::binding(1, 0)]]
StructuredBuffer<vertex_resolve_info> resolve_infos;

// 3. 绑定 2：新增的拍平 Timeline 数组 (N=2)
[[vk::binding(2, 0)]]
StructuredBuffer<uint[2]> timelines;

// 4. 绑定 3：原先的 payload instruction 后移
[[vk::binding(3, 0)]]
ByteAddressBuffer instruction;

// 注意：Binding 4 被 Textures 预留了（根据 C++ 端的设置）

// 5. 绑定 5+：UBO 数据整体后移
[[vk::binding(5, 0)]]
readonly StructuredBuffer<ubo_screen_info, Std140DataLayout> screen_info;

[[vk::binding(6, 0)]]
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

    // 虽然在 V3 解析器中不再调用此函数（Timeline推演已转移到CPU），
    // 但为了满足 vertex_transformer_2 接口签名，保留空实现或原逻辑均可。
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
    
    // 6. 调用 V3 的解析函数，传入新的 Buffer
    let pmt = resolve_instruction_v3(
        threadId, 
        dispatch_info[gid], 
        resolve_infos, 
        timelines, 
        instruction, 
        trans(), 
        verts, 
        prims, 
        indices
    );
    
    if (threadId == 0) SetMeshOutputCounts(VERTEX_PER_MESH, pmt);
}