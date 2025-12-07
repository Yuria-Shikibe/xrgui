import mesh_instruction_lib;
import general_lib;
import slide_line;

#ifdef MODE_SDF
import sdf;
#else
#define MODE_SDF 0
#endif 

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

struct ubo_ui_state {
    float time;
    uint32_t[3] _cap;
};

struct VertexUI {
    noperspective Vertex vtx;
    noperspective float[4] clip_dst : SV_ClipDistance;
}

typealias UI_VTX = VertexUI;

[[vk::binding(0, 0)]]
ByteAddressBuffer instruction;

[[vk::binding(1, 0)]]
ConstantBuffer<subrange_info> mesh_subrange;

[[vk::binding(2, 0)]]
Sampler2D textures[MAX_IMAGE_CACHE_COUNT];

[[vk::binding(0, 1)]]
ConstantBuffer<ubo_screen_info> screen_info;

[[vk::binding(1, 1)]]
ConstantBuffer<ubo_layer_info> layer_info;

struct trans : vertex_transformer<UI_VTX>{
    [ForceInline]
    UI_VTX operator()(Vertex v) {
        let screen_coord = mul(layer_info.element_to_screen, float3(v.position.xy, 1));
        let uniformed_coord = mul(screen_info.screen_to_uniform, screen_coord);
        v.position.xy = uniformed_coord.xy;

        UI_VTX vtx;
        vtx.vtx = v;
        float left_plane_dist = screen_coord.x - layer_info.scissor.src.x;
        float right_plane_dist = layer_info.scissor.dst.x - screen_coord.x;
        float top_plane_dist = layer_info.scissor.dst.y - screen_coord.y;
        float bottom_plane_dist = screen_coord.y - layer_info.scissor.src.y;
        vtx.clip_dst = { left_plane_dist, right_plane_dist, top_plane_dist, bottom_plane_dist};
        return vtx;
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
    let pmt = resolve_instruction(
        groupId, threadId, mesh_subrange, instruction, trans(), verts, prims, indices
    );

    if (threadId == 0) SetMeshOutputCounts(VERTEX_PER_MESH, pmt);
}

[[vk::binding(0, 2)]]
ConstantBuffer<ubo_ui_state> ui_state;

[[vk::binding(0, 3)]]
ConstantBuffer<slide_line::slide_line_style> slide_line_style;

enum draw_mode_type : uint32_t{
    none = 0u,

    slide_line = 1u << 0u,
    target_background = 1u << 1u
};

struct FragOut {
    float4 out_base : SV_Target0;
    float4 out_back : SV_Target1;
}

[shader("fragment")]
FragOut main_frag(
    in UI_VTX vtx,
    nointerpolation in Primitive primit
) {
    let no_tex = bool(primit.texture_index >= MAX_IMAGE_CACHE_COUNT);
    float4 out_color_base;
    
#if MODE_SDF == 1
    var alpha = no_tex ? 1 : sdf::msdf(textures[primit.texture_index], vtx.vtx.uv);
    out_color_base = float4(1, 1, 1, smoothstep(-0.015, 0.045, alpha)) * vtx.vtx.color;
    // out_color_base = sdf::visualize(alpha, 0.5) * vtx.vtx.color;
#else
    let tex_color = (no_tex ? float4(1) : textures[primit.texture_index].Sample(vtx.vtx.uv)) * vtx.vtx.color;
    out_color_base = tex_color;
#endif

    if (bool(primit.mode.value & draw_mode_type::slide_line)) {
        float scl = slide_line::is_in_slide_line_smooth(vtx.vtx.position.xy, slide_line_style, ui_state.time);
        out_color_base.a *= scl;
    }

    let target_back = bool(primit.mode.value & draw_mode_type::target_background);

    FragOut out;
    out.out_base = select(target_back, float4(0), out_color_base);
    out.out_back = select(target_back, out_color_base * float4(0, 0, 0, 1), float4(0));
    return out;
}