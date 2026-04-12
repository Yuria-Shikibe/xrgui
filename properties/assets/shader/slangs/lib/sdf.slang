module sdf;

import general_lib;

namespace sdf {

[ForceInline]
float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

[[ForceInline]]
float median(float3 v) {
    return median(v.r, v.g, v.b);
}

float CalculateLOD(float2 fw, float2 textureSize) {
    let texels = fw * textureSize;

    var maxDelta = max(texels.x, texels.y);
    maxDelta = max(maxDelta, 0.00001);

    return log2(maxDelta);
}

public struct sdf_result {
    public float factor;
    public float scale;
}

[[ForceInline]]
public sdf_result msdf(Sampler2D samp, float2 uv, float2 size, float pxRange = 4.f) {
    let fw = fwidth(uv);





    let col = samp.SampleLevel(uv, 0.0f).rgb;
    float sigDist = median(col) - 0.5f;


    float sdfToPixelScale = max(0.5f * dot(float2(pxRange) * rcp(size), rcp(fw)), 1.0f);


    return { sigDist * sdfToPixelScale, rcp(sdfToPixelScale)};
}

[[ForceInline]]
public float msdf_with_lod(Sampler2D samp, float2 uv, float2 size, float pxRange = 4.f) {
    let fw = fwidth(uv);





    float2 texelsPerPixel = fw * size;
    float lod = max(0.0, log2(max(texelsPerPixel.x, texelsPerPixel.y)));

    let col = samp.SampleLevel(uv, lod).rgb;

    float sigDist = median(col) - 0.5f;

    sigDist *= max(0.5 * dot(float2(pxRange) * rcp(size), rcp(fw)), 1.0);

    return sigDist;
}


[[ForceInline]]
public float msdf(Texture2D samp, SamplerState sampler, float2 uv) {
    let fw = fwidth(uv);
    let lod = min(fw.x, fw.y);

    float2 size;
    float _;
    samp.GetDimensions(uint(lod), size.x, size.y, _);

    let col = samp.SampleLevel(sampler, uv, lod).rgb;
    float sigDist = median(col) - 0.5f;

    sigDist *= max(0.5 * dot(rcp(size), rcp(fw)), 1.0);;

    return sigDist;
}

[[ForceInline]]
public float4 visualize(float distance, float scale){



    float d = distance / scale;





    float4 colorInside = float4(0.1, 0.2, 0.6, 1.0);


    float4 colorSurface = float4(1.0, 1.0, 1.0, 1.0);


    float4 colorOutsideNear = float4(0.6, 0.8, 1.0, 1.0);


    float4 colorOutsideFar = float4(0.1, 0.6, 0.1, 1.0);



    float4 finalColor;





    if (d <= 0.0)
    {

        float k = smoothstep(0.0, 1.0, -d);
        finalColor = lerp(colorInside, colorSurface, k);
    }


    else
    {

        float k1 = smoothstep(0.0, 1.0, d);


        float4 nearColor = lerp(colorSurface, colorOutsideNear, k1);


        float k2 = smoothstep(1.0, 2.0, d);


        finalColor = lerp(nearColor, colorOutsideFar, k2);
    }

    return finalColor;
}

}