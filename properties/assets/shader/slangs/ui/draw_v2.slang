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
    noperspective float[4] clip_dst : SV_ClipDistance;
}

typealias UI_VTX = VertexUI;


[[vk::binding(0, 0)]]
ConstantBuffer<dispatch_config> dispatch_config_ubo;

[[vk::binding(1, 0)]]
readonly StructuredBuffer<mesh_dispatch_info_v2<2>> dispatch_info;

[[vk::binding(2, 0)]]
ByteAddressBuffer instruction;

[[vk::binding(3, 0)]]
uniform Sampler2D textures[];

[[vk::binding(4, 0)]]
readonly StructuredBuffer<ubo_screen_info> screen_info;

[[vk::binding(5, 0)]]
readonly StructuredBuffer<ubo_layer_info> layer_info;

struct trans : vertex_transformer_2<UI_VTX> {
    uint32_t[2] timeline;

    [[ForceInline]]
    uint index_at(uint slot) {
        return timeline[slot] - 1;
    }
    
    [[ForceInline]]
    __init(uint group_index) {
        this.timeline = dispatch_info[group_index].timeline;
    }

    [[ForceInline]]
    UI_VTX operator()(Vertex v) {
        let layer = layer_info[index_at(1)];

        let screen_coord = mul(layer.element_to_screen, float3(v.position.xy, 1));

        let uniformed_coord = mul(screen_info[index_at(0)].screen_to_uniform, screen_coord);

        v.position.xy = uniformed_coord.xy;

        UI_VTX vtx;
        vtx.vtx = v;
        float left_plane_dist = screen_coord.x - layer.scissor.src.x;
        float right_plane_dist = layer.scissor.dst.x - screen_coord.x;
        float top_plane_dist = layer.scissor.dst.y - screen_coord.y;
        float bottom_plane_dist = screen_coord.y - layer.scissor.src.y;
        vtx.clip_dst = { left_plane_dist, right_plane_dist, top_plane_dist, bottom_plane_dist };
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

    let gid = groupId + dispatch_config_ubo.group_offset;

    let pmt = resolve_instruction_v2(
        threadId, dispatch_info[gid], instruction, trans(gid), verts, prims, indices
    );

    if (threadId == 0) SetMeshOutputCounts(VERTEX_PER_MESH, pmt);
}

struct ubo_ui_state {
    float time;
    uint32_t[3] _cap;
};

[[vk::binding(0, 1)]]
ConstantBuffer<ubo_ui_state> ui_state;

[[vk::binding(1, 1)]]
ConstantBuffer<slide_line::slide_line_style> slide_line_style;

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
    uint mode_sdf;
}

[[vk::push_constant]]
pipeline_flags constant_flags;

[shader("fragment")]
FragOut main_frag(
    in UI_VTX vtx,
    nointerpolation in Primitive primit
) {

    let no_tex = bool(primit.texture_index == ~0U);
    float4 out_color_base;

    switch (global_draw_mode_type(constant_flags.mode_sdf)) {
    case global_draw_mode_type::def:
        let tex_color = (no_tex ? float4(1) : textures[primit.texture_index].Sample(vtx.vtx.uv)) * vtx.vtx.color;
        out_color_base = tex_color;
        break;
    case global_draw_mode_type::sdf:
        var alpha = no_tex ? 1 : sdf::msdf(textures[primit.texture_index], vtx.vtx.uv);
        out_color_base = float4(1, 1, 1, smoothstep(-0.015, 0.045, alpha)) * vtx.vtx.color;
        break;
    default: out_color_base = vtx.vtx.color;
    }
    
    if (bool(primit.mode.value & draw_mode_type::slide_line)) {
        float scl = slide_line::is_in_slide_line_smooth(vtx.vtx.position.xy, slide_line_style, ui_state.time);
        out_color_base.a *= scl;
    }

    FragOut out;
    out.out_base = out_color_base;

    return out;
}