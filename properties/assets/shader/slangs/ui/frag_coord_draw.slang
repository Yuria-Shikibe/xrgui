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
    uint[2] timeline;

    [[ForceInline]]
    uint index_at(uint slot) {
        return timeline[slot] - 1;
    }

    [[mutating]]
    void set_timeline(uint[2] timeline) {
        this.timeline = timeline;
    }

    [[ForceInline]]
    Vertex operator()(Vertex v) {


        v.uv = v.position.xy;

        let layer = input_params.layer_info[index_at(1)];
        let screen_coord = mul(layer.element_to_screen, float3(v.position.xy, 1.f));
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
    let pmt = resolve_instruction_v3(
        gid, threadId, input_params.data,
        trans(), 
        verts, prims, indices
    );
    if (threadId == 0) SetMeshOutputCounts(VERTEX_PER_MESH, pmt);
}





struct FragOut {
    float4 out_base : SV_Target0;
}



static const float GRID_SIZE = 200.0f;
static const float GRID_WIDTH = 2.5f;
static const float4 GRID_COLOR = float4(0.76f, 0.76f, 0.76f, 0.8f);


static const float DASH_LEN = 8.0f;
static const float DASH_GAP = 6.0f;


static const float AXIS_WIDTH = 5.0f;
static const float4 AXIS_X_COLOR = float4(1.0f, 0.2f, 0.2f, 1.0f);
static const float4 AXIS_Y_COLOR = float4(0.2f, 0.8f, 0.2f, 1.0f);

[shader("fragment")]
FragOut main_frag(
    in mesh_instr::Vertex vtx,
    in mesh_instr::Primitive primit
) {

    float2 local_pos = vtx.uv;



    float2 df = max(fwidth(local_pos), 1e-5);






    float2 grid_dist = abs(frac(local_pos / GRID_SIZE + 0.5f) - 0.5f) * GRID_SIZE;

    float2 grid_pixel_dist = grid_dist / df;


    float dash_pattern_y = frac(local_pos.y / (DASH_LEN + DASH_GAP)) * (DASH_LEN + DASH_GAP);
    float dash_alpha_y   = (dash_pattern_y < DASH_LEN) ? 1.0f : 0.0f;


    float dash_pattern_x = frac(local_pos.x / (DASH_LEN + DASH_GAP)) * (DASH_LEN + DASH_GAP);
    float dash_alpha_x   = (dash_pattern_x < DASH_LEN) ? 1.0f : 0.0f;


    float grid_v = (1.0f - smoothstep(GRID_WIDTH * 0.5f - 0.5f, GRID_WIDTH * 0.5f + 0.5f, grid_pixel_dist.x)) * dash_alpha_y;
    float grid_h = (1.0f - smoothstep(GRID_WIDTH * 0.5f - 0.5f, GRID_WIDTH * 0.5f + 0.5f, grid_pixel_dist.y)) * dash_alpha_x;
    

    float grid_alpha = max(grid_v, grid_h);





    float2 axis_pixel_dist = abs(local_pos) / df;


    float axis_y_line = 1.0f - smoothstep(AXIS_WIDTH * 0.5f - 0.5f, AXIS_WIDTH * 0.5f + 0.5f, axis_pixel_dist.x);

    float axis_x_line = 1.0f - smoothstep(AXIS_WIDTH * 0.5f - 0.5f, AXIS_WIDTH * 0.5f + 0.5f, axis_pixel_dist.y);





    float4 final_color = float4(0.0f, 0.0f, 0.0f, 0.0f);


    final_color = lerp(final_color, GRID_COLOR, grid_alpha * GRID_COLOR.a);
    final_color = lerp(final_color, AXIS_X_COLOR, axis_x_line * AXIS_X_COLOR.a);
    final_color = lerp(final_color, AXIS_Y_COLOR, axis_y_line * AXIS_Y_COLOR.a);

    FragOut out;
    out.out_base = final_color;
    

    out.out_base.rgb *= out.out_base.a;
    
    return out;
}