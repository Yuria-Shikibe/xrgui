import post_process_lib;
import general_lib;

struct volumn_info {
    float current_time;
    uint32_t _c1, _c2, _c3;

    uint32_t[8] count;
}

struct volume_generic {
    uint32_t blend_priority;
    float birth_time;
    float lifetime; //TODO make it inv on cpu side?
    float depth;

    [ForceInline]
    float get_life_scale(float current_time) {
        return (current_time - birth_time) / lifetime;
    }
}

struct vfx_result {
    float2 resample_offset;
    float blend_factor;
    float4 color;
}

interface sdf {
    float get_distance(float2 pos);
}

struct shockwave : sdf {
    volume_generic generic;

    float2 pos;
    float radius;
    float stroke;
    float intensity;
    float fade_factor;
    float blend_factor;
    float time_effect_factor;

    float4 color_from;
    float4 color_to;

    [ForceInline]
    float get_distance(float2 pos){
        return distance(this.pos, pos) - radius;
    }

    vfx_result get_resample_vec(float2 world_pos, float current_time) {
        // static const float thickness = 15;
        let toCenter = world_pos - pos;
        let dist = length(toCenter);

        if (dist > radius + stroke) {
            return {};
        }

        let fin = generic.get_life_scale(current_time);
        let fin_pow = interp::pow_out(fin, 3);
        let curent_r = fin_pow * radius;

        let dst_to_wave = dist - curent_r;
        let scaled_dtw = dst_to_wave / stroke;
        let factor = (1.0 - smoothstep(0.0, radius, dist)); // TODO make it 1 - pow2Out
        let attenuation = exp(-scaled_dtw * scaled_dtw * fade_factor) * factor;
        let color_scl = (dst_to_wave > 0 ? attenuation : smoothstep(fin_pow, 1, dist / curent_r) * factor) * (1.f - fin_pow);

        // 计算扭曲
        let direction = normalize(toCenter);
        var distortion = attenuation * intensity;

        // 添加时间动画
        let timeEffect = sin(dst_to_wave * 15.0 - current_time) * time_effect_factor;
        distortion += timeEffect * intensity;

        return vfx_result(direction * distortion * (1 - fin), attenuation * blend_factor, lerp(color_from, color_to, fin + timeEffect) * color_scl);
    }
};

[[vk::binding(0, 0)]]
restrict WTexture2D output;

[[vk::binding(1, 0)]]
Sampler2D input;

//TODO depth input as predicate

[[vk::binding(0, 1)]]
ConstantBuffer<volumn_info> ubo;

[[vk::binding(1, 1)]]
StructuredBuffer<shockwave> shockwaves;

[[vk::binding(0, 2)]]
ConstantBuffer<WorldProjection> proj;

groupshared float3x3 inverseViewMatrix;

[shader("compute")]
[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(
    uint3 dispatchThreadID: SV_DispatchThreadID
) {
    let pos = dispatchThreadID.xy;
    // let hdrColor = input.Load(uint3(pos, 0));

    let imgSz = output.imageSize2D();
    let uniformed = pp::coord_to_uniform(pos, imgSz);
    let world = mul(proj.inv, float3(uniformed.xy, 1)).xy;

    let instr_count = ubo.count[0];
    float maxAlpha = 0;
    float4 blendColor = {};
    var resVec = float2(0);
    for (uint i = 0; i < instr_count; ++i) {
        let instr = shockwaves[i];
        let rst = instr.get_resample_vec(world, ubo.current_time);
        maxAlpha += rst.blend_factor;

        resVec += rst.resample_offset * rst.blend_factor;
        blendColor = pp::blend::pre_scaled_alpha_blend(rst.color, blendColor);// * rst.blend_factor;
    }

    if (maxAlpha > 0) {
        resVec /= maxAlpha;
        // blendColor /= maxAlpha;
    }

    let uv = pp::uniform_to_normalize(mul(proj.view, mul(proj.proj, float3(resVec + world, 1))).xy);
    let rspColor = input.Sample(uv);
    output.Store(pos, pp::blend::pre_scaled_alpha_blend(blendColor, rspColor));
    // output.Store(pos, pp::blend::alpha_blend(blendColor, rspColor));
}