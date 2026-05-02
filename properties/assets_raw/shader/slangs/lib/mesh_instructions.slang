module mesh_instruction_lib;
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
    uniform_update,


    triangle,
    quad,
    rectangle,

    line_frag,
    line_segments,
    line_segments_closed,
    
    circle,
    partial_circle,
    constrained_curve,

    rect_ortho,
    rect_ortho_outline,
    row_patch,
    
    SIZE,
};

public struct draw_mode
{
    public uint32_t value;
};

struct quad_vert_color{
	float4 c00, c10, c01, c11;

    
    __subscript(uint index)-> float4 {
        get {
            switch (index) {
            case 0: return c00;
            case 1: return c10;
            case 2: return c01;
            case 3: return c11;
            default: return float4(float.maxValue);
            }
        }
        set {
            
        }
    }
};

public struct resolve_param {
    public uint vtx_idx;
    public uint vtx_skip;
    public uint prm_skip;

    [[ForceInline]]
    public bool generates_primitive() {
        return prm_skip != 0xFFFF;
    }

    [[ForceInline]]
    public uint get_primitive_index() {
        return prm_skip;
    }

    [[ForceInline]]
    public uint3 getTrivialIndicies() {
        return uint3(vtx_idx - 2, vtx_idx - 1, vtx_idx);
    }
}

struct image_index
{
    
    uint32_t index : TextureIndex;
    uint32_t _cap; //TODO use as sampler index
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

public struct resolve_result {
    public Vertex vtx;
    public Primitive pmt;
};

interface instr_resolver
{
    [ForceInline]
    resolve_result push_vertex(resolve_param param);
}

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

struct triangle_draw : instr_resolver
{
    primitive_generic generic;
    float2 p0, p1, p2;
    float2 uv0, uv1, uv2;
    float4 c0, c1, c2;

    [ForceInline]
    resolve_result push_vertex(resolve_param param)
    {
        resolve_result rst;
        rst.pmt = Primitive(generic, param);

        switch (param.vtx_skip) {
        case 0: rst.vtx = Vertex(float3(p0, generic.depth), c0, uv0); break;
        case 1: rst.vtx = Vertex(float3(p1, generic.depth), c1, uv1); break;
        case 2: rst.vtx = Vertex(float3(p2, generic.depth), c2, uv2); break;
        default: rst.vtx = Vertex(); break;
        }

        return rst;
    }
};

struct quad_draw : instr_resolver
{
    primitive_generic generic;
    float2 vts[4];
    float2 uv[4];
    quad_vert_color vert_color;

    [ForceInline]
    resolve_result push_vertex(resolve_param param)
    {
        resolve_result rst;
        rst.pmt = Primitive(generic, param);
        rst.vtx = Vertex(float3(vts[param.vtx_skip], generic.depth), vert_color[param.vtx_skip], uv[param.vtx_skip]);
        return rst;
    }
};

struct rectangle_draw : instr_resolver
{
    primitive_generic generic;
    float2 center;
    float angle;
    float scale;
    quad_vert_color vert_color;
    float2 extent;
    float2 uv00, uv11;

    [ForceInline]
    resolve_result push_vertex(resolve_param param)
    {
        let idx = param.vtx_skip;
        let rotX = get_dir_vec2(angle) * scale * .5f;
        let rotY = rotate_rt_counter_clockwise(rotX);

        resolve_result rst;
        rst.pmt = Primitive(generic, param);

        switch (idx) {
        case 0: rst.vtx = Vertex(float3(center - extent.x * rotX - extent.y * rotY, generic.depth), vert_color.c00, uv00); break;
        case 1: rst.vtx = Vertex(float3(center + extent.x * rotX - extent.y * rotY, generic.depth), vert_color.c10, { uv11.x, uv00.y }); break;
        case 2: rst.vtx = Vertex(float3(center - extent.x * rotX + extent.y * rotY, generic.depth), vert_color.c01, { uv00.x, uv11.y }); break;
        case 3: rst.vtx = Vertex(float3(center + extent.x * rotX + extent.y * rotY, generic.depth), vert_color.c11, uv11); break;
        default: rst.vtx = Vertex(); break;
        }

        return rst;
    }
};

struct rectangle_ortho : instr_resolver {
    primitive_generic generic;
    float2 v00, v11;
    float2 uv00, uv11;
    quad_vert_color vert_color;
    float slant_factor_asc;
    float slant_factor_desc;
    float sdf_expand;

