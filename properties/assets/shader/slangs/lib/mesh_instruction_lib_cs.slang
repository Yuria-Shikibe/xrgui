module mesh_instruction_lib_cs;
__exported import mesh_instructions;
import general_lib;

namespace mesh_instr {

public struct VS_Vertex
{
    public float3 position = {};
    public uint timeline_index = {};
    public float4 color = {};
    public float2 uv = {};

    public __init(Vertex vtx) {
        position = vtx.position.xyz;
        color = vtx.color;
        uv = vtx.uv;
    }

    public __init() {}
};

public struct dispatch_config {
    public uint group_offset;
    public uint global_primitive_offset;
    uint[2] _cap;
}

public struct vertex_resolve_info {
    public uint packed_type_timeline;
    public uint payload_offset;
    public uint packed_skips;
    public uint packed_dispatch_size; // [修改] 接受 C++ 传来的打包数据

    [ForceInline] public instruction_type get_type() { return (instruction_type)(packed_type_timeline & 0xFFFF); }
    [ForceInline] public uint get_relative_timeline() { return packed_type_timeline >> 16; }
    [ForceInline] public uint get_vtx_skip() { return packed_skips & 0xFFFF; }
    [ForceInline] public uint get_prm_skip() { return packed_skips >> 16; }
    [ForceInline] public uint get_dispatch_group() { return packed_dispatch_size & 0xFFFF; }
    [ForceInline] public uint get_payload_size() { return packed_dispatch_size >> 16; }
};

public struct mesh_dispatch_info_v3 {
    public uint global_vertex_offset;
    public uint primitives;
    public uint base_timeline_index;
    public uint global_primitive_offset; // 替换了原来的 _cap
};

public struct mesh_instruction_data<uint TimelineSz> {
    StructuredBuffer<mesh_dispatch_info_v3> dispatch_info;
    StructuredBuffer<vertex_resolve_info> resolve_infos;
    StructuredBuffer<uint[TimelineSz]> timelines;
    ByteAddressBuffer instruction;
};

// 1. 将所有资源封装到一个结构体中
public struct BuilderResources
{
    // 输入 Buffer
    StructuredBuffer<mesh_instr::mesh_dispatch_info_v3> dispatch_info;
    StructuredBuffer<mesh_instr::vertex_resolve_info> resolve_infos;
    ByteAddressBuffer instruction_buffer;

    // 输出到纯 GPU Buffer
    RWStructuredBuffer<mesh_instr::VS_Vertex, CDataLayout> out_vbo;
    RWStructuredBuffer<uint> out_ibo;
    RWStructuredBuffer<mesh_instr::Primitive> out_primitive_data;
};

// 3. 提取出的独立函数：接收全局索引和封装好的资源结构体
public void process_mesh_instruction(uint target_index, BuilderResources res)
{

    // 获取当前顶点的解析配置
    mesh_instr::vertex_resolve_info info = res.resolve_infos[target_index];
    mesh_instr::instruction_type type = info.get_type();

    if (type == mesh_instr::instruction_type::noop) return;

    mesh_instr::mesh_dispatch_info_v3 dispatchinfo = res.dispatch_info[info.get_dispatch_group()];

    // --- 1. 核心数据整合 ---

    // 计算将在 VS 中使用的绝对 Timeline 索引
    uint absolute_timeline_idx = dispatchinfo.base_timeline_index + info.get_relative_timeline();

    // 组装解析参数
    mesh_instr::resolve_param param;
    param.vtx_idx = target_index; // 【重要】把全局顶点索引传进去，确保 getTrivialIndicies 生成正确的全局 Index
    param.vtx_skip = info.get_vtx_skip();
    param.prm_skip = info.get_prm_skip();

    // 组装供 push_vertex 消费的假想指令头
    mesh_instr::instruction_head dummy_head;
    dummy_head.type = type;
    dummy_head.size = info.get_payload_size();

    // --- 2. 几何与图元解析 ---
    mesh_instr::resolve_result rst = mesh_instr::push_vertex(
        dummy_head,
        res.instruction_buffer,
        info.payload_offset,
        param
    );

    // --- 3. 写入 Vertex Buffer ---
    var out_v = mesh_instr::VS_Vertex(rst.vtx);
    out_v.timeline_index = absolute_timeline_idx; // 打包装箱给 VS

    res.out_vbo[target_index] = out_v;

    // --- 4. 写入 Index Buffer 和 Primitive Data Buffer ---
    // 只有负责完结图元的特定线程才进行此处写入
    if (param.generates_primitive()) {

        // 计算当前图元在全局 SSBO 中的绝对偏移
        uint global_prm_idx = dispatchinfo.global_primitive_offset + param.get_primitive_index();

        // IBO: 获取局部拓扑（此时已是全局索引）并直接摊平写入
        uint3 indices = param.getTrivialIndicies();
        res.out_ibo[global_prm_idx * 3 + 0] = indices.x;
        res.out_ibo[global_prm_idx * 3 + 1] = indices.y;
        res.out_ibo[global_prm_idx * 3 + 2] = indices.z;

        // 图元元数据: 供 PS 通过 gl_PrimitiveID / SV_PrimitiveID 提取
        res.out_primitive_data[global_prm_idx] = rst.pmt;
    }
}

}