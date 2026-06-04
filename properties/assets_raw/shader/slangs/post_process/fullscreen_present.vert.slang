struct VSOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOut main_vert(uint vertex_id : SV_VertexID) {
    float2 positions[3] = {
        float2(-1.0f, -1.0f),
        float2( 3.0f, -1.0f),
        float2(-1.0f,  3.0f),
    };

    let pos = positions[vertex_id];
    VSOut output;
    output.position = float4(pos, 0.0f, 1.0f);
    output.uv = pos * 0.5f + 0.5f;
    return output;
}
