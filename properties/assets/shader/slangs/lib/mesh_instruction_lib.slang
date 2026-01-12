module mesh_instruction_lib;
import general_lib;

extension<T> vector<T, 2> where T : IArithmetic {
    [ForceInline]
    vector<T, 2> rotate_rt_counter_clockwise() {
        return vector<T, 2>(this.y, -this.x);
    }
}

[ForceInline]
func cross<T>(vector<T, 2> l, vector<T, 2> r) -> T where T : IArithmetic {
    return l.x * r.y - l.y * r.x;
}

[ForceInline]
float angle_between(float2 from, float2 to) {
    return atan2(cross(from, to), dot(from, to));
}

[ForceInline]
func rotate_rt_counter_clockwise<T>(vector<T, 2> vec) -> vector<T, 2> where T : IArithmetic  {
    return vector<T, 2>( vec.y, -vec.x );
}


[ForceInline]
float2 get_dir_vec2(float rad) {
    float2 rst;
    sincos(rad, rst.y, rst.x);
    return rst;
}

public static const uint MAX_IMAGE_CACHE_COUNT = 4;

public static const uint GROUP_COUNT = 32;
public static const uint VERTEX_PER_MESH = 64;
public static const uint PRIMITIVES_PER_MESH = 126;

public enum instruction_type : uint32_t
{
    noop,
    uniform_update,


    triangle,
    quad,
    rectangle,

    line,
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

public struct resolve_param
{
    public uint vtx_idx;
    public uint vtx_skip;
    public uint vtx_patch_idx; // should only be 1, 2, or 0 as no patch

    public int prm_skip;

    [ForceInline]
    public uint get_primitive_index() {
        return prm_skip + vtx_skip - 2;
    }

    [[ForceInline]]
    bool isPatch() {
        return vtx_patch_idx > 0;
    }

    [[ForceInline]]
    uint3 getTrivialIndicies() {
        return uint3(vtx_idx - 2, vtx_idx - 1, vtx_idx);
    }
}

public struct primitive_generic
{
    nointerpolation public image_index image;
    nointerpolation public draw_mode mode;
    nointerpolation public float depth;
};

public struct Vertex
{
    public float4 position : SV_Position;
    public float4 color : VtxSclColor;
    centroid public float2 uv : VtxUV;

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
    public uint texture_index : PmtTexID;
    public draw_mode mode : PmtMode;

