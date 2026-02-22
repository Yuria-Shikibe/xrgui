#pragma shader_stage(fragment)
#extension GL_EXT_descriptor_heap : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_mesh_shader : require

struct ubo_ui_state {
    float time;
    uint _cap[3];
};

struct slide_line_style {
    float angle;
    float scale;

    float spacing;
    float stroke;

    float speed;
    float phase;

    float margin;

    float opacity;
};

layout(push_constant) uniform dbh_input {
    uint dispatch_buffer_start_index;

    uint dispatch_config_index;
    uint ui_state_index;
    uint slide_line_style_index;
} push_constant;

layout(descriptor_heap) uniform sampler heapSampler[];
layout(descriptor_heap) uniform texture2D heapTexture2D[];
layout(descriptor_heap) buffer STATES {
    ubo_ui_state state[];
} states[];

layout(descriptor_heap) buffer SLIDE_LINES {
    slide_line_style state;
} slide_lines[];
layout(descriptor_heap) uniform sampler heapSampler[];


//layout (location = 0) in vec4 pos;
noperspective layout (location = 0) in vec4 color;
 layout (location = 1) in vec2 uv;
perprimitiveEXT layout (location = 2) flat in uint image_index;
perprimitiveEXT layout (location = 3) flat in uint draw_mode;
//layout (location = 5) flat in float depth;

layout (location = 0) out vec4 fragColor;

void main() {
//    fragColor = color;//image_index == ~0 ? vec4(1) : texture(sampler2D(heapTexture2D[image_index], heapSampler[0]), uv);
//    fragColor = vec4(uv * 100, 1, 1);//image_index == ~0 ? vec4(1) : texture(sampler2D(heapTexture2D[image_index], heapSampler[0]), uv);

//    fragColor = imageLoad(heapTexture2D[4], ivec2(0));
    fragColor = vec4(texture(sampler2D(heapTexture2D[0], heapSampler[0]), vec2(0)).rgb, 1);
//    fragColor = vec4(vec3(states[264 + 6].state[0].time / 30), 1);
}