
import mesh_instruction_lib;
import general_lib;
import slide_line;
import sdf;

[[vk::binding(4, 0)]]
uniform Sampler2D textures[];

struct ubo_ui_state {
    float2 screen_extent;
    float time;
    uint32_t _cap;
};

[[vk::binding(1, 1)]]
ConstantBuffer<ubo_ui_state> ui_state;

[[vk::binding(2, 1)]]
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
    uint mode;
}

[[vk::push_constant]]
pipeline_flags constant_flags;


[shader("fragment")]
FragOut main_frag(
    in Vertex vtx,
    in Primitive primit
) {

    let no_tex = bool(primit.texture_index == ~0U);
    float4 out_color_base;

    switch (global_draw_mode_type(constant_flags.mode)) {
    case global_draw_mode_type::def:
        let tex_color = (no_tex ? float4(1.f) : textures[primit.texture_index].Sample(vtx.uv)) * vtx.color;
        out_color_base = tex_color;
        break;
    case global_draw_mode_type::sdf:
        if (no_tex) {
            out_color_base = float4(1.f) * vtx.color;
        } else {
            let rst = sdf::msdf(textures[primit.texture_index], vtx.uv, float2(4096.f, 4096.f));
            let rng = rst.scale * primit.sdf_expand;
            out_color_base = float4(1.f, 1.f, 1.f, smoothstep(-0.6f, 0.6f, rst.factor - rng)) * vtx.color;
        }
       
        break;
    default: out_color_base = vtx.color;
    }
    
    if (bool(primit.mode.value & draw_mode_type::slide_line)) {
        float scl = slide_line::is_in_slide_line_smooth(vtx.position.xy, slide_line_style, ui_state.time);
        out_color_base.a *= scl;
    }

    FragOut out;
    out.out_base = out_color_base;
    out.out_base.rgb *= out.out_base.a;
    
    return out;
}