    [ForceInline]
    __init(primitive_generic generic, resolve_param param) {
        this.texture_index = uint(generic.image.index);
        this.mode = generic.mode;
    }
};

interface instr_resolver
{
    [ForceInline]
    Vertex push_vertex(
        resolve_param param,
        OutputPrimitives<Primitive, PRIMITIVES_PER_MESH> primits: __ref,
        OutputIndices<vector<uint, 3>, PRIMITIVES_PER_MESH> indices: __ref, );
}

public struct instruction_head
{
    public instruction_type type;
    public uint32_t size;
    public uint32_t vertex_count; //Or timeline index
    public uint32_t primit_count;
};

public struct mesh_dispatch_info
{
    public uint instruction_offset; // offset in 8 Byte
    public uint vertex_offset;
    public uint primit_offset;
    public uint primitives;
};

public struct section<T>
    where T : IArithmetic
{
    public T from;
    public T to;

    __subscript(uint index) ->T {
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


public typealias range = section<float>;

public struct image_index
{
    public uint32_t index;
    public uint32_t _cap;
};

struct triangle_draw : instr_resolver
{
    primitive_generic generic;
    float2 p0, p1, p2;
    float2 uv0, uv1, uv2;
    float4 c0, c1, c2;

    [ForceInline]
    Vertex push_vertex(
        resolve_param param,

        OutputPrimitives<Primitive, PRIMITIVES_PER_MESH> primits,
        OutputIndices<vector<uint, 3>, PRIMITIVES_PER_MESH> indices, )
    {
        //A triangle never needs patch
        switch (param.vtx_skip) {
        case 0: 
            return Vertex(float3(p0, generic.depth), c0, uv0);

        case 1:
            primits[param.prm_skip] = Primitive(generic, param);
            return Vertex(float3(p1, generic.depth), c1, uv1);

        case 2:
            indices[param.prm_skip] = param.getTrivialIndicies();
            return Vertex(float3(p2, generic.depth), c2, uv2);

        default:
            return Vertex();
        }

    }
};

struct quad_draw : instr_resolver
{
    primitive_generic generic;
    float2 v00, v10, v01, v11;
    float2 uv00, uv10, uv01, uv11;

    quad_vert_color vert_color;

    [ForceInline]
    Vertex push_vertex(
        resolve_param param,

        OutputPrimitives<Primitive, PRIMITIVES_PER_MESH> primits,
        OutputIndices<vector<uint, 3>, PRIMITIVES_PER_MESH> indices, )
    {
        switch (param.vtx_skip) {
        case 0:
            primits[param.prm_skip] = Primitive(generic, param);
            return Vertex(float3(v00, generic.depth), vert_color.c00, uv00);
        case 1:
            primits[param.prm_skip + 1 - param.isPatch()] = Primitive(generic, param);
            return Vertex(float3(v10, generic.depth), vert_color.c10, uv10);
        case 2:
            indices[param.get_primitive_index()] = param.getTrivialIndicies();
            return Vertex(float3(v01, generic.depth), vert_color.c01, uv01);
        case 3:
            indices[param.get_primitive_index()] = param.getTrivialIndicies();
            return Vertex(float3(v11, generic.depth), vert_color.c11, uv11);
        default: return Vertex();
        }
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
    Vertex push_vertex(
        resolve_param param,

        OutputPrimitives<Primitive, PRIMITIVES_PER_MESH> primits,
        OutputIndices<vector<uint, 3>, PRIMITIVES_PER_MESH> indices, )
    {
        let isPatch = param.isPatch();
        let idx = param.vtx_skip;// + (isPatch ? param.vtx_patch_idx : 0);

        let rotX = get_dir_vec2(angle) * scale * .5f;
        let rotY = rotate_rt_counter_clockwise(rotX);

        switch (idx) {
        case 0:
            return Vertex(float3(center - extent.x * rotX - extent.y * rotY, generic.depth), vert_color.c00, uv00);

        case 1:
            return Vertex(float3(center + extent.x * rotX - extent.y * rotY, generic.depth), vert_color.c10, {uv11.x, uv00.y});

        case 2:
            if (!isPatch) {
                primits[param.prm_skip] = Primitive(generic, param);
                indices[param.prm_skip] = param.getTrivialIndicies();
            }
            return Vertex(float3(center - extent.x * rotX + extent.y * rotY, generic.depth), vert_color.c01, { uv00.x, uv11.y });

        case 3:
            primits[param.prm_skip + 1] = Primitive(generic, param);
            indices[param.prm_skip + 1] = param.getTrivialIndicies();
            return Vertex(float3(center + extent.x * rotX + extent.y * rotY, generic.depth), vert_color.c11, uv11);

        default:
            return Vertex();
        }
    }
};

struct rectangle_ortho : instr_resolver {
    primitive_generic generic;
    float2 v00, v11;
    float2 uv00, uv11;
    quad_vert_color vert_color;

    [ForceInline]
    Vertex push_vertex(
        resolve_param param,
        OutputPrimitives<Primitive, PRIMITIVES_PER_MESH> primits,
        OutputIndices<vector<uint, 3>, PRIMITIVES_PER_MESH> indices, 
    ) {
        const uint isPatch = param.isPatch();
        switch (param.vtx_skip) {
        case 0:
            primits[param.prm_skip] = Primitive(generic, param);
            return Vertex(float3(v00, generic.depth), vert_color.c00, uv00);
        case 1:
            primits[param.prm_skip + 1 - isPatch] = Primitive(generic, param);
            return Vertex(float3(float2(v11.x, v00.y), generic.depth), vert_color.c10, float2(uv11.x, uv00.y));
        case 2:
            indices[param.prm_skip - isPatch] = param.getTrivialIndicies();
            return Vertex(float3(float2(v00.x, v11.y), generic.depth), vert_color.c01, float2(uv00.x, uv11.y));
        case 3:
            indices[param.prm_skip + 1 - isPatch] = param.getTrivialIndicies();
            return Vertex(float3(v11, generic.depth), vert_color.c11, uv11);
        default: return Vertex();
        }
    }
}

struct line : instr_resolver{
    primitive_generic generic;
    float2 src, dst;
    float4 src_color;
    float4 dst_color;
    float stroke;

    uint32_t _cap1, cap2, cap3;

    [ForceInline]
    Vertex push_vertex(
        resolve_param param,
        OutputPrimitives<Primitive, PRIMITIVES_PER_MESH> primits,
        OutputIndices<vector<uint, 3>, PRIMITIVES_PER_MESH> indices, 
    ) {
        let h_stroke = stroke / 2.0f;
        let diff = normalize(dst - src);

        switch (param.vtx_skip) {
        case 0:
            primits[param.prm_skip] = Primitive(generic, param);
            return Vertex(float3(fma(float2(-diff.x + diff.y, -diff.y - diff.x), h_stroke, src), generic.depth), src_color, 0);
            
        case 1:
            primits[param.prm_skip + 1 - param.isPatch()] = Primitive(generic, param);
            return Vertex(float3(fma(float2(-diff.x - diff.y, -diff.y + diff.x), h_stroke, src), generic.depth), src_color, 0);
        case 2:
            indices[param.get_primitive_index()] = param.getTrivialIndicies();
            return Vertex(float3(fma(float2(+diff.x + diff.y, +diff.y - diff.x), h_stroke, dst), generic.depth), dst_color, 0);
        case 3:
            indices[param.get_primitive_index()] = uint3(param.vtx_idx - 1, param.vtx_idx - 2, param.vtx_idx);
            return Vertex(float3(fma(float2(+diff.x - diff.y, +diff.y + diff.x), h_stroke, dst), generic.depth), dst_color, 0);
        default: return Vertex();
        }
    }
};

struct line_node {
    float2 pos;
    float stroke;
    float offset; // TODO ?
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
        // 1. 获取方向向量
        let dirL = posC - posL;
        let dirR = posR - posC;

        // 2. 计算垂直法向 (旋转 90 度)
        // 如果是端点，dirL 或 dirR 至少有一个为 0
        let nL = float2(-dirL.y, dirL.x);
        let nR = float2(-dirR.y, dirR.x);

        // 3. 归一化 (加上极小值防止除以 0)
        // 即使在端点，只要线段长度不为 0，n_sum 归一化后就是正确的端点法向
        let nL_u = nL * rsqrt(max(dot(nL, nL), 0.00001f));
        let nR_u = nR * rsqrt(max(dot(nR, nR), 0.00001f));

        // 4. Miter Join 核心逻辑
        let miter_dir = nL_u + nR_u;

        // 计算投影长度的倒数: 1 / cos(theta)
        // 使用 max 防止夹角为 180 度时除以 0
        let k = dot(miter_dir, nL_u);
        var inv_k = rcp(max(k, 0.0001f));

        return miter_dir * inv_k;
    }

    [ForceInline]
    static line_node line_node_at(uint index, const in ByteAddressBuffer instruction, const in uint payload_begin) {
        return instruction.LoadAligned<line_node>(payload_begin + index * sizeof(line_node), 16);
    }

    [ForceInline]
    Vertex push_vertex(
        resolve_param param,

        OutputPrimitives<Primitive, PRIMITIVES_PER_MESH> primits,
        OutputIndices<vector<uint, 3>, PRIMITIVES_PER_MESH> indices,

        const in instruction_head head,
        const in ByteAddressBuffer instruction,
        const in uint payload_begin,
    ) {
        let node_length = (head.size - sizeof(line_draw)) / sizeof(line_node);
        
        let i = param.vtx_skip / 2 + 1;

        let nodeL = line_node_at(i - 1, instruction, payload_begin);
        let nodeC = line_node_at(i, instruction, payload_begin);
        let nodeR = line_node_at(i + 1, instruction, payload_begin);

        let vert_nor = calculate_vert_nor(nodeL.pos, nodeC.pos, nodeR.pos);

        if (param.vtx_skip > 1) {
            let idc_idx = param.get_primitive_index();
            primits[idc_idx] = Primitive(generic, param);
            indices[idc_idx] = param.getTrivialIndicies();
        }

        let offset = fma(bool(param.vtx_skip & 1U) ? .5f : -.5f, nodeC.stroke, nodeC.offset);

        return Vertex(float3(fma(vert_nor, offset, nodeC.pos), generic.depth), nodeC.color[param.vtx_skip], 0);
    }

    [ForceInline]
    Vertex push_vertex_closed(
        resolve_param param,

        OutputPrimitives<Primitive, PRIMITIVES_PER_MESH> primits,
        OutputIndices<vector<uint, 3>, PRIMITIVES_PER_MESH> indices,

        const in instruction_head head,
        const in ByteAddressBuffer instruction,
        const in uint payload_begin,
    ) {
        let node_length = (head.size - sizeof(line_draw)) / sizeof(line_node);
        let mod = node_length;
        let i = param.vtx_skip / 2;

        let nodeL = line_node_at((i + mod - 1) % mod, instruction, payload_begin);
        let nodeC = line_node_at(i % mod, instruction, payload_begin);
        let nodeR = line_node_at((i + 1) % mod, instruction, payload_begin);
        let vert_nor = calculate_vert_nor(nodeL.pos, nodeC.pos, nodeR.pos);

        if (param.vtx_skip > 1) {
            let idc_idx = param.get_primitive_index();
            primits[idc_idx] = Primitive(generic, param);
            indices[idc_idx] = param.getTrivialIndicies();
        }

        let offset = fma(bool(param.vtx_skip & 1U) ? .5f : -.5f, nodeC.stroke, nodeC.offset);

        return Vertex(float3(fma(vert_nor, offset, nodeC.pos), generic.depth), nodeC.color[param.vtx_skip], 0);
    }
};


struct circle_fill_draw : instr_resolver
{
    primitive_generic generic;

    float2 pos;
    uint32_t segments;
    float initial_angle;

    uint32_t cap1;
    uint32_t cap2;
    range radius;

    float2 uv00, uv11;

    float4 inner, outer;
    
    [ForceInline]
    private float angle_at(uint index) {
        return fma(float(index) / float(segments), float.getPi() * 2, initial_angle);
    }

    [ForceInline]
    private Vertex draw_full(
        resolve_param param,
        OutputPrimitives<Primitive, PRIMITIVES_PER_MESH> primits,
        OutputIndices<vector<uint, 3>, PRIMITIVES_PER_MESH> indices, ) {
        let isPatch = param.isPatch();
        let idx = param.vtx_skip + (isPatch ? param.vtx_patch_idx : 0);

        if (param.vtx_skip == 0 || param.vtx_patch_idx == 2) {
            return Vertex(float3(pos, generic.depth), inner, 0);
        } else {
            const float angle = angle_at(param.vtx_skip);
            let direction = get_dir_vec2(angle);

            if (!isPatch && param.vtx_skip > 1) {
                let idc_idx = param.get_primitive_index();
                primits[idc_idx] = Primitive(generic, param);
                indices[idc_idx] = uint3(param.vtx_idx - param.vtx_skip - min(param.prm_skip, 0), param.vtx_idx - 1, param.vtx_idx);
            }

            return Vertex(float3(fma(direction, radius.to, pos), generic.depth), outer, 0);
        }
    }

    [ForceInline]
    private Vertex draw_ring(
        resolve_param param,
        OutputPrimitives<Primitive, PRIMITIVES_PER_MESH> primits,
        OutputIndices<vector<uint, 3>, PRIMITIVES_PER_MESH> indices, 
    ) {
        let i = param.vtx_skip / 2;
        let angle = angle_at(i);
        let direction = get_dir_vec2(angle);
        let r = radius[param.vtx_skip];

        if (param.vtx_skip > 1) {
            let idc_idx = param.get_primitive_index();
            primits[idc_idx] = Primitive(generic, param);
            indices[idc_idx] = param.getTrivialIndicies();
        }

        return Vertex(float3(fma(direction, r, pos), generic.depth), bool(param.vtx_skip & 1u) ? outer : inner, 0);
    }

    [ForceInline]
    Vertex push_vertex(
        resolve_param param,

        OutputPrimitives<Primitive, PRIMITIVES_PER_MESH> primits,
        OutputIndices<vector<uint, 3>, PRIMITIVES_PER_MESH> indices, )
    {
        return draw_ring(param, primits, indices);

        // [branch]
        // if (radius.from > 0) {
        //     return draw_ring(param, primits, indices);
        // } else {
        //     return draw_full(param, primits, indices);
        // }
        
    }
};

//TODO reuse the first one/two vertex
struct partial_circle_fill_draw : instr_resolver
{
    primitive_generic generic;
    float2 pos;
    uint32_t segments;
    uint32_t cap;

    range radius;
    float ratio_src;
    float ratio_ext;

    float2 uv00, uv11;
    section<float4> color_src;
    section<float4> color_dst;

    [ForceInline]
    private float angle_at(uint index) {
        return fma(float(index) / float(segments), ratio_ext, ratio_src) * float.getPi() * 2;
    }

    // [ForceInline]
    // private Vertex draw_full(
    //     resolve_param param,
    //     OutputPrimitives<Primitive, PRIMITIVES_PER_MESH> primits,
    //     OutputIndices<vector<uint, 3>, PRIMITIVES_PER_MESH> indices, ) {
    //     let isPatch = param.isPatch();
    //     let idx = param.vtx_skip + (isPatch ? param.vtx_patch_idx : 0);

    //     if (param.vtx_skip == 0 || param.vtx_patch_idx == 2) {
    //         return Vertex(float3(pos, generic.depth), inner, 0);
    //     } else {
    //         const float angle = angle_at(param.vtx_skip);
    //         let direction = get_dir_vec2(angle);

    //         if (!isPatch && param.vtx_skip > 1) {
    //             let idc_idx = param.get_primitive_index();
    //             primits[idc_idx] = Primitive(generic, param);
    //             indices[idc_idx] = uint3(param.vtx_idx - param.vtx_skip - min(param.prm_skip, 0), param.vtx_idx - 1, param.vtx_idx);
    //         }

    //         return Vertex(float3(fma(direction, radius.to, pos), generic.depth), outer, 0);
    //     }
    // }

    [ForceInline]
    private Vertex draw_ring(
        resolve_param param,
        OutputPrimitives<Primitive, PRIMITIVES_PER_MESH> primits,
        OutputIndices<vector<uint, 3>, PRIMITIVES_PER_MESH> indices, 
    ) {
        let i = param.vtx_skip / 2;
        let angle = angle_at(i);
        let direction = get_dir_vec2(angle);
        let r = radius[param.vtx_skip];

        let colorSrc = color_src[param.vtx_skip];
        let colorDst = color_dst[param.vtx_skip];
        let color = lerp(colorSrc, colorDst, float(i) / float(segments));

        if (param.vtx_skip > 1) {
            let idc_idx = param.get_primitive_index();
            primits[idc_idx] = Primitive(generic, param);
            indices[idc_idx] = param.getTrivialIndicies();
        }

        return Vertex(float3(fma(direction, r, pos), generic.depth), color, 0);
    }

    [ForceInline]
    Vertex push_vertex(
        resolve_param param,

        OutputPrimitives<Primitive, PRIMITIVES_PER_MESH> primits,
        OutputIndices<vector<uint, 3>, PRIMITIVES_PER_MESH> indices, )
    {
        return draw_ring(param, primits, indices);
        // [branch]
        // if (radius.from > 0) {
        //     return draw_ring(param, primits, indices);
        // } else {
        //     return draw_full(param, primits, indices);
        // }
    }
};

struct constrained_curve : instr_resolver{
    primitive_generic generic;

    float4x2 constrain_vector;
    // float4x2 constrain_vector_derivative;

    range margin;
    range stroke;
    range offset_on_normal;

    uint32_t segments;
    uint32_t cap1;

    float2 uv00, uv11;
    section<float4> color_src;
    section<float4> color_dst;

    [ForceInline]
    static float4 get_factor(float factor) {
        return float4(1, factor, factor * factor, factor * factor * factor);
    }

    [ForceInline]
    Vertex push_vertex(
        resolve_param param,

        OutputPrimitives<Primitive, PRIMITIVES_PER_MESH> primits,
        OutputIndices<vector<uint, 3>, PRIMITIVES_PER_MESH> indices, )
    {
        let i = param.vtx_skip / 2;
        let scl = float(i) / float(segments);
        let fac = fma(scl, 1 - margin.from - margin.to,  margin.from);

        let is_odd = param.vtx_skip & 1u;

        let colorSrc = color_src[param.vtx_skip];
        let colorDst = color_dst[param.vtx_skip];
        let col = lerp(colorSrc, colorDst, scl);

        let facVec = get_factor(fac);
        let velFacVec = float4(0, 1, 2 * fac, 3 * facVec[2]);
 
        let pos = mul(facVec, constrain_vector);

        let normal = normalize(mul(velFacVec, constrain_vector)).rotate_rt_counter_clockwise();
        

        if (param.vtx_skip > 1) {
            let idc_idx = param.get_primitive_index();
            primits[idc_idx] = Primitive(generic, param);
            indices[idc_idx] = param.getTrivialIndicies();
        }

        let s = lerp(stroke.from, stroke.to, scl);
        let off = lerp(offset_on_normal.from, offset_on_normal.to, scl);

        return Vertex(float3(fma(normal, s * (bool(is_odd) ? -.5 : .5f) + off, pos), generic.depth), col, 0);
    }
}

struct rectangle_ortho_outline : instr_resolver{
    primitive_generic generic;
    float2[2] verts; // v00, v11
    float[4] strokes;
    quad_vert_color vert_color;

    [ForceInline]
    float2 at(uint idx) {
        return float2(verts[idx & 1].x, verts[bool(idx & 2)].y);
    }

    [ForceInline]
    float2 sign_at(uint idx) {
        return float2(bool(idx & 1) ? -1 : 1, bool(idx & 2) ? -1 : 1);
    }

    [ForceInline]
    Vertex push_vertex(
        resolve_param param,
        OutputPrimitives<Primitive, PRIMITIVES_PER_MESH> primits,
        OutputIndices<vector<uint, 3>, PRIMITIVES_PER_MESH> indices)
    {
        static const uint[5] idx = { 0, 1, 3, 2, 0 };
        let i = idx[param.vtx_skip / 2u];
        let pos = at(i);
        let sgn = sign_at(i);
        let off = select(bool(param.vtx_skip & 1), -.5, .5f) * strokes[i];

        if (param.vtx_skip > 1) {
            let idc_idx = param.get_primitive_index();
            primits[idc_idx] = Primitive(generic, param);
            indices[idc_idx] = param.getTrivialIndicies();
        }

        return Vertex(float3(fma(sgn, off, pos), generic.depth), vert_color[i], 0);
    }
};

struct row_patch : instr_resolver{
    primitive_generic generic;

    float[4] x;
    float[2] y;
    float[2] uv_y;
    float[4] uv_x;

    quad_vert_color color;

    [ForceInline]
    float2 access_pos_at(uint2 coord) {
        return float2(x[coord.x], y[coord.y]);
    }

    [ForceInline]
    float2 access_uv_at(uint2 coord) {
        return float2(uv_x[coord.x], uv_y[coord.y]);
    }

    [ForceInline]
    Vertex push_vertex(
        resolve_param param,

        OutputPrimitives<Primitive, PRIMITIVES_PER_MESH> primits,
        OutputIndices<vector<uint, 3>, PRIMITIVES_PER_MESH> indices, )
    {
        let cur_coord = uint2(param.vtx_skip / 2u, param.vtx_skip & 1u);

        let pos = access_pos_at(cur_coord);
        let uv = access_uv_at(cur_coord);

        let mixX = (pos.x - x[0]) / (x[3] - x[0]);
        // let colorY1 = bool(cur_coord.y) ? color.c01 : color.c00;
        // let colorY2 = bool(cur_coord.y) ? color.c11 : color.c10;
        let colorY1 = select(bool(cur_coord.y), color.c01, color.c00);
        let colorY2 = select(bool(cur_coord.y), color.c11, color.c10);
        let c = lerp(colorY1, colorY2, mixX);
        
        if (param.vtx_skip > 1) {
            let idc_idx = param.get_primitive_index();
            primits[idc_idx] = Primitive(generic, param);
            indices[idc_idx] = param.getTrivialIndicies();
        }

        return Vertex(float3(pos, generic.depth), c, uv);
    }
};

static const uint instruction_align = 16;
// static const uint instruction_shift = 4;

[ForceInline]
[[deprecated("")]]
public instruction_head get_instruction_head(const in ByteAddressBuffer instruction, uint location){
    return instruction.LoadAligned<instruction_head>(location, instruction_align);
}

[ForceInline]
public Vertex push_vertex(
    instruction_head head,
    const in ByteAddressBuffer instruction,
    uint location,
    resolve_param param,
    OutputPrimitives<Primitive, PRIMITIVES_PER_MESH> primits: __ref,
    OutputIndices<vector<uint, 3>, PRIMITIVES_PER_MESH> indices: __ref
) {
    switch (head.type)
    {
    case instruction_type::triangle:
        return instruction.LoadAligned<triangle_draw>((location), instruction_align).push_vertex(param, primits, indices);

    case instruction_type::quad:
        return instruction.LoadAligned<quad_draw>((location), instruction_align).push_vertex(param, primits, indices);

    case instruction_type::rectangle:
        return instruction.LoadAligned<rectangle_draw>((location), instruction_align).push_vertex(param, primits, indices);

    case instruction_type::line:
        return instruction.LoadAligned<line>((location), instruction_align).push_vertex(param, primits, indices);

    case instruction_type::line_segments:
        return instruction.LoadAligned<line_draw>((location), instruction_align).push_vertex(param, primits, indices, head, instruction, location + sizeof(line_draw));

    case instruction_type::line_segments_closed:
        return instruction.LoadAligned<line_draw>((location), instruction_align).push_vertex_closed(param, primits, indices, head, instruction, location + sizeof(line_draw));

    case instruction_type::circle:
        return instruction.LoadAligned<circle_fill_draw>((location), instruction_align).push_vertex(param, primits, indices);

    case instruction_type::partial_circle:
        return instruction.LoadAligned<partial_circle_fill_draw>((location), instruction_align).push_vertex(param, primits, indices);

    case instruction_type::constrained_curve:
        return instruction.LoadAligned<constrained_curve>((location), instruction_align).push_vertex(param, primits, indices);

    case instruction_type::rect_ortho:
        return instruction.LoadAligned<rectangle_ortho>((location), instruction_align).push_vertex(param, primits, indices);

    case instruction_type::rect_ortho_outline:
        return instruction.LoadAligned<rectangle_ortho_outline>((location), instruction_align).push_vertex(param, primits, indices);

    case instruction_type::row_patch:
        return instruction.LoadAligned<row_patch>((location), instruction_align).push_vertex(param, primits, indices);


    default:
        return Vertex();
    }
}

public struct subrange_info {
    public uint override_index;
    public uint cap1;
    public uint cap2;
    public uint cap3;
    public mesh_dispatch_info[GROUP_COUNT] ranges;
}

public interface vertex_transformer<T> {
    [ForceInline]
    T operator()(Vertex v);
}

[ForceInline]
public uint resolve_instruction<T>(
    const uint groupId,
    const uint threadId,
    const in subrange_info mesh_subrange,
    const in ByteAddressBuffer instruction,
    const in vertex_transformer<T> transformer,

    OutputVertices<T, VERTEX_PER_MESH> verts: __ref,
    OutputPrimitives<Primitive, PRIMITIVES_PER_MESH> prims: __ref,
    OutputIndices<vector<uint, 3>, PRIMITIVES_PER_MESH> indices: __ref
) {
    let group_head = mesh_subrange.ranges[groupId];
    let patch_til_index = bool(group_head.vertex_offset) * 2;
    let group_instr_head_ptr = group_head.instruction_offset;

    uint skipped_vertices = 0;
    int skipped_primitives = -int(group_head.primit_offset);
    var ptr_to_head = group_instr_head_ptr;

    [ForceUnroll]
    for (int i = 0; i < VERTEX_PER_MESH / GROUP_COUNT; ++i) {
        let target_index = (i == 1 ? (VERTEX_PER_MESH - threadId - 1) : threadId);
        let thread_vtx_skip = target_index + group_head.vertex_offset;

        while (true) {
            let head = get_instruction_head(instruction, ptr_to_head);

            if (skipped_primitives >= int(group_head.primitives)) break;
            if (head.vertex_count == 0) break;

            let local_skip = thread_vtx_skip - skipped_vertices;
            if (local_skip < head.vertex_count) {

                let patch_index = patch_til_index > target_index ? patch_til_index - target_index : 0;
                var vtx = push_vertex(
                    head, instruction, ptr_to_head + sizeof(instruction_head),
                    resolve_param(target_index, local_skip, patch_index, skipped_primitives),
                    prims, indices);

                verts[target_index] = transformer(vtx);
                break;
            }

            ptr_to_head += head.size;
            skipped_vertices += head.vertex_count;
            skipped_primitives += head.primit_count;
        }
    }

    

    return group_head.primitives;
}

public interface vertex_transformer_2<T> {
    [ForceInline]
    T operator()(Vertex v);

    [[ForceInline]]
    [[mutating]]
    void operator()(uint march_index);

}

public struct mesh_dispatch_info_v2<uint n>
{
    public uint instruction_head_index;
    public uint instruction_offset;
    public uint vertex_offset;
    public uint primit_offset;
    public uint primitives;

    public uint[n] timeline;
}

[ForceInline]
public uint resolve_instruction_v2<T, uint n, L:IBufferDataLayout>(
    const uint threadId,
    const in mesh_dispatch_info_v2<n> mesh_subrange,

    const in StructuredBuffer<instruction_head, L> instruction_heads,
    const in ByteAddressBuffer instruction,
    in vertex_transformer_2<T> transformer,

    OutputVertices<T, VERTEX_PER_MESH> verts: __ref,
    OutputPrimitives<Primitive, PRIMITIVES_PER_MESH> prims: __ref,
    OutputIndices<vector<uint, 3>, PRIMITIVES_PER_MESH> indices: __ref
) {
    let patch_til_index = bool(mesh_subrange.vertex_offset) * 2;

    uint skipped_vertices = 0;
    int skipped_primitives = -int(mesh_subrange.primit_offset);
    var ptr_to_payload = mesh_subrange.instruction_offset;
    var idx_to_head = mesh_subrange.instruction_head_index;


    [ForceUnroll]
    outer: for (int i = 0; i < VERTEX_PER_MESH / GROUP_COUNT; ++i) {
        let target_index = select(i == 1, (VERTEX_PER_MESH - threadId - 1), threadId);
        let thread_vtx_skip = target_index + mesh_subrange.vertex_offset;

        while (true) {
            let head = instruction_heads[idx_to_head];

            if (head.type == instruction_type::uniform_update) {
                transformer(head.vertex_count);
                ++idx_to_head;
                continue;
            }

            if (skipped_primitives >= int(mesh_subrange.primitives)) break outer;

            let local_skip = thread_vtx_skip - skipped_vertices;
            if (local_skip < head.vertex_count) {

                let patch_index = patch_til_index > target_index ? patch_til_index - target_index : 0;
                var vtx = push_vertex(
                    head, instruction, ptr_to_payload,
                    resolve_param(target_index, local_skip, patch_index, skipped_primitives),
                    prims, indices);

                verts[target_index] = transformer(vtx);
                break;
            }

            ptr_to_payload += head.size;
            skipped_vertices += head.vertex_count;
            skipped_primitives += head.primit_count;
            ++idx_to_head;
        }
    }

    return mesh_subrange.primitives;
}
