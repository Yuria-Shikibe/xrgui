#version 460 core
#pragma shader_stage(mesh)
#extension GL_EXT_mesh_shader: require
#extension GL_EXT_descriptor_heap: require
#extension GL_EXT_nonuniform_qualifier: require

struct dispatch_config_0
{
    uint group_offset_0;
    uint _cap_0[3];
};



layout (descriptor_heap) readonly buffer StructuredBuffer_dispatch_config_t_0 {
    dispatch_config_0 _data[];
} _slang_resource_heap_11[];


struct dbh_input_0
{
    uint dispatch_buffer_start_index_0;
    uint dispatch_config_index_0;
    uint ui_state_index_0;
    uint slide_line_style_index_0;
};



layout (push_constant)
layout (std430) uniform block_dbh_input_0
{
    uint dispatch_buffer_start_index_0;
    uint dispatch_config_index_0;
    uint ui_state_index_0;
    uint slide_line_style_index_0;
} dbh_input_handles_0;


out gl_MeshPerVertexEXT
{
    vec4 gl_Position;
    float gl_ClipDistance[4];
} gl_MeshVerticesEXT[64U];




noperspective layout (location = 0)
out vec4 verts_vtx_color_0[64U];



centroid layout (location = 1)
out vec2 verts_vtx_uv_0[64U];



flat perprimitiveEXT layout (location = 2)
out uint prims_texture_index_0[126U];



flat perprimitiveEXT layout (location = 3)
out uint prims_mode_value_0[126U];



out uvec3 gl_PrimitiveTriangleIndicesEXT[126U];



layout (local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
layout (max_primitives = 126) out;
layout (max_primitives = 126) out;
layout (max_vertices = 64) out;
layout (triangles) out;
void main()
{
    if (gl_LocalInvocationIndex == 0) {
        vec4 color = dbh_input_handles_0.dispatch_buffer_start_index_0 == 264 ? vec4(0, 1, 0, 0.25f) : vec4(1, 0, 0, 0.25f);

        // 1. 获取并限制索引值，防止越界导致着色器崩溃
        uint config_idx = dbh_input_handles_0.dispatch_config_index_0 < 8 ? dbh_input_handles_0.dispatch_config_index_0 : 10;

        // 2. 决定要画多少个三角形 (如果为 0，我们画 1 个灰色的，否则值为几就画几个)
        uint active_primitives = (config_idx == 0) ? 1 : config_idx;
        uint active_vertices = active_primitives * 3;

        // 动态分配资源！这是 Mesh Shader 的核心优势
        SetMeshOutputsEXT(active_vertices, active_primitives);

        // 获取对应的调试颜色
        vec4 current_color = color;

        // 3. 循环生成小三角形排成一排
        for (uint i = 0; i < active_primitives; ++i) {
            uint v0 = i * 3 + 0;
            uint v1 = i * 3 + 1;
            uint v2 = i * 3 + 2;

            // 让三角形沿 X 轴排列开，每个间隔 0.15 左右
            float offset_x = -0.8 + float(i) * 0.18;
            float scale = 0.1; // 缩小单个三角形的尺寸

            // 写入顶点位置
            gl_MeshVerticesEXT[v0].gl_Position = vec4(offset_x         ,-.8f + active_primitives * .2f +scale, 0.0, 1.0);
            gl_MeshVerticesEXT[v1].gl_Position = vec4(offset_x - scale, -.8f + active_primitives * .2f -scale, 0.0, 1.0);
            gl_MeshVerticesEXT[v2].gl_Position = vec4(offset_x + scale, -.8f + active_primitives * .2f -scale, 0.0, 1.0);

            // 写入你自定义的颜色和 UV 输出
            verts_vtx_color_0[v0] = current_color;
            verts_vtx_color_0[v1] = current_color;
            verts_vtx_color_0[v2] = current_color;

            verts_vtx_uv_0[v0] = vec2(0.0, 0.0107421875);
            verts_vtx_uv_0[v1] = vec2(0.0, 0.0);
            verts_vtx_uv_0[v2] = vec2(0.0134277344, 0.0);

            // 组装当前三角形的拓扑
            gl_PrimitiveTriangleIndicesEXT[i] = uvec3(v0, v1, v2);

            // 记录 perprimitive 纹理索引
            prims_texture_index_0[i] = 0U;
            //            prims_mode_value_0[i] = dbh_input_handles_0.dispatch_config_index_0;
            prims_mode_value_0[i] = _slang_resource_heap_11[dbh_input_handles_0.dispatch_buffer_start_index_0 + 5U]._data[dbh_input_handles_0.dispatch_config_index_0].group_offset_0;
        }
    }

}

