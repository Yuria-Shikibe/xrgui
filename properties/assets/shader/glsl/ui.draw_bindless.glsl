#version 460 core
#pragma shader_stage(mesh)
#extension GL_EXT_mesh_shader : require
#extension GL_EXT_descriptor_heap : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout(row_major) uniform;
layout(row_major) buffer;


struct ubo_screen_info_0
{
    mat3x3 screen_to_uniform_0;
};



layout(std140, descriptor_heap) readonly buffer StructuredBuffer_ubo_screen_info_t_0 {
    ubo_screen_info_0 _data[];
} _slang_resource_heap_0[];


struct scissor_0
{
    vec2 src_0;
    vec2 dst_0;
    float margin_0;
    uint _c1_0;
    uint _c2_0;
    uint _c3_0;
};



struct ubo_layer_info_0
{
    mat3x3 element_to_screen_0;
    scissor_0 scissor_1;
};



layout(std140, descriptor_heap) readonly buffer StructuredBuffer_ubo_layer_info_t_0 {
    ubo_layer_info_0 _data[];
} _slang_resource_heap_1[];


layout(std430, descriptor_heap) readonly buffer StructuredBuffer_uint_t_0 {
    uint _data[];
} _slang_resource_heap_2[];


layout(std430, descriptor_heap) readonly buffer StructuredBuffer_float_t_0 {
    float _data[];
} _slang_resource_heap_3[];


layout(std430, descriptor_heap) readonly buffer StructuredBuffer_vectorx3Cfloatx2C2x3E_t_0 {
    vec2 _data[];
} _slang_resource_heap_4[];


layout(std430, descriptor_heap) readonly buffer StructuredBuffer_vectorx3Cfloatx2C4x3E_t_0 {
    vec4 _data[];
} _slang_resource_heap_5[];


layout(std430, descriptor_heap) readonly buffer StructuredBuffer_arrayx3Cvectorx3Cfloatx2C2x3Ex2C2x3E_t_0 {
    vec2  _data[][2];
} _slang_resource_heap_6[];


layout(std430, descriptor_heap) readonly buffer StructuredBuffer_arrayx3Cfloatx2C4x3E_t_0 {
    float  _data[][4];
} _slang_resource_heap_7[];


layout(std430, descriptor_heap) readonly buffer StructuredBuffer_arrayx3Cfloatx2C2x3E_t_0 {
    float  _data[][2];
} _slang_resource_heap_8[];


struct instruction_head_0
{
    uint type_0;
    uint size_0;
    uint vertex_count_0;
    uint primit_count_0;
};



layout(std140, descriptor_heap) readonly buffer StructuredBuffer_instruction_head_t_0 {
    instruction_head_0 _data[];
} _slang_resource_heap_9[];


struct mesh_dispatch_info_v2_0
{
    uint instruction_head_index_0;
    uint instruction_offset_0;
    uint vertex_offset_0;
    uint primit_offset_0;
    uint primitives_0;
    uint  timeline_0[2];
};



layout(std430, descriptor_heap) readonly buffer StructuredBuffer_mesh_dispatch_info_v2_t_0 {
    mesh_dispatch_info_v2_0 _data[];
} _slang_resource_heap_10[];


struct dispatch_config_0
{
    uint group_offset_0;
    uint  _cap_0[3];
};



layout(std140, descriptor_heap) readonly buffer StructuredBuffer_dispatch_config_t_0 {
    dispatch_config_0 _data[];
} _slang_resource_heap_11[];


struct dbh_input_0
{
    uint dispatch_buffer_start_index_0;
    uint dispatch_config_index_0;
    uint ui_state_index_0;
    uint slide_line_style_index_0;
};



layout(push_constant)
layout(std430) uniform block_dbh_input_0
{
    uint dispatch_buffer_start_index_0;
    uint dispatch_config_index_0;
    uint ui_state_index_0;
    uint slide_line_style_index_0;
}dbh_input_handles_0;


out gl_MeshPerVertexEXT
{
    vec4 gl_Position;
    float  gl_ClipDistance[4];
} gl_MeshVerticesEXT[64U];



const uint  idx_0[5] = { 0U, 1U, 3U, 2U, 0U };


dispatch_config_0 dbh_input_get_dispatch_config_0(dbh_input_0 this_0)
{


    return _slang_resource_heap_11[uvec2(this_0.dispatch_buffer_start_index_0 + 5U, 0U).x]._data[uint(this_0.dispatch_config_index_0)];
}



struct resolve_param_0
{
    uint vtx_idx_0;
    uint vtx_skip_0;
    uint vtx_patch_idx_0;
    int prm_skip_0;
};



resolve_param_0 resolve_param_x24init_0(uint vtx_idx_1, uint vtx_skip_1, uint vtx_patch_idx_1, int prm_skip_1)
{


    resolve_param_0 _S1;

    _S1.vtx_idx_0 = vtx_idx_1;
    _S1.vtx_skip_0 = vtx_skip_1;
    _S1.vtx_patch_idx_0 = vtx_patch_idx_1;

    _S1.prm_skip_0 = prm_skip_1;


    return _S1;
}



struct Vertex_0
{
    vec4 position_0;
    vec4 color_0;
    vec2 uv_0;
};




Vertex_0 Vertex_x24init_0(vec3 p_0, vec4 c_0, vec2 uv_1)
{


    Vertex_0 _S2;
    _S2.position_0 = vec4(p_0, 1.0);
    _S2.color_0 = c_0;
    _S2.uv_0 = uv_1;


    return _S2;
}




Vertex_0 Vertex_x24init_1()
{


    Vertex_0 _S3;
    const vec4 _S4 = vec4(0.0);


    _S3.position_0 = _S4;
    _S3.color_0 = _S4;
    _S3.uv_0 = vec2(0.0);


    return _S3;
}



struct quad_vert_color_0
{
    vec4 c00_0;
    vec4 c10_0;
    vec4 c01_0;
    vec4 c11_0;
};



vec4 quad_vert_color_operatorx5Bx5D_get_0(quad_vert_color_0 this_1, uint index_0)
{


    switch(index_0)
{
    case 0U:
        {


            return this_1.c00_0;
        }
    case 1U:
        {


            return this_1.c10_0;
        }
    case 2U:
        {


            return this_1.c01_0;
        }
    case 3U:
        {


            return this_1.c11_0;
        }
    default:
        {


            return vec4(3.4028234663852886e+38);
        }
}


}