    [ForceInline]
    resolve_result push_vertex(resolve_param param) {
        resolve_result rst;
        rst.pmt = Primitive(generic, param);
        rst.pmt.sdf_expand = sdf_expand;

        // 使用 uint32_t 提取顶点的索引
        uint32_t skip = param.vtx_skip;

        // 拆解二进制位：
        // bit_x 控制左右：0 -> 左 (v00.x, uv00.x), 1 -> 右 (v11.x, uv11.x)
        // bit_y 控制上下：0 -> 上 (v00.y, uv00.y), 1 -> 下 (v11.y, uv11.y)
        let bit_x = bool(skip & 1);
        let bit_y = bool((skip >> 1) & 1);

        // 1. 无分支 UV 计算
        float2 uv = float2(
            bit_x ? uv11.x : uv00.x,
            bit_y ? uv11.y : uv00.y
        );

        // 2. 无分支颜色计算 (使用二叉树形式的条件选择)
        let color_y0 = bit_x ? vert_color.c10 : vert_color.c00;
        let color_y1 = bit_x ? vert_color.c11 : vert_color.c01;
        let color = bit_y ? color_y1 : color_y0;

        // 3. 无分支坐标与倾斜 (Slant) 计算
        float pos_x = bit_x ? v11.x : v00.x;
        float pos_y = bit_y ? v11.y : v00.y;

        // 根据 bit_y 选择当前行应用 asc 还是 desc 的倾斜因子
        float slant_x = bit_y ? -slant_factor_desc : slant_factor_asc;

        // 注意：在你原代码的 case 3 中写的是 `v11 - slant_factor_desc`。
        // 如果 `v11` 是 float2 并且减去一个标量，这会导致 Y 坐标也一同减去该标量。
        // 这里严格复原了你原代码的逻辑：仅在 skip == 3 时给 Y 也增加偏移。
        float slant_y = (skip == 3) ? -slant_factor_desc : 0.0f;

        float3 pos = float3(pos_x + slant_x, pos_y + slant_y, generic.depth);

        rst.vtx = Vertex(pos, color, uv);

        return rst;
    }
};

struct line_frag : instr_resolver {
    primitive_generic generic;
    float2 src, dst;
    float4 src_color;
    float4 dst_color;
    float stroke;
    float cap_length;

    uint32_t _cap1, cap2;

    [ForceInline]
    resolve_result push_vertex(resolve_param param) {
        let h_stroke = stroke * 0.5f;
        let dir = normalize(dst - src);
        let normal = float2(dir.y, -dir.x);

        let idx = param.vtx_skip;
        let is_dst_end = (idx & 2u) != 0u;
        let is_neg_normal = (idx & 1u) != 0u;

        let sign_dir = is_dst_end ? cap_length : -cap_length;
        let sign_norm = is_neg_normal ? -h_stroke : h_stroke;

        let base_pos = is_dst_end ? dst : src;
        let color = is_dst_end ? dst_color : src_color;
        let offset = dir * sign_dir + normal * sign_norm;

        resolve_result rst;
        rst.pmt = Primitive(generic, param);
        rst.vtx = Vertex(float3(base_pos + offset, generic.depth), color, 0);
        return rst;
    }
};

struct line_node {
    float2 pos;
    float stroke;
    float offset;
    section<float4> color;
};

struct line_draw
{
    primitive_generic generic;

    [ForceInline]
    static float2 safe_normalize(float2 v, float2 fallback) {
        float lenSq = dot(v, v);
        return (lenSq > 0.000001f) ? v * rsqrt(lenSq) : fallback;
    }

    [ForceInline]
    float2 calculate_vert_nor(float2 posL, float2 posC, float2 posR) {
        let dirL = posC - posL;
        let dirR = posR - posC;

        let nL = float2(-dirL.y, dirL.x);
        let nR = float2(-dirR.y, dirR.x);

        let nL_u = nL * rsqrt(max(dot(nL, nL), 0.00001f));
        let nR_u = nR * rsqrt(max(dot(nR, nR), 0.00001f));

        let miter_dir = nL_u + nR_u;
        let k = dot(miter_dir, nL_u);
        var inv_k = rcp(max(k, 0.0001f));

        return miter_dir * inv_k;
    }

