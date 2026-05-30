module mesh_instructions;
import general_lib;

[ForceInline]
float2 get_dir_vec2(float rad) {
    float2 rst;
    sincos(rad, rst.y, rst.x);
    return rst;
}

namespace mesh_instr {

public enum instruction_type : uint32_t
{
    noop,
    poly,
    poly_partial,
    constrained_curve,
    SIZE,
};

public struct draw_mode
{
    public uint32_t value;
};

public struct resolve_param
{
    public uint vtx_idx;
    public uint vtx_skip;
    public uint prm_skip;

    [ForceInline]
    public bool generates_primitive() {
        return prm_skip != 0xFFFF;
    }

    [ForceInline]
    public uint get_primitive_index() {
        return prm_skip;
    }

    [ForceInline]
    public uint3 getTrivialIndicies() {
        return uint3(vtx_idx - 2, vtx_idx - 1, vtx_idx);
    }
}

struct image_index
{
    uint32_t index : TextureIndex;
    uint32_t _cap;
};

struct primitive_generic
{
    image_index image;
    draw_mode mode;
    float depth;
};

public struct Vertex
{
    public float4 position : SV_Position;
    public float4 color;
    public float2 uv;

    public __init(float3 p_, float4 c_, float2 uv_) {
        position = float4(p_, 1.f);
        color = c_;
        uv = uv_;
    }

    public __init() {
        position = {};
        color = {};
        uv = {};
    }
};

public struct Primitive
{
    nointerpolation public uint texture_index : PmtTexID = {};
    nointerpolation public draw_mode mode : PmtMode = {};
    nointerpolation public float sdf_expand : PmtParam = {};

    [ForceInline]
    __init(primitive_generic generic, resolve_param param) {
        this.texture_index = uint(generic.image.index);
        this.mode = generic.mode;
    }

    public __init() {
        this.texture_index = ~0U;
    }
};

public struct resolve_result
{
    public Vertex vtx;
    public Primitive pmt;
};

public struct instruction_head
{
    public instruction_type type;
    public uint32_t size;
    public uint32_t vertex_count;
    public uint32_t primit_count;
};

struct section<T>
    where T : IArithmetic
{
    T from;
    T to;

    __subscript(uint index)->T {
        get { return bool(index & 1U) ? to : from; }
        set {
            if (bool(index & 1U)) {
                to = newValue;
            } else {
                from = newValue;
            }
        }
    }
};

typealias range = section<float>;

struct poly_draw
{
    primitive_generic generic;

    float2 pos;
    float segments_inv;
    float initial_angle;

    uint32_t cap1;
    uint32_t cap2;
    range radius;

    float2 uv00, uv11;
    float4 inner, outer;

    [ForceInline]
    private float angle_at(uint index) {
        return fma(float(index) * segments_inv, float.getPi() * 2, initial_angle);
    }

    [ForceInline]
    resolve_result push_vertex(resolve_param param) {
        let i = param.vtx_skip / 2;
        let angle = angle_at(i);
        let direction = get_dir_vec2(angle);
        let r = radius[param.vtx_skip];

        resolve_result rst;
        rst.pmt = Primitive(generic, param);
        rst.vtx = Vertex(float3(fma(direction, r, pos), generic.depth), bool(param.vtx_skip & 1u) ? outer : inner, 0);
        return rst;
    }
};

struct poly_partial_draw
{
    primitive_generic generic;
    float2 pos;
    float segments_inv;
    uint32_t cap;

    float ratio_src;
    float ratio_ext;
    range radius;

    float2 uv00, uv11;
    section<float4> color_src;
    section<float4> color_dst;

    [ForceInline]
    private float angle_at(uint index) {
        return fma(float(index) * segments_inv, ratio_ext, ratio_src) * float.getPi() * 2;
    }

    [ForceInline]
    resolve_result push_vertex(resolve_param param) {
        let i = param.vtx_skip / 2;
        let angle = angle_at(i);
        let direction = get_dir_vec2(angle);
        let r = radius[param.vtx_skip];

        let colorSrc = color_src[param.vtx_skip];
        let colorDst = color_dst[param.vtx_skip];
        let color = lerp(colorSrc, colorDst, float(i) * segments_inv);

        resolve_result rst;
        rst.pmt = Primitive(generic, param);
        rst.vtx = Vertex(float3(fma(direction, r, pos), generic.depth), color, 0);
        return rst;
    }
};

struct constrained_curve
{
    primitive_generic generic;

    float4x2 constrain_vector;
    range margin;
    range stroke;
    range offset_on_normal;

    float segments_inv;
    uint32_t cap1;

    float2 uv00, uv11;
    section<float4> color_src;
    section<float4> color_dst;

    [ForceInline]
    static float4 get_factor(float factor) {
        return float4(1, factor, factor * factor, factor * factor * factor);
    }

    [ForceInline]
    resolve_result push_vertex(resolve_param param) {
        let i = param.vtx_skip / 2;
        let scl = float(i) * segments_inv;
        let fac = fma(scl, 1 - margin.from - margin.to, margin.from);
        let is_odd = param.vtx_skip & 1u;

        let colorSrc = color_src[param.vtx_skip];
        let colorDst = color_dst[param.vtx_skip];
        let col = lerp(colorSrc, colorDst, scl);

        let facVec = get_factor(fac);
        let velFacVec = float4(0, 1, 2 * fac, 3 * facVec[2]);

        let pos = mul(facVec, constrain_vector);
        let normal = normalize(mul(velFacVec, constrain_vector)).rotate_rt_counter_clockwise();
        let s = lerp(stroke.from, stroke.to, scl);
        let off = lerp(offset_on_normal.from, offset_on_normal.to, scl);

        resolve_result rst;
        rst.pmt = Primitive(generic, param);
        rst.vtx = Vertex(float3(fma(normal, s * (bool(is_odd) ? -.5 : .5f) + off, pos), generic.depth), col, 0);
        return rst;
    }
};

static const uint instruction_align = 16;

[ForceInline]
public resolve_result push_vertex(
    instruction_head head,
    const in ByteAddressBuffer instruction,
    uint location,
    resolve_param param
) {
    switch (head.type)
    {
    case instruction_type::poly:
        return instruction.LoadAligned<poly_draw>(location, instruction_align).push_vertex(param);
    case instruction_type::poly_partial:
        return instruction.LoadAligned<poly_partial_draw>(location, instruction_align).push_vertex(param);
    case instruction_type::constrained_curve:
        return instruction.LoadAligned<constrained_curve>(location, instruction_align).push_vertex(param);
    default:
        resolve_result empty_rst;
        empty_rst.vtx = Vertex();
        return empty_rst;
    }
}

}