float rcp_0(float x_0)
{


    return 1.0 / x_0;
}



struct section_0
{
    vec4 from_0;
    vec4 to_0;
};


vec4 section_operatorx5Bx5D_get_0(section_0 this_2, uint index_1)
{


    vec4 _S5;
    if(bool(index_1 & 1U))
    {


        _S5 = this_2.to_0;


    }
    else
    {


        _S5 = this_2.from_0;


    }


    return _S5;
}



struct section_1
{
    float from_0;
    float to_0;
};


float section_operatorx5Bx5D_get_1(section_1 this_3, uint index_2)
{


    float _S6;
    if(bool(index_2 & 1U))
    {


        _S6 = this_3.to_0;


    }
    else
    {


        _S6 = this_3.from_0;


    }


    return _S6;
}



float float_getPi_0()
{


    return 3.14159274101257324;
}



noperspective layout(location = 0)
out vec4  verts_vtx_color_0[64U];



centroid layout(location = 1)
out vec2  verts_vtx_uv_0[64U];



flat perprimitiveEXT layout(location = 2)
out uint  prims_texture_index_0[126U];



flat perprimitiveEXT layout(location = 3)
out uint  prims_mode_value_0[126U];



out uvec3  gl_PrimitiveTriangleIndicesEXT[126U];



struct trans_0
{
    uint  timeline_1[2];
};



struct Tuple_0
{
    uint value0_0;
    trans_0 value1_0;
};



struct draw_mode_0
{
    uint value_0;
};



struct Primitive_0
{
    uint texture_index_0;
    draw_mode_0 mode_0;
};



struct VertexUI_0
{
    Vertex_0 vtx_0;
    float  clip_dst_0[4];
};



layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
layout(max_primitives = 126) out;
layout(max_primitives = 126) out;
layout(max_vertices = 64) out;
layout(triangles) out;
void main()
{


    uint _S7;


    uint _S8 = gl_LocalInvocationID.x;


    uint _S9 = dbh_input_handles_0.dispatch_buffer_start_index_0;


    dbh_input_0 _S10 = { dbh_input_handles_0.dispatch_buffer_start_index_0, dbh_input_handles_0.dispatch_config_index_0, dbh_input_handles_0.ui_state_index_0, dbh_input_handles_0.slide_line_style_index_0 };


    uint _S11 = gl_WorkGroupID.x + dbh_input_get_dispatch_config_0(_S10).group_offset_0;


    uvec2 _S12 = uvec2(dbh_input_handles_0.dispatch_buffer_start_index_0, 0U);


    uint _S13 = uvec2(dbh_input_handles_0.dispatch_buffer_start_index_0 + 1U, 0U).x;


    uint _S14 = uvec2(dbh_input_handles_0.dispatch_buffer_start_index_0 + 2U, 0U).x;


    trans_0 _S15;


    _S15.timeline_1 = _slang_resource_heap_10[_S12.x]._data[uint(_S11)].timeline_0;


    trans_0 _S16 = _S15;


    for(;;)
    {


        uint _S17 = _slang_resource_heap_10[_S12.x]._data[uint(_S11)].vertex_offset_0;


        int _S18 = int(bool(_slang_resource_heap_10[_S12.x]._data[uint(_S11)].vertex_offset_0)) * 2;


        int _S19 = - int(_slang_resource_heap_10[_S12.x]._data[uint(_S11)].primit_offset_0);


        int i_0 = 0;


        Tuple_0 _S20;


        _S20.value0_0 = 1U;


        _S20.value1_0 = _S16;


        uint idx_to_head_0 = _slang_resource_heap_10[_S12.x]._data[uint(_S11)].instruction_head_index_0;


        int skipped_primitives_0 = _S19;


        uint skipped_vertices_0 = 0U;


        uint ptr_to_payload_0 = _slang_resource_heap_10[_S12.x]._data[uint(_S11)].instruction_offset_0;


        for(;;)
        {


            if(uint(i_0) < 2U)
        {
        }
        else
        {


                break;
        }


            int _S21;
            uint _S22 = i_0 == 1 ? 64U - _S8 - 1U : _S8;
            uint _S23 = _S22 + _S17;


            Tuple_0 _S24 = _S20;


            int skipped_primitives_1 = skipped_primitives_0;


            uint skipped_vertices_1 = skipped_vertices_0;


            uint ptr_to_payload_1 = ptr_to_payload_0;

            for(;;)
            {


                instruction_head_0 _S25 = _slang_resource_heap_9[_S13]._data[uint(idx_to_head_0)];

                if((_S25.type_0) == 1U)
                {


                    trans_0 _S26 = _S24.value1_0;


                    _S26.timeline_1[_S25.vertex_count_0] = _S26.timeline_1[_S25.vertex_count_0] + 1U;


                    idx_to_head_0 = idx_to_head_0 + 1U;


                    _S24.value0_0 = 1U;


                    _S24.value1_0 = _S26;

                    continue;
                }

                if(skipped_primitives_1 >= int(_slang_resource_heap_10[_S12.x]._data[uint(_S11)].primitives_0))
                {


                    _S21 = 1;


                    break;
                }
                uint _S27 = _S23 - skipped_vertices_1;
                if(_S27 < (_S25.vertex_count_0))
                {
                    uint _S28 = uint(_S18);


                    uint _S29;


                    if(_S28 > _S22)
                    {


                        _S29 = _S28 - _S22;


                    }
                    else
                    {


                        _S29 = 0U;


                    }


                    Vertex_0 _S30;


                    resolve_param_0 _S31 = resolve_param_x24init_0(_S22, _S27, _S29, skipped_primitives_1);


                    for(;;)
                    {


                        float _S32;


                        vec4 _S33;


                        switch(_S25.type_0)
                        {
                            case 2U:
                                {


                                uint _S34 = _slang_resource_heap_2[_S14]._data[uint(ptr_to_payload_1 / 4U)];


                                uint _S35 = _slang_resource_heap_2[_S14]._data[uint((ptr_to_payload_1 + 8U) / 4U)];


                                float _S36 = _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 12U) / 4U)];


                                vec2 _S37 = _slang_resource_heap_4[_S14]._data[uint((ptr_to_payload_1 + 16U) / 8U)];


                                vec2 _S38 = _slang_resource_heap_4[_S14]._data[uint((ptr_to_payload_1 + 24U) / 8U)];


                                vec2 _S39 = _slang_resource_heap_4[_S14]._data[uint((ptr_to_payload_1 + 32U) / 8U)];


                                vec2 _S40 = _slang_resource_heap_4[_S14]._data[uint((ptr_to_payload_1 + 40U) / 8U)];


                                vec2 _S41 = _slang_resource_heap_4[_S14]._data[uint((ptr_to_payload_1 + 48U) / 8U)];


                                vec2 _S42 = _slang_resource_heap_4[_S14]._data[uint((ptr_to_payload_1 + 56U) / 8U)];


                                vec4 _S43 = _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 64U) / 16U)];


                                vec4 _S44 = _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 80U) / 16U)];


                                vec4 _S45 = _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 96U) / 16U)];


                                for(;;)
                                    {


                                    if((_S31.vtx_skip_0) > 1U)
                                    {


                                        uint _S46 = uint(_S31.prm_skip_0) + _S31.vtx_skip_0 - 2U;


                                        Primitive_0 _S47;


                                        _S47.texture_index_0 = _S34;


                                        _S47.mode_0.value_0 = _S35;


                                        Primitive_0 _S48 = _S47;


                                        prims_texture_index_0[_S46] = _S47.texture_index_0;


                                        prims_mode_value_0[_S46] = _S48.mode_0.value_0;


                                        gl_PrimitiveTriangleIndicesEXT[_S46] = uvec3(_S31.vtx_idx_0 - 2U, _S31.vtx_idx_0 - 1U, _S31.vtx_idx_0);


                                    }


                                    switch(_S31.vtx_skip_0)
                                        {
                                            case 0U:
                                                {


                                            _S30 = Vertex_x24init_0(vec3(_S37, _S36), _S43, _S40);


                                            break;
                                                }
                                            case 1U:
                                                {


                                            _S30 = Vertex_x24init_0(vec3(_S38, _S36), _S44, _S41);


                                            break;
                                                }
                                            case 2U:
                                                {


                                            _S30 = Vertex_x24init_0(vec3(_S39, _S36), _S45, _S42);


                                            break;
                                                }
                                            default:
                                                {


                                            _S30 = Vertex_x24init_1();


                                            break;
                                                }
                                        }


                                    break;
                                    }


                                break;
                                }
                            case 3U:
                                {


                                uint _S49 = _slang_resource_heap_2[_S14]._data[uint(ptr_to_payload_1 / 4U)];


                                uint _S50 = _slang_resource_heap_2[_S14]._data[uint((ptr_to_payload_1 + 8U) / 4U)];


                                float _S51 = _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 12U) / 4U)];


                                vec2  _S52[4] = { vec2(_slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 16U) / 4U)], _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 20U) / 4U)]), vec2(_slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 24U) / 4U)], _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 28U) / 4U)]), vec2(_slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 32U) / 4U)], _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 36U) / 4U)]), vec2(_slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 40U) / 4U)], _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 44U) / 4U)]) };


                                vec2  _S53[4] = { vec2(_slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 48U) / 4U)], _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 52U) / 4U)]), vec2(_slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 56U) / 4U)], _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 60U) / 4U)]), vec2(_slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 64U) / 4U)], _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 68U) / 4U)]), vec2(_slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 72U) / 4U)], _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 76U) / 4U)]) };


                                quad_vert_color_0 _S54 = { vec4(_slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 80U) / 4U)], _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 84U) / 4U)], _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 88U) / 4U)], _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 92U) / 4U)]), vec4(_slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 96U) / 4U)], _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 100U) / 4U)], _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 104U) / 4U)], _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 108U) / 4U)]), vec4(_slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 112U) / 4U)], _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 116U) / 4U)], _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 120U) / 4U)], _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 124U) / 4U)]), vec4(_slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 128U) / 4U)], _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 132U) / 4U)], _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 136U) / 4U)], _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 140U) / 4U)]) };


                                if((_S31.vtx_skip_0) > 1U)
                                    {


                                    uint _S55 = uint(_S31.prm_skip_0) + _S31.vtx_skip_0 - 2U;


                                    Primitive_0 _S56;


                                    _S56.texture_index_0 = _S49;


                                    _S56.mode_0.value_0 = _S50;


                                    Primitive_0 _S57 = _S56;


                                    prims_texture_index_0[_S55] = _S56.texture_index_0;


                                    prims_mode_value_0[_S55] = _S57.mode_0.value_0;


                                    gl_PrimitiveTriangleIndicesEXT[_S55] = uvec3(_S31.vtx_idx_0 - 2U, _S31.vtx_idx_0 - 1U, _S31.vtx_idx_0);


                                }


                                Vertex_0 _S58 = Vertex_x24init_0(vec3(_S52[_S31.vtx_skip_0], _S51), quad_vert_color_operatorx5Bx5D_get_0(_S54, _S31.vtx_skip_0), _S53[_S31.vtx_skip_0]);


                                _S30 = _S58;


                                break;
                                }
                            case 4U:
                                {


                                uint _S59 = _slang_resource_heap_2[_S14]._data[uint(ptr_to_payload_1 / 4U)];


                                uint _S60 = _slang_resource_heap_2[_S14]._data[uint((ptr_to_payload_1 + 8U) / 4U)];


                                float _S61 = _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 12U) / 4U)];


                                vec2 _S62 = _slang_resource_heap_4[_S14]._data[uint((ptr_to_payload_1 + 16U) / 8U)];


                                float _S63 = _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 24U) / 4U)];


                                float _S64 = _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 28U) / 4U)];


                                vec4 _S65 = _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 32U) / 16U)];


                                vec4 _S66 = _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 48U) / 16U)];


                                vec4 _S67 = _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 64U) / 16U)];


                                vec4 _S68 = _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 80U) / 16U)];


                                vec2 _S69 = _slang_resource_heap_4[_S14]._data[uint((ptr_to_payload_1 + 96U) / 8U)];


                                vec2 _S70 = _slang_resource_heap_4[_S14]._data[uint((ptr_to_payload_1 + 104U) / 8U)];


                                vec2 _S71 = _slang_resource_heap_4[_S14]._data[uint((ptr_to_payload_1 + 112U) / 8U)];


                                for(;;)
                                    {


                                    vec2 rst_0;


                                    rst_0[1] = sin(_S63);


                                    rst_0[0] = cos(_S63);


                                    vec2 _S72 = rst_0 * _S64 * 0.5;


                                    vec2 _S73 = vec2(_S72.y, - _S72.x);


                                    if((_S31.vtx_skip_0) > 1U)
                                        {


                                        uint _S74 = uint(_S31.prm_skip_0) + _S31.vtx_skip_0 - 2U;


                                        Primitive_0 _S75;


                                        _S75.texture_index_0 = _S59;


                                        _S75.mode_0.value_0 = _S60;


                                        Primitive_0 _S76 = _S75;


                                        prims_texture_index_0[_S74] = _S75.texture_index_0;


                                        prims_mode_value_0[_S74] = _S76.mode_0.value_0;


                                        gl_PrimitiveTriangleIndicesEXT[_S74] = uvec3(_S31.vtx_idx_0 - 2U, _S31.vtx_idx_0 - 1U, _S31.vtx_idx_0);


                                    }


                                    switch(_S31.vtx_skip_0)
                                        {
                                            case 0U:
                                                {


                                            _S30 = Vertex_x24init_0(vec3(_S62 - _S69.x * _S72 - _S69.y * _S73, _S61), _S65, _S70);


                                            break;
                                                }
                                            case 1U:
                                                {


                                            _S30 = Vertex_x24init_0(vec3(_S62 + _S69.x * _S72 - _S69.y * _S73, _S61), _S66, vec2(_S71.x, _S70.y));


                                            break;
                                                }
                                            case 2U:
                                                {


                                            _S30 = Vertex_x24init_0(vec3(_S62 - _S69.x * _S72 + _S69.y * _S73, _S61), _S67, vec2(_S70.x, _S71.y));


                                            break;
                                                }
                                            case 3U:
                                                {


                                            _S30 = Vertex_x24init_0(vec3(_S62 + _S69.x * _S72 + _S69.y * _S73, _S61), _S68, _S71);


                                            break;
                                                }
                                            default:
                                                {


                                            _S30 = Vertex_x24init_1();


                                            break;
                                                }
                                        }


                                    break;
                                    }


                                break;
                                }
                            case 5U:
                                {


                                uint _S77 = _slang_resource_heap_2[_S14]._data[uint(ptr_to_payload_1 / 4U)];


                                uint _S78 = _slang_resource_heap_2[_S14]._data[uint((ptr_to_payload_1 + 8U) / 4U)];


                                float _S79 = _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 12U) / 4U)];


                                vec2 _S80 = _slang_resource_heap_4[_S14]._data[uint((ptr_to_payload_1 + 16U) / 8U)];


                                vec2 _S81 = _slang_resource_heap_4[_S14]._data[uint((ptr_to_payload_1 + 24U) / 8U)];


                                vec4 _S82 = _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 32U) / 16U)];


                                vec4 _S83 = _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 48U) / 16U)];


                                float _S84 = _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 68U) / 4U)];


                                float _S85 = _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 64U) / 4U)] * 0.5;


                                vec2 _S86 = normalize(_S81 - _S80);


                                vec2 _S87 = vec2(_S86.y, - _S86.x);


                                bool _S88 = ((_S31.vtx_skip_0) & 2U) != 0U;


                                bool _S89 = ((_S31.vtx_skip_0) & 1U) != 0U;


                                if(_S88)
                                    {


                                    _S32 = _S84;


                                }
                                    else
                                    {


                                    _S32 = - _S84;


                                }


                                float _S90;


                                if(_S89)
                                    {


                                    _S90 = - _S85;


                                }
                                    else
                                    {


                                    _S90 = _S85;


                                }


                                vec2 _S91;


                                if(_S88)
                                    {


                                    _S91 = _S81;


                                }
                                    else
                                    {


                                    _S91 = _S80;


                                }


                                if(_S88)
                                    {


                                    _S33 = _S83;


                                }
                                    else
                                    {


                                    _S33 = _S82;


                                }


                                vec2 _S92 = _S86 * _S32 + _S87 * _S90;


                                if((_S31.vtx_skip_0) > 1U)
                                    {


                                    uint _S93 = uint(_S31.prm_skip_0) + _S31.vtx_skip_0 - 2U;


                                    Primitive_0 _S94;


                                    _S94.texture_index_0 = _S77;


                                    _S94.mode_0.value_0 = _S78;


                                    Primitive_0 _S95 = _S94;


                                    prims_texture_index_0[_S93] = _S94.texture_index_0;


                                    prims_mode_value_0[_S93] = _S95.mode_0.value_0;


                                    gl_PrimitiveTriangleIndicesEXT[_S93] = uvec3(_S31.vtx_idx_0 - 2U, _S31.vtx_idx_0 - 1U, _S31.vtx_idx_0);


                                }


                                Vertex_0 _S96 = Vertex_x24init_0(vec3(_S91 + _S92, _S79), _S33, vec2(ivec2(0)));


                                _S30 = _S96;


                                break;
                                }
                            case 6U:
                                {


                                uint _S97 = _slang_resource_heap_2[_S14]._data[uint(ptr_to_payload_1 / 4U)];


                                uint _S98 = _slang_resource_heap_2[_S14]._data[uint((ptr_to_payload_1 + 8U) / 4U)];


                                float _S99 = _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 12U) / 4U)];


                                uint _S100 = ptr_to_payload_1 + 16U;


                                uint _S101 = _S31.vtx_skip_0 / 2U + 1U;


                                uint _S102 = _S100 + _S101 * 48U;


                                vec2 _S103 = _slang_resource_heap_4[_S14]._data[uint(_S102 / 8U)];


                                float _S104 = _slang_resource_heap_3[_S14]._data[uint((_S102 + 8U) / 4U)];


                                float _S105 = _slang_resource_heap_3[_S14]._data[uint((_S102 + 12U) / 4U)];


                                section_0 _S106 = { _slang_resource_heap_5[_S14]._data[uint((_S102 + 16U) / 16U)], _slang_resource_heap_5[_S14]._data[uint((_S102 + 32U) / 16U)] };


                                vec2 _S107 = _S103 - _slang_resource_heap_4[_S14]._data[uint((_S100 + (_S101 - 1U) * 48U) / 8U)];


                                vec2 _S108 = _slang_resource_heap_4[_S14]._data[uint((_S100 + (_S101 + 1U) * 48U) / 8U)] - _S103;


                                vec2 _S109 = vec2(- _S107.y, _S107.x);


                                vec2 _S110 = vec2(- _S108.y, _S108.x);


                                vec2 _S111 = _S109 * (inversesqrt((max(dot(_S109, _S109), 0.00000999999974738))));


                                vec2 _S112 = _S111 + _S110 * (inversesqrt((max(dot(_S110, _S110), 0.00000999999974738))));


                                vec2 _S113 = _S112 * rcp_0(max(dot(_S112, _S111), 0.00009999999747379));


                                if((_S31.vtx_skip_0) > 1U)
                                    {


                                    uint _S114 = uint(_S31.prm_skip_0) + _S31.vtx_skip_0 - 2U;


                                    Primitive_0 _S115;


                                    _S115.texture_index_0 = _S97;


                                    _S115.mode_0.value_0 = _S98;


                                    Primitive_0 _S116 = _S115;


                                    prims_texture_index_0[_S114] = _S115.texture_index_0;


                                    prims_mode_value_0[_S114] = _S116.mode_0.value_0;


                                    gl_PrimitiveTriangleIndicesEXT[_S114] = uvec3(_S31.vtx_idx_0 - 2U, _S31.vtx_idx_0 - 1U, _S31.vtx_idx_0);


                                }


                                if(bool((_S31.vtx_skip_0) & 1U))
                                    {


                                    _S32 = 0.5;


                                }
                                    else
                                    {


                                    _S32 = -0.5;


                                }


                                Vertex_0 _S117 = Vertex_x24init_0(vec3(fma(_S113, vec2(fma(_S32, _S104, _S105)), _S103), _S99), section_operatorx5Bx5D_get_0(_S106, _S31.vtx_skip_0), vec2(ivec2(0)));


                                _S30 = _S117;


                                break;
                                }
                            case 7U:
                                {


                                uint _S118 = _slang_resource_heap_2[_S14]._data[uint(ptr_to_payload_1 / 4U)];


                                uint _S119 = _slang_resource_heap_2[_S14]._data[uint((ptr_to_payload_1 + 8U) / 4U)];


                                float _S120 = _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 12U) / 4U)];


                                uint _S121 = ptr_to_payload_1 + 16U;


                                uint _S122 = (_S25.size_0 - 16U) / 48U;


                                uint _S123 = _S31.vtx_skip_0 / 2U;


                                uint _S124 = (_S123 + _S122 - 1U) % _S122;


                                vec2 _S125 = _slang_resource_heap_4[_S14]._data[uint((_S121 + _S124 * 48U) / 8U)];


                                uint _S126 = _S123 % _S122;


                                uint _S127 = _S121 + _S126 * 48U;


                                vec2 _S128 = _slang_resource_heap_4[_S14]._data[uint(_S127 / 8U)];


                                float _S129 = _slang_resource_heap_3[_S14]._data[uint((_S127 + 8U) / 4U)];


                                float _S130 = _slang_resource_heap_3[_S14]._data[uint((_S127 + 12U) / 4U)];


                                section_0 _S131 = { _slang_resource_heap_5[_S14]._data[uint((_S127 + 16U) / 16U)], _slang_resource_heap_5[_S14]._data[uint((_S127 + 32U) / 16U)] };


                                uint _S132 = (_S123 + 1U) % _S122;


                                vec2 _S133 = _S128 - _S125;


                                vec2 _S134 = _slang_resource_heap_4[_S14]._data[uint((_S121 + _S132 * 48U) / 8U)] - _S128;


                                vec2 _S135 = vec2(- _S133.y, _S133.x);


                                vec2 _S136 = vec2(- _S134.y, _S134.x);


                                vec2 _S137 = _S135 * (inversesqrt((max(dot(_S135, _S135), 0.00000999999974738))));


                                vec2 _S138 = _S137 + _S136 * (inversesqrt((max(dot(_S136, _S136), 0.00000999999974738))));


                                vec2 _S139 = _S138 * rcp_0(max(dot(_S138, _S137), 0.00009999999747379));


                                if((_S31.vtx_skip_0) > 1U)
                                    {


                                    uint _S140 = uint(_S31.prm_skip_0) + _S31.vtx_skip_0 - 2U;


                                    Primitive_0 _S141;


                                    _S141.texture_index_0 = _S118;


                                    _S141.mode_0.value_0 = _S119;


                                    Primitive_0 _S142 = _S141;


                                    prims_texture_index_0[_S140] = _S141.texture_index_0;


                                    prims_mode_value_0[_S140] = _S142.mode_0.value_0;


                                    gl_PrimitiveTriangleIndicesEXT[_S140] = uvec3(_S31.vtx_idx_0 - 2U, _S31.vtx_idx_0 - 1U, _S31.vtx_idx_0);


                                }


                                if(bool((_S31.vtx_skip_0) & 1U))
                                    {


                                    _S32 = 0.5;


                                }
                                    else
                                    {


                                    _S32 = -0.5;


                                }


                                Vertex_0 _S143 = Vertex_x24init_0(vec3(fma(_S139, vec2(fma(_S32, _S129, _S130)), _S128), _S120), section_operatorx5Bx5D_get_0(_S131, _S31.vtx_skip_0), vec2(ivec2(0)));


                                _S30 = _S143;


                                break;
                                }
                            case 8U:
                                {


                                uint _S144 = _slang_resource_heap_2[_S14]._data[uint(ptr_to_payload_1 / 4U)];


                                uint _S145 = _slang_resource_heap_2[_S14]._data[uint((ptr_to_payload_1 + 8U) / 4U)];


                                float _S146 = _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 12U) / 4U)];


                                vec2 _S147 = _slang_resource_heap_4[_S14]._data[uint((ptr_to_payload_1 + 16U) / 8U)];


                                section_1 _S148 = { _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 40U) / 4U)], _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 44U) / 4U)] };


                                vec4 _S149 = _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 64U) / 16U)];


                                vec4 _S150 = _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 80U) / 16U)];


                                float _S151 = fma(float(_S31.vtx_skip_0 / 2U) / float(_slang_resource_heap_2[_S14]._data[uint((ptr_to_payload_1 + 24U) / 4U)]), float_getPi_0() * 2.0, _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 28U) / 4U)]);


                                vec2 rst_1;


                                rst_1[1] = sin(_S151);


                                rst_1[0] = cos(_S151);


                                vec2 _S152 = rst_1;


                                float _S153 = section_operatorx5Bx5D_get_1(_S148, _S31.vtx_skip_0);


                                if((_S31.vtx_skip_0) > 1U)
                                    {


                                    uint _S154 = uint(_S31.prm_skip_0) + _S31.vtx_skip_0 - 2U;


                                    Primitive_0 _S155;


                                    _S155.texture_index_0 = _S144;


                                    _S155.mode_0.value_0 = _S145;


                                    Primitive_0 _S156 = _S155;


                                    prims_texture_index_0[_S154] = _S155.texture_index_0;


                                    prims_mode_value_0[_S154] = _S156.mode_0.value_0;


                                    gl_PrimitiveTriangleIndicesEXT[_S154] = uvec3(_S31.vtx_idx_0 - 2U, _S31.vtx_idx_0 - 1U, _S31.vtx_idx_0);


                                }


                                vec3 _S157 = vec3(fma(_S152, vec2(_S153), _S147), _S146);


                                if(bool((_S31.vtx_skip_0) & 1U))
                                    {


                                    _S33 = _S150;


                                }
                                    else
                                    {


                                    _S33 = _S149;


                                }


                                Vertex_0 _S158 = Vertex_x24init_0(_S157, _S33, vec2(ivec2(0)));


                                _S30 = _S158;


                                break;
                                }
                            case 9U:
                                {


                                uint _S159 = _slang_resource_heap_2[_S14]._data[uint(ptr_to_payload_1 / 4U)];


                                uint _S160 = _slang_resource_heap_2[_S14]._data[uint((ptr_to_payload_1 + 8U) / 4U)];


                                float _S161 = _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 12U) / 4U)];


                                vec2 _S162 = _slang_resource_heap_4[_S14]._data[uint((ptr_to_payload_1 + 16U) / 8U)];


                                section_1 _S163 = { _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 32U) / 4U)], _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 36U) / 4U)] };


                                section_0 _S164 = { _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 64U) / 16U)], _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 80U) / 16U)] };


                                section_0 _S165 = { _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 96U) / 16U)], _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 112U) / 16U)] };


                                float _S166 = float(_S31.vtx_skip_0 / 2U);


                                float _S167 = float(_slang_resource_heap_2[_S14]._data[uint((ptr_to_payload_1 + 24U) / 4U)]);


                                float _S168 = fma(_S166 / _S167, _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 44U) / 4U)], _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 40U) / 4U)]) * float_getPi_0() * 2.0;


                                vec2 rst_2;


                                rst_2[1] = sin(_S168);


                                rst_2[0] = cos(_S168);


                                vec2 _S169 = rst_2;


                                float _S170 = section_operatorx5Bx5D_get_1(_S163, _S31.vtx_skip_0);


                                vec4 _S171 = mix(section_operatorx5Bx5D_get_0(_S164, _S31.vtx_skip_0), section_operatorx5Bx5D_get_0(_S165, _S31.vtx_skip_0), vec4(_S166 / _S167));


                                if((_S31.vtx_skip_0) > 1U)
                                    {


                                    uint _S172 = uint(_S31.prm_skip_0) + _S31.vtx_skip_0 - 2U;


                                    Primitive_0 _S173;


                                    _S173.texture_index_0 = _S159;


                                    _S173.mode_0.value_0 = _S160;


                                    Primitive_0 _S174 = _S173;


                                    prims_texture_index_0[_S172] = _S173.texture_index_0;


                                    prims_mode_value_0[_S172] = _S174.mode_0.value_0;


                                    gl_PrimitiveTriangleIndicesEXT[_S172] = uvec3(_S31.vtx_idx_0 - 2U, _S31.vtx_idx_0 - 1U, _S31.vtx_idx_0);


                                }


                                Vertex_0 _S175 = Vertex_x24init_0(vec3(fma(_S169, vec2(_S170), _S162), _S161), _S171, vec2(ivec2(0)));


                                _S30 = _S175;


                                break;
                                }
                            case 10U:
                                {


                                uint _S176 = _slang_resource_heap_2[_S14]._data[uint(ptr_to_payload_1 / 4U)];


                                uint _S177 = _slang_resource_heap_2[_S14]._data[uint((ptr_to_payload_1 + 8U) / 4U)];


                                float _S178 = _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 12U) / 4U)];


                                vec4 _S179 = _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 16U) / 16U)];


                                vec4 _S180 = _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 32U) / 16U)];


                                mat4x2 _S181 = mat4x2(_S179[0], _S180[0], _S179[1], _S180[1], _S179[2], _S180[2], _S179[3], _S180[3]);


                                float _S182 = _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 48U) / 4U)];


                                float _S183 = _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 56U) / 4U)];


                                float _S184 = _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 60U) / 4U)];


                                float _S185 = _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 64U) / 4U)];


                                float _S186 = _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 68U) / 4U)];


                                section_0 _S187 = { _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 96U) / 16U)], _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 112U) / 16U)] };


                                section_0 _S188 = { _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 128U) / 16U)], _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 144U) / 16U)] };


                                float _S189 = float(_S31.vtx_skip_0 / 2U) / float(_slang_resource_heap_2[_S14]._data[uint((ptr_to_payload_1 + 72U) / 4U)]);


                                float _S190 = fma(_S189, 1.0 - _S182 - _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 52U) / 4U)], _S182);


                                uint _S191 = (_S31.vtx_skip_0) & 1U;


                                vec4 _S192 = mix(section_operatorx5Bx5D_get_0(_S187, _S31.vtx_skip_0), section_operatorx5Bx5D_get_0(_S188, _S31.vtx_skip_0), vec4(_S189));


                                float _S193 = _S190 * _S190;


                                vec2 _S194 = (((_S181) * (vec4(1.0, _S190, _S193, _S193 * _S190))));


                                vec2 _S195 = normalize((((_S181) * (vec4(0.0, 1.0, 2.0 * _S190, 3.0 * _S193)))));


                                vec2 _S196 = vec2(_S195.y, - _S195.x);


                                if((_S31.vtx_skip_0) > 1U)
                                    {


                                    uint _S197 = uint(_S31.prm_skip_0) + _S31.vtx_skip_0 - 2U;


                                    Primitive_0 _S198;


                                    _S198.texture_index_0 = _S176;


                                    _S198.mode_0.value_0 = _S177;


                                    Primitive_0 _S199 = _S198;


                                    prims_texture_index_0[_S197] = _S198.texture_index_0;


                                    prims_mode_value_0[_S197] = _S199.mode_0.value_0;


                                    gl_PrimitiveTriangleIndicesEXT[_S197] = uvec3(_S31.vtx_idx_0 - 2U, _S31.vtx_idx_0 - 1U, _S31.vtx_idx_0);


                                }


                                float _S200 = mix(_S183, _S184, _S189);


                                float _S201 = mix(_S185, _S186, _S189);


                                if(bool(_S191))
                                    {


                                    _S32 = -0.5;


                                }
                                    else
                                    {


                                    _S32 = 0.5;


                                }


                                Vertex_0 _S202 = Vertex_x24init_0(vec3(fma(_S196, vec2(_S200 * _S32 + _S201), _S194), _S178), _S192, vec2(ivec2(0)));


                                _S30 = _S202;


                                break;
                                }
                            case 11U:
                                {


                                uint _S203 = _slang_resource_heap_2[_S14]._data[uint(ptr_to_payload_1 / 4U)];


                                uint _S204 = _slang_resource_heap_2[_S14]._data[uint((ptr_to_payload_1 + 8U) / 4U)];


                                float _S205 = _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 12U) / 4U)];


                                vec2 _S206 = _slang_resource_heap_4[_S14]._data[uint((ptr_to_payload_1 + 16U) / 8U)];


                                vec2 _S207 = _slang_resource_heap_4[_S14]._data[uint((ptr_to_payload_1 + 24U) / 8U)];


                                vec2 _S208 = _slang_resource_heap_4[_S14]._data[uint((ptr_to_payload_1 + 32U) / 8U)];


                                vec2 _S209 = _slang_resource_heap_4[_S14]._data[uint((ptr_to_payload_1 + 40U) / 8U)];


                                vec4 _S210 = _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 48U) / 16U)];


                                vec4 _S211 = _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 64U) / 16U)];


                                vec4 _S212 = _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 80U) / 16U)];


                                vec4 _S213 = _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 96U) / 16U)];


                                for(;;)
                                    {


                                    if((_S31.vtx_skip_0) > 1U)
                                    {


                                        uint _S214 = uint(_S31.prm_skip_0) + _S31.vtx_skip_0 - 2U;


                                        Primitive_0 _S215;


                                        _S215.texture_index_0 = _S203;


                                        _S215.mode_0.value_0 = _S204;


                                        Primitive_0 _S216 = _S215;


                                        prims_texture_index_0[_S214] = _S215.texture_index_0;


                                        prims_mode_value_0[_S214] = _S216.mode_0.value_0;


                                        gl_PrimitiveTriangleIndicesEXT[_S214] = uvec3(_S31.vtx_idx_0 - 2U, _S31.vtx_idx_0 - 1U, _S31.vtx_idx_0);


                                    }


                                    switch(_S31.vtx_skip_0)
                                        {
                                            case 0U:
                                                {


                                            _S30 = Vertex_x24init_0(vec3(_S206, _S205), _S210, _S208);


                                            break;
                                                }
                                            case 1U:
                                                {


                                            _S30 = Vertex_x24init_0(vec3(vec2(_S207.x, _S206.y), _S205), _S211, vec2(_S209.x, _S208.y));


                                            break;
                                                }
                                            case 2U:
                                                {


                                            _S30 = Vertex_x24init_0(vec3(vec2(_S206.x, _S207.y), _S205), _S212, vec2(_S208.x, _S209.y));


                                            break;
                                                }
                                            case 3U:
                                                {


                                            _S30 = Vertex_x24init_0(vec3(_S207, _S205), _S213, _S209);


                                            break;
                                                }
                                            default:
                                                {


                                            _S30 = Vertex_x24init_1();


                                            break;
                                                }
                                        }


                                    break;
                                    }


                                break;
                                }
                            case 12U:
                                {


                                uint _S217 = _slang_resource_heap_2[_S14]._data[uint(ptr_to_payload_1 / 4U)];


                                uint _S218 = _slang_resource_heap_2[_S14]._data[uint((ptr_to_payload_1 + 8U) / 4U)];


                                float _S219 = _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 12U) / 4U)];


                                vec2  _S220[2] = _slang_resource_heap_6[_S14]._data[uint((ptr_to_payload_1 + 16U) / 16U)];


                                float  _S221[4] = _slang_resource_heap_7[_S14]._data[uint((ptr_to_payload_1 + 32U) / 16U)];


                                quad_vert_color_0 _S222 = { _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 48U) / 16U)], _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 64U) / 16U)], _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 80U) / 16U)], _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 96U) / 16U)] };


                                uint _S223 = _S31.vtx_skip_0 / 2U;


                                uint _S224 = (idx_0[_S223]) & 1U;


                                bool _S225 = bool((idx_0[_S223]) & 2U);


                                vec2 _S226 = vec2(_S220[_S224].x, _S220[uint(_S225)].y);


                                if(bool(_S224))
                                    {


                                    _S21 = -1;


                                }
                                    else
                                    {


                                    _S21 = 1;


                                }


                                float _S227 = float(_S21);


                                int _S228;


                                if(_S225)
                                    {


                                    _S228 = -1;


                                }
                                    else
                                    {


                                    _S228 = 1;


                                }


                                vec2 _S229 = vec2(_S227, float(_S228));


                                float _S230 = (bool((_S31.vtx_skip_0) & 1U) ? -0.5 : 0.5) * _S221[idx_0[_S223]];


                                if((_S31.vtx_skip_0) > 1U)
                                    {


                                    uint _S231 = uint(_S31.prm_skip_0) + _S31.vtx_skip_0 - 2U;


                                    Primitive_0 _S232;


                                    _S232.texture_index_0 = _S217;


                                    _S232.mode_0.value_0 = _S218;


                                    Primitive_0 _S233 = _S232;


                                    prims_texture_index_0[_S231] = _S232.texture_index_0;


                                    prims_mode_value_0[_S231] = _S233.mode_0.value_0;


                                    gl_PrimitiveTriangleIndicesEXT[_S231] = uvec3(_S31.vtx_idx_0 - 2U, _S31.vtx_idx_0 - 1U, _S31.vtx_idx_0);


                                }


                                Vertex_0 _S234 = Vertex_x24init_0(vec3(fma(_S229, vec2(_S230), _S226), _S219), quad_vert_color_operatorx5Bx5D_get_0(_S222, idx_0[_S223]), vec2(ivec2(0)));


                                _S30 = _S234;


                                break;
                                }
                            case 13U:
                                {


                                uint _S235 = _slang_resource_heap_2[_S14]._data[uint(ptr_to_payload_1 / 4U)];


                                uint _S236 = _slang_resource_heap_2[_S14]._data[uint((ptr_to_payload_1 + 8U) / 4U)];


                                float _S237 = _slang_resource_heap_3[_S14]._data[uint((ptr_to_payload_1 + 12U) / 4U)];


                                float  _S238[4] = _slang_resource_heap_7[_S14]._data[uint((ptr_to_payload_1 + 16U) / 16U)];


                                uint _S239 = _S31.vtx_skip_0 / 2U;


                                uint _S240 = (_S31.vtx_skip_0) & 1U;


                                vec2 _S241 = vec2(_S238[_S239], _slang_resource_heap_8[_S14]._data[uint((ptr_to_payload_1 + 32U) / 8U)][_S240]);


                                vec2 _S242 = vec2(_slang_resource_heap_7[_S14]._data[uint((ptr_to_payload_1 + 48U) / 16U)][_S239], _slang_resource_heap_8[_S14]._data[uint((ptr_to_payload_1 + 40U) / 8U)][_S240]);


                                bool _S243 = bool(_S240);


                                vec4 _S244 = mix(_S243 ? _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 96U) / 16U)] : _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 64U) / 16U)], _S243 ? _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 112U) / 16U)] : _slang_resource_heap_5[_S14]._data[uint((ptr_to_payload_1 + 80U) / 16U)], vec4((_S238[_S239] - _S238[0]) / (_S238[3] - _S238[0])));


                                if((_S31.vtx_skip_0) > 1U)
                                    {


                                    uint _S245 = uint(_S31.prm_skip_0) + _S31.vtx_skip_0 - 2U;


                                    Primitive_0 _S246;


                                    _S246.texture_index_0 = _S235;


                                    _S246.mode_0.value_0 = _S236;


                                    Primitive_0 _S247 = _S246;


                                    prims_texture_index_0[_S245] = _S246.texture_index_0;


                                    prims_mode_value_0[_S245] = _S247.mode_0.value_0;


                                    gl_PrimitiveTriangleIndicesEXT[_S245] = uvec3(_S31.vtx_idx_0 - 2U, _S31.vtx_idx_0 - 1U, _S31.vtx_idx_0);


                                }


                                Vertex_0 _S248 = Vertex_x24init_0(vec3(_S241, _S237), _S244, _S242);


                                _S30 = _S248;


                                break;
                                }
                            default:
                                {


                                _S30 = Vertex_x24init_1();


                                break;
                                }
                        }


                        break;
                    }



                    Vertex_0 _S249 = _S30;


                    ubo_layer_info_0 _S250 = _slang_resource_heap_1[uvec2(_S9 + 4U, 0U).x]._data[uint(_S24.value1_0.timeline_1[1U] - 1U)];


                    vec3 _S251 = (((vec3(_S249.position_0.xy, 1.0)) * (_S250.element_to_screen_0)));


                    _S249.position_0.xy = (((_S251) * (_slang_resource_heap_0[uvec2(_S9 + 3U, 0U).x]._data[uint(_S24.value1_0.timeline_1[0U] - 1U)].screen_to_uniform_0))).xy;


                    VertexUI_0 vtx_1;


                    vtx_1.vtx_0 = _S249;


                    float _S252 = _S251.x;


                    float right_plane_dist_0 = _S250.scissor_1.dst_0.x - _S252;


                    float _S253 = _S251.y;


                    float top_plane_dist_0 = _S250.scissor_1.dst_0.y - _S253;


                    float bottom_plane_dist_0 = _S253 - _S250.scissor_1.src_0.y;


                    vtx_1.clip_dst_0[0] = _S252 - _S250.scissor_1.src_0.x;


                    vtx_1.clip_dst_0[1] = right_plane_dist_0;


                    vtx_1.clip_dst_0[2] = top_plane_dist_0;


                    vtx_1.clip_dst_0[3] = bottom_plane_dist_0;


                    VertexUI_0 _S254 = vtx_1;


                    gl_MeshVerticesEXT[_S22].gl_Position = vtx_1.vtx_0.position_0;


                    verts_vtx_color_0[_S22] = _S254.vtx_0.color_0;


                    verts_vtx_uv_0[_S22] = _S254.vtx_0.uv_0;


                    gl_MeshVerticesEXT[_S22].gl_ClipDistance = _S254.clip_dst_0;


                    _S21 = 2;


                    break;
                }


                uint ptr_to_payload_2 = ptr_to_payload_1 + _S25.size_0;
                uint skipped_vertices_2 = skipped_vertices_1 + _S25.vertex_count_0;
                int skipped_primitives_2 = skipped_primitives_1 + int(_S25.primit_count_0);


                idx_to_head_0 = idx_to_head_0 + 1U;


                skipped_primitives_1 = skipped_primitives_2;


                skipped_vertices_1 = skipped_vertices_2;


                ptr_to_payload_1 = ptr_to_payload_2;


            }


            if(_S21 != 2)
            {


                break;
            }


            i_0 = i_0 + 1;


            _S20 = _S24;


            skipped_primitives_0 = skipped_primitives_1;


            skipped_vertices_0 = skipped_vertices_1;


            ptr_to_payload_0 = ptr_to_payload_1;


        }


        _S7 = _slang_resource_heap_10[_S12.x]._data[uint(_S11)].primitives_0;


        break;
    }


    if(_S8 == 0U)
    {


        SetMeshOutputsEXT(64U, _S7);


    }
    return;
}

