module slide_line;

namespace slide_line {

public struct slide_line_style {
    float angle;
    float scale;
    
    float spacing;
    float stroke;

    float speed;
    float phase;

    float margin;

    float opacity;
};

[[ForceInline]]
float slope(float value){
    return 1.0f - abs(value - 0.5f) * 2.0f;
}

[[ForceInline]]
float pingpong(float src, float dst, float value) {
    float len = dst - src;
    float t = abs(fmod(value, 2.0 * abs(len)) / len - 1.0);
    return lerp(src, dst, t);
}

[[ForceInline]]
float get_dst_at(const float angle, const float2 T) {
    float sin_, cos_;
    sincos(radians(angle), sin_, cos_);

    let nor = float2(cos_, sin_);
    let len = dot(nor, T);
    return len;
}

// [[ForceInline]]
// public bool is_in_slide_line(const float2 T, const slide_line_style style, float time){
//     const float len = get_dst_at(style.angle, T) * style.scale;

//     return fmod(len + time, style.spacing) < style.stroke;// step(, stroke);
// }

[[ForceInline]]
public float is_in_slide_line_smooth(const float2 T, const slide_line_style style, float time){
    let len = get_dst_at(style.angle, T) * style.scale;
    let duration = style.spacing + style.stroke;
    let scl = slope(fmod(len + style.phase + time * style.speed, duration) / duration);

    float v = scl * duration / style.spacing;
    return lerp(style.opacity, 1, smoothstep(1.f - style.margin, 1.f + style.margin, v));
}

}