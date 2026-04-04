import mesh_instruction_lib;
import ui_structs;

using namespace mesh_instr;

struct params {
    mesh_instruction_data<2> data;
    uniform Sampler2D _cap;
    StructuredBuffer<gui::ubo_screen_info, Std140DataLayout> screen_info;
    StructuredBuffer<gui::ubo_layer_info, Std140DataLayout> layer_info;
}

[[vk::binding(0, 0)]]
ParameterBlock<params> input_params;


[[vk::binding(0, 1)]]
ConstantBuffer<dispatch_config> dispatch_config_ubo;

struct trans : vertex_transformer<Vertex, 2> {
    uint32_t[2] timeline;

    [[ForceInline]]
    uint index_at(uint slot) {
        return timeline[slot] - 1;
    }

    [[mutating]]
    [[ForceInline]]
    void set_timeline(uint32_t[2] timeline) {
        this.timeline = timeline;
    }

    [[ForceInline]]
    Vertex operator()(Vertex v) {
        let layer = input_params.layer_info[index_at(1)];
        let screen_coord = mul(layer.element_to_screen, float3(v.position.xy, 1));

        let uniformed_coord = mul(input_params.screen_info[index_at(0)].screen_to_uniform, screen_coord);

        v.position.xy = uniformed_coord.xy;

        return v;
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
        gid, threadId, input_params.data,
        trans(), 
        verts, prims, indices
    );
    
    if (threadId == 0) SetMeshOutputCounts(VERTEX_PER_MESH, pmt);
}