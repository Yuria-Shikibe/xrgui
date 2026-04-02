// [require(spvDescriptorHeapEXT)]
module ui_draw;

import mesh_instruction_lib;
import general_lib;
import slide_line;
import sdf;

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

struct VertexUI {
    noperspective Vertex vtx;
}

struct ubo_ui_state {
    float time;
    uint32_t[3] _cap;
}; 

typealias UI_VTX = VertexUI;

struct dbh_input {
    uint32_t dispatch_buffer_start_index;

    uint32_t dispatch_config_index;
    uint32_t ui_state_index;
    uint32_t slide_line_style_index;

    func get_buffer_<T : IOpaqueDescriptor, uint off>() -> T {
        return *DescriptorHandle<T>(uint2(dispatch_buffer_start_index + off, 0));
    }

    func get_dispatch_info() -> StructuredBuffer<mesh_dispatch_info_v2<2>> {
        return get_buffer_<StructuredBuffer<mesh_dispatch_info_v2<2>>, 0>();
    }

    func get_instruction_heads() -> StructuredBuffer<instruction_head, Std140DataLayout> {
        return get_buffer_<StructuredBuffer<instruction_head, Std140DataLayout>, 1>();
    }

    func get_instruction() -> ByteAddressBuffer {
        return get_buffer_<ByteAddressBuffer, 2>();
    }

    func get_screen_info()->StructuredBuffer<ubo_screen_info, Std140DataLayout> {
        return get_buffer_<StructuredBuffer<ubo_screen_info, Std140DataLayout>, 3>();
    }

    func get_layer_info()->StructuredBuffer<ubo_layer_info, Std140DataLayout> {
        return get_buffer_<StructuredBuffer<ubo_layer_info, Std140DataLayout>, 4>();
    }

    func get_dispatch_config()-> dispatch_config {
        return get_buffer_<StructuredBuffer<dispatch_config, Std140DataLayout>, 5>()[dispatch_config_index];
    }

    func get_ui_state()->ubo_ui_state {
        return get_buffer_<StructuredBuffer<ubo_ui_state, Std140DataLayout>, 6>()[ui_state_index];
    }

    func get_slide_line_style()->slide_line::slide_line_style {
        return get_buffer_<StructuredBuffer<slide_line::slide_line_style, Std140DataLayout>, 7>()[slide_line_style_index];
    }

    Texture2D get_image_at(uint image_index) {
        let handle = DescriptorHandle<Texture2D>(uint2(image_index, 0));
        return *nonuniform(handle);
    }
}

[[vk::push_constant]]
dbh_input dbh_input_handles;

struct trans : vertex_transformer_2<UI_VTX, 2> {
    uint32_t[2] timeline;

    [[ForceInline]]
    uint index_at(uint slot) {
        return timeline[slot] - 1;
    }
    
    [[ForceInline]]
    __init(uint group_index) {
        this.timeline = dbh_input_handles.get_dispatch_info()[group_index].timeline;
    }

    [[ForceInline]]
    UI_VTX operator()(Vertex v) {
        let layer = dbh_input_handles.get_layer_info()[index_at(1)];

        let screen_coord = mul(layer.element_to_screen, float3(v.position.xy, 1));

        let uniformed_coord = mul(dbh_input_handles.get_screen_info()[index_at(0)].screen_to_uniform, screen_coord);

        v.position.xy = uniformed_coord.xy;

        UI_VTX vtx;
        vtx.vtx = v;
        return vtx;
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
    vertices out UI_VTX verts[VERTEX_PER_MESH],
    primitives out Primitive prims[PRIMITIVES_PER_MESH],
    indices out uint3 indices[PRIMITIVES_PER_MESH],
) {

    let gid = groupId + dbh_input_handles.get_dispatch_config().group_offset;

    let pmt = resolve_instruction_v2(
        threadId, dbh_input_handles.get_dispatch_info()[gid], dbh_input_handles.get_instruction_heads(), dbh_input_handles.get_instruction(), trans(gid), verts, prims, indices
    );

    if (threadId == 0) SetMeshOutputCounts(VERTEX_PER_MESH, pmt);
}

struct FragOut {
    float4 out_base : SV_Target0;
}

enum draw_mode_type : uint32_t {
    none = 0u,

    slide_line = 1u << 0u,
    target_background = 1u << 1u
};

enum global_draw_mode_type {
    def,
    sdf
}

struct pipeline_flags {
    uint mode;
}


[shader("fragment")]
FragOut main_frag(
    in Vertex vtx,
    nointerpolation in Primitive primit
) {
    SamplerState sampler = SamplerState.Handle(0);

    let no_tex = bool(primit.texture_index == ~0U);
    float4 out_color_base;

    // switch (global_draw_mode_type(0)) {
    // case global_draw_mode_type::def:
    //     let tex_color = (no_tex ? float4(1) : dbh_input_handles.get_image_at(primit.texture_index).Sample(sampler, vtx.uv)) * vtx.color;
    //     out_color_base = tex_color;
    //     break;
    // case global_draw_mode_type::sdf:
    //     var alpha = no_tex ? 1 : sdf::msdf(dbh_input_handles.get_image_at(primit.texture_index), sampler, vtx.uv);
    //     out_color_base = float4(1, 1, 1, smoothstep(-0.015, 0.045, alpha)) * vtx.color;
    //     break;
    // default: break;
    // }

    // if (bool(primit.mode.value & draw_mode_type::slide_line)) {
    //     float scl = slide_line::is_in_slide_line_smooth(vtx.position.xy, dbh_input_handles.get_slide_line_style(), dbh_input_handles.get_ui_state().time);
    //     out_color_base.a *= scl;
    // }

    FragOut out;
    out.out_base = float4(0.2f);
    // out.out_base = out_color_base;
    // out.out_base.rgb *= out.out_base.a;
    
    return out;
}