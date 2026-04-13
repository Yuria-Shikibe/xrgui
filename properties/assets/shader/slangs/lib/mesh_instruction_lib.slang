module mesh_instruction_lib;

__exported import mesh_instructions;
import general_lib;

namespace mesh_instr {

public static const uint MAX_IMAGE_CACHE_COUNT = 4;

public static const uint GROUP_COUNT = 32;
public static const uint VERTEX_PER_MESH = 64;
public static const uint PRIMITIVES_PER_MESH = 126;
public static const uint TEXTURE_BINDING_INDEX = 4;

public interface vertex_transformer<T, uint N> {
    [ForceInline]
    T operator()(Vertex v);

    [[mutating]] void set_timeline(uint32_t[N] value);
}

public struct dispatch_config {
    public uint group_offset;
    uint[3] _cap;
}

public struct vertex_resolve_info {
    uint packed_type_timeline;
    uint payload_offset;
    uint packed_skips;
    uint payload_size;

    [ForceInline] instruction_type get_type() { return (instruction_type)(packed_type_timeline & 0xFFFF); }
    [ForceInline] uint get_relative_timeline() { return packed_type_timeline >> 16; }
    [ForceInline] uint get_vtx_skip() { return packed_skips & 0xFFFF; }
    [ForceInline] uint get_prm_skip() { return packed_skips >> 16; }
};

public struct mesh_dispatch_info_v3 {
    uint global_vertex_offset;
    uint primitives;
    uint base_timeline_index;
    uint cap;
};

public struct mesh_instruction_data<uint TimelineSz> {
    StructuredBuffer<mesh_dispatch_info_v3> dispatch_info;
    StructuredBuffer<vertex_resolve_info> resolve_infos;
    StructuredBuffer<uint[TimelineSz]> timelines;
    ByteAddressBuffer instruction;
}

[ForceInline]
public uint resolve_instruction_v3<T, uint n>(
    const uint global_group_index,
    const uint threadId,
    const in mesh_instruction_data<n> data,
    in vertex_transformer<T, n> transformer,
    OutputVertices<T, VERTEX_PER_MESH> verts: __ref,
    OutputPrimitives<Primitive, PRIMITIVES_PER_MESH> prims: __ref,
    OutputIndices<vector<uint, 3>, PRIMITIVES_PER_MESH> indices: __ref
) {
    let dispatchinfo = data.dispatch_info[global_group_index];
    let global_vtx_base = global_group_index * VERTEX_PER_MESH;

    [ForceUnroll]
    for (uint i = 0; i < VERTEX_PER_MESH / GROUP_COUNT; ++i) {
        let target_index = i * GROUP_COUNT + threadId;
        let info = data.resolve_infos[global_vtx_base + target_index];
        let type = info.get_type();

        if (type == instruction_type::noop) continue;

        let absolute_timeline_idx = dispatchinfo.base_timeline_index + info.get_relative_timeline();
        transformer.set_timeline(data.timelines[absolute_timeline_idx]);

        instruction_head dummy_head;
        dummy_head.type = type;
        dummy_head.size = info.payload_size;

        let param = resolve_param(target_index, info.get_vtx_skip(), info.get_prm_skip());


        let rst = push_vertex(
            dummy_head, data.instruction, info.payload_offset, param
        );

        verts[target_index] = transformer(rst.vtx);


        if (param.generates_primitive()) {
            let idc_idx = param.get_primitive_index();
            prims[idc_idx] = rst.pmt;
            indices[idc_idx] = param.getTrivialIndicies();
        }
    }

    return dispatchinfo.primitives;
}

}