    [ForceInline]
    static line_node line_node_at(uint index, const in ByteAddressBuffer instruction, const in uint payload_begin) {
        return instruction.LoadAligned<line_node>(payload_begin + index * sizeof(line_node), 16);
    }

    [ForceInline]
    resolve_result push_vertex(
        resolve_param param,
        const in instruction_head head,
        const in ByteAddressBuffer instruction,
        const in uint payload_begin
    ) {
        let node_length = (head.size - sizeof(line_draw)) / sizeof(line_node);
        let i = param.vtx_skip / 2 + 1;

        let nodeL = line_node_at(i - 1, instruction, payload_begin);
        let nodeC = line_node_at(i, instruction, payload_begin);
        let nodeR = line_node_at(i + 1, instruction, payload_begin);

        let vert_nor = calculate_vert_nor(nodeL.pos, nodeC.pos, nodeR.pos);
        let offset = fma(bool(param.vtx_skip & 1U) ? .5f : -.5f, nodeC.stroke, nodeC.offset);

        resolve_result rst;
        rst.pmt = Primitive(generic, param);
        rst.vtx = Vertex(float3(fma(vert_nor, offset, nodeC.pos), generic.depth), nodeC.color[param.vtx_skip], 0);
        return rst;
    }

    [ForceInline]
    resolve_result push_vertex_closed(
        resolve_param param,
        const in instruction_head head,
        const in ByteAddressBuffer instruction,
        const in uint payload_begin
    ) {
        let node_length = (head.size - sizeof(line_draw)) / sizeof(line_node);
        let mod = node_length;
        let i = param.vtx_skip / 2;
        let nodeL = line_node_at((i + mod - 1) % mod, instruction, payload_begin);
        let nodeC = line_node_at(i % mod, instruction, payload_begin);
        let nodeR = line_node_at((i + 1) % mod, instruction, payload_begin);

        let vert_nor = calculate_vert_nor(nodeL.pos, nodeC.pos, nodeR.pos);
        let offset = fma(bool(param.vtx_skip & 1U) ? .5f : -.5f, nodeC.stroke, nodeC.offset);

        resolve_result rst;
        rst.pmt = Primitive(generic, param);
        rst.vtx = Vertex(float3(fma(vert_nor, offset, nodeC.pos), generic.depth), nodeC.color[param.vtx_skip], 0);
        return rst;
    }
};

struct circle_fill_draw : instr_resolver
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
    private resolve_result draw_ring(resolve_param param) {
        let i = param.vtx_skip / 2;
        let angle = angle_at(i);
        let direction = get_dir_vec2(angle);
        let r = radius[param.vtx_skip];

        resolve_result rst;
        rst.pmt = Primitive(generic, param);
        rst.vtx = Vertex(float3(fma(direction, r, pos), generic.depth), bool(param.vtx_skip & 1u) ? outer : inner, 0);
        return rst;
    }

    [ForceInline]
    resolve_result push_vertex(resolve_param param)
    {
        return draw_ring(param);
    }
};

struct partial_circle_fill_draw : instr_resolver
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
    private resolve_result draw_ring(resolve_param param) {
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

    [ForceInline]
    resolve_result push_vertex(resolve_param param)
    {
        return draw_ring(param);
    }
};

struct constrained_curve : instr_resolver {
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
    resolve_result push_vertex(resolve_param param)
    {
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
}

struct rectangle_ortho_outline : instr_resolver {
    primitive_generic generic;
    float2[2] verts;
    float[4] strokes;
    quad_vert_color vert_color;

    [ForceInline]
    float2 at(uint idx) {
        return float2(verts[idx & 1].x, verts[(idx >> 1) & 1].y);
    }

    [ForceInline]
    float2 sign_at(uint idx) {
        return float2(bool(idx & 1) ? -1 : 1, bool(idx & 2) ? -1 : 1);
    }

    [ForceInline]
    resolve_result push_vertex(resolve_param param)
    {
        static const uint[5] idx = { 0, 1, 3, 2, 0 };
        let i = idx[param.vtx_skip / 2u];
        let pos = at(i);
        let sgn = sign_at(i);
        let off = select(bool(param.vtx_skip & 1), -.5, .5f) * strokes[i];

        resolve_result rst;
        rst.pmt = Primitive(generic, param);
        rst.vtx = Vertex(float3(fma(sgn, off, pos), generic.depth), vert_color[i], 0);
        return rst;
    }
};

[Flags]
enum row_patch_flags {
    transposed,
    flip_major_pos,
    flip_minor_pos,
    flip_major_uv,
    flip_minor_uv
};

struct row_patch : instr_resolver {

    primitive_generic generic;

    float[4] major_pos;
    float[2] minor_pos;
    float[2] minor_uv;
    float[4] major_uv;

    quad_vert_color color;
    row_patch_flags flags;
    float depth_bias;
    float2 skew;

    [[ForceInline]]
    float2 access_pos_at(uint2 coord) {
        let mask_x = select(bool(flags & row_patch_flags::flip_major_pos), 3u, 0u);
        let mask_y = select(bool(flags & row_patch_flags::flip_minor_pos), 1u, 0u);

        let fetch_coord = uint2(coord.x ^ mask_x, coord.y ^ mask_y);
        float2 base_pos = float2(major_pos[fetch_coord.x], minor_pos[fetch_coord.y]);

        base_pos.x += base_pos.y * skew.x;
        base_pos.y += base_pos.x * skew.y;
        return select(bool(flags & row_patch_flags::transposed), base_pos.yx, base_pos);
    }

    [[ForceInline]]
    float2 access_uv_at(uint2 coord) {
        let mask_x = select(bool(flags & row_patch_flags::flip_major_uv), 3u, 0u);
        let mask_y = select(bool(flags & row_patch_flags::flip_minor_uv), 1u, 0u);

        let fetch_coord = uint2(coord.x ^ mask_x, coord.y ^ mask_y);
        let base_uv = float2(major_uv[fetch_coord.x], minor_uv[fetch_coord.y]);

        return float2(major_uv[fetch_coord.x], minor_uv[fetch_coord.y]);
    }

    [[ForceInline]]
    resolve_result push_vertex(resolve_param param)
    {
        let cur_coord = uint2(param.vtx_skip / 2u, param.vtx_skip & 1u);
        let position = access_pos_at(cur_coord);
        let uv = access_uv_at(cur_coord);

        let mixMajor = (major_pos[cur_coord.x] - major_pos[0]) / (major_pos[3] - major_pos[0]);
        let colorMinor1 = select(bool(cur_coord.y), color.c01, color.c00);
        let colorMinor2 = select(bool(cur_coord.y), color.c11, color.c10);
        let c = lerp(colorMinor1, colorMinor2, mixMajor);

        resolve_result rst;
        rst.pmt = Primitive(generic, param);
        rst.vtx = Vertex(float3(position, generic.depth + depth_bias), c, uv);
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
    case instruction_type::triangle:
        return instruction.LoadAligned<triangle_draw>((location), instruction_align).push_vertex(param);
    case instruction_type::quad:
        return instruction.Load<quad_draw>((location)).push_vertex(param);
    case instruction_type::rectangle:
        return instruction.LoadAligned<rectangle_draw>((location), instruction_align).push_vertex(param);
    case instruction_type::line_frag:
        return instruction.LoadAligned<line_frag>((location), instruction_align).push_vertex(param);
    case instruction_type::line_segments:
        return instruction.LoadAligned<line_draw>((location), instruction_align).push_vertex(param, head, instruction, location + sizeof(line_draw));
    case instruction_type::line_segments_closed:
        return instruction.LoadAligned<line_draw>((location), instruction_align).push_vertex_closed(param, head, instruction, location + sizeof(line_draw));
    case instruction_type::circle:
        return instruction.LoadAligned<circle_fill_draw>((location), instruction_align).push_vertex(param);
    case instruction_type::partial_circle:
        return instruction.LoadAligned<partial_circle_fill_draw>((location), instruction_align).push_vertex(param);
    case instruction_type::constrained_curve:
        return instruction.LoadAligned<constrained_curve>((location), instruction_align).push_vertex(param);
    case instruction_type::rect_ortho:
        return instruction.LoadAligned<rectangle_ortho>((location), instruction_align).push_vertex(param);
    case instruction_type::rect_ortho_outline:
        return instruction.LoadAligned<rectangle_ortho_outline>((location), instruction_align).push_vertex(param);
    case instruction_type::row_patch:
        return instruction.LoadAligned<row_patch>((location), instruction_align).push_vertex(param);

    default:
        resolve_result empty_rst;
        empty_rst.vtx = Vertex();
        return empty_rst;
    }
}


}