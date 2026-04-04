module ui_structs;

namespace gui {

public struct ubo_screen_info {
    public float3x3 screen_to_uniform;
};

public struct scissor {
    public float2 src;
    public float2 dst;
    public float margin;
    uint32_t _c1, _c2, _c3;
}

public struct ubo_layer_info {
    public float3x3 element_to_screen;
    public scissor scissor;
};

public struct ubo_ui_state {
    public float2 screen_extent;
    public float time;
    uint32_t _cap;
};
}
