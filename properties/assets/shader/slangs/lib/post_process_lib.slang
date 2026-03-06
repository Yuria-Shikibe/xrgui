module post_process_lib;

public static const uint GROUP_SIZE = 16;

namespace pp
{

// 使用魔法数字将交错的偶数位压缩到低位
// 提取如 0b0a0b0c0d 中的 a,b,c,d 到 0b0000abcd
uint compact1By1(uint x)
{
    x &= 0x55555555;                 // 保留偶数位
    x = (x ^ (x >> 1)) & 0x33333333; // 将位两两靠拢
    x = (x ^ (x >> 2)) & 0x0f0f0f0f; // 将位四四靠拢
    x = (x ^ (x >> 4)) & 0x00ff00ff; // 将位八八靠拢
    x = (x ^ (x >> 8)) & 0x0000ffff; // 完成最后合并
    return x;
}

// 接受一维索引和区块尺寸（尺寸必须是2的幂），返回 2D Z-order 索引
public uint2 decode_z_order_rect(uint index, constexpr uint2 size = { GROUP_SIZE, GROUP_SIZE }){
    // static_assert(countbits(size).equals(uint2(1)), "size must in power of two");

    // 使用 firstbithigh 获取 2的幂 尺寸的位宽 (例如 8 -> 3, 16 -> 4)
    uint xBits = firstbithigh(size.x);
    uint yBits = firstbithigh(size.y);

    // 计算 X 和 Y 共有的交错深度
    uint minBits = min(xBits, yBits);

    // 1. 处理交错部分 (低位 2 * minBits 个 bit)
    uint interleavedMask = (1u << (2 * minBits)) - 1u;
    uint interleavedPart = index & interleavedMask;

    uint x = compact1By1(interleavedPart);
    uint y = compact1By1(interleavedPart >> 1);

    // 2. 处理非交错部分 (高位属于较长维度的剩余 bit)
    uint remainingPart = index >> (2 * minBits);

    // 因为长宽都是2的幂，不可能存在非整数的溢出逻辑，直接将剩余高位拼接到较大维度上
    if (xBits > yBits)
    {
        x |= (remainingPart << minBits);
    }
    else if (yBits > xBits)
    {
        y |= (remainingPart << minBits);
    }

    return uint2(x, y);
}

/*
 * Copyright (c) 2020-2024, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

// Normally argument "dipatchGridDim" is parsed through a constant buffer. However, if for some reason it is a
// static value, some DXC compiler versions will be unable to compile the code.
// If that's the case for you, flip DXC_STATIC_DISPATCH_GRID_DIM definition from 0 to 1.
#define DXC_STATIC_DISPATCH_GRID_DIM 0

// Divide the 2D-Dispatch_Grid into tiles of dimension [N, DipatchGridDim.y]
// “CTA” (Cooperative Thread Array) == Thread Group in DirectX terminology
uint2 ThreadGroupTilingX(
    const uint2 dipatchGridDim,  // Arguments of the Dispatch call (typically from a ConstantBuffer)
    const uint2 groupThreadID,   // SV_GroupThreadID
    const uint2 groupId,         // SV_GroupID
    constexpr uint maxTileWidth, // User parameter (N). Recommended values: 8, 16 or 32.
    constexpr uint2 ctaDim,      // Already known in HLSL, eg:[numthreads(8, 8, 1)] -> uint2(8, 8)
)
{
    // A perfect tile is one with dimensions = [maxTileWidth, dipatchGridDim.y]
    const uint Number_of_CTAs_in_a_perfect_tile = maxTileWidth * dipatchGridDim.y;

    // Possible number of perfect tiles
    const uint Number_of_perfect_tiles = dipatchGridDim.x / maxTileWidth;

    // Total number of CTAs present in the perfect tiles
    const uint Total_CTAs_in_all_perfect_tiles = Number_of_perfect_tiles * maxTileWidth * dipatchGridDim.y;
    const uint vThreadGroupIDFlattened = dipatchGridDim.x * groupId.y + groupId.x;

    // Tile_ID_of_current_CTA : current CTA to TILE-ID mapping.
    const uint Tile_ID_of_current_CTA = vThreadGroupIDFlattened / Number_of_CTAs_in_a_perfect_tile;
    const uint Local_CTA_ID_within_current_tile = vThreadGroupIDFlattened % Number_of_CTAs_in_a_perfect_tile;
    uint Local_CTA_ID_y_within_current_tile;
    uint Local_CTA_ID_x_within_current_tile;

    if (Total_CTAs_in_all_perfect_tiles <= vThreadGroupIDFlattened)
    {
        // Path taken only if the last tile has imperfect dimensions and CTAs from the last tile are launched. 
        uint X_dimension_of_last_tile = dipatchGridDim.x % maxTileWidth;
        X_dimension_of_last_tile = max(1, X_dimension_of_last_tile);
        Local_CTA_ID_y_within_current_tile = Local_CTA_ID_within_current_tile / X_dimension_of_last_tile;
        Local_CTA_ID_x_within_current_tile = Local_CTA_ID_within_current_tile % X_dimension_of_last_tile;
    }
    else
    {
        Local_CTA_ID_y_within_current_tile = Local_CTA_ID_within_current_tile / maxTileWidth;
        Local_CTA_ID_x_within_current_tile = Local_CTA_ID_within_current_tile % maxTileWidth;
    }

    const uint Swizzled_vThreadGroupIDFlattened =
        Tile_ID_of_current_CTA * maxTileWidth +
        Local_CTA_ID_y_within_current_tile * dipatchGridDim.x +
        Local_CTA_ID_x_within_current_tile;

    uint2 SwizzledvThreadGroupID;
    SwizzledvThreadGroupID.y = Swizzled_vThreadGroupIDFlattened / dipatchGridDim.x;
    SwizzledvThreadGroupID.x = Swizzled_vThreadGroupIDFlattened % dipatchGridDim.x;

    uint2 SwizzledvThreadID;
    SwizzledvThreadID.x = ctaDim.x * SwizzledvThreadGroupID.x + groupThreadID.x;
    SwizzledvThreadID.y = ctaDim.y * SwizzledvThreadGroupID.y + groupThreadID.y;

    return SwizzledvThreadID.xy;
}

// 包装函数：根据总任务尺寸计算 GridDim，并返回重排后的 2D 线程 ID
public uint2 get_swizzled_thread_id(
    const uint2 totalDim,      // 任务的总尺寸 (例如：纹理的宽和高)
    const uint2 groupThreadID, // 语义参数：SV_GroupThreadID
    const uint2 groupId,
    constexpr uint maxTileWidth = 8,         // 语义参数：SV_GroupID
    constexpr uint2 ctaDim = {GROUP_SIZE, GROUP_SIZE},        // 前 Shader 的线程组尺寸，例如 uint2(8, 8)
    
){
    // 计算分发网格尺寸 (等同于 CPU 端的向上取整除法)
    // 编译器会自动将这里的除法优化为位运算，因为 ctaDim 是 2 的幂
    const uint2 dispatchGridDim = (totalDim + ctaDim - 1) / ctaDim;

    // 调用底层的 Swizzling 核心函数
    return ThreadGroupTilingX(dispatchGridDim, groupThreadID, groupId, maxTileWidth, ctaDim);
}

namespace colors {
public static float4 black = { 0, 0, 0, 1 };
}


[ForceInline]
public float luma(float3 rgb)
{
    return dot(rgb, float3(0.299, 0.587, 0.114));
}

[ForceInline]
public float luma(float4 val)
{
    return luma(val.rgb) * val.a;
}

namespace blend
{

[ForceInline]
public float3 screen(float3 src, float3 dst)
{
    return src + dst - src * dst;
}

public float4 screen_straight(float4 top, float4 bottom)
{
    float outAlpha = top.a + bottom.a - top.a * bottom.a;

    float3 topWeighted = top.rgb * top.a;
    float3 bottomWeighted = bottom.rgb * bottom.a;
    float3 outRGB = (topWeighted + bottomWeighted - topWeighted * bottomWeighted) / outAlpha;

    return float4(outRGB, outAlpha);
}

[ForceInline]
public float4 alpha_blend(const in float4 src, const in float4 dst)
{
    let rgb = lerp(dst.rgb, src.rgb, src.a);
    let a = fma(dst.a, 1 - src.a, src.a);
    return float4(rgb, a);
}

[ForceInline]
public float4 pre_scaled_alpha_blend(const in float4 src, const in float4 dest) {
    return fma(dest, saturate(1.0 - src.a), src);
}

}

[ForceInline]
public float3 hdr_to_sdr(float3 color_in_hdr, const float exposure, const float gamma) {
    let mapped = float3(1.0) - exp(-color_in_hdr * exposure);

    // 或者使用其他色调映射算子：
    // vec3 mapped = hdrColor / (hdrColor + vec3(1.0)); // Reinhard简单版本
    // vec3 mapped = hdrColor * exposure / (hdrColor * exposure + vec3(1.0)); // 带曝光的Reinhard

    return pow(mapped, float3(1.0 / gamma));
}

[ForceInline]
public float2 uniform_to_normalize(float2 uniformed) {
    return (uniformed + 1.f) * .5f;
}

[ForceInline]
public float2 coord_to_uniform(float2 coord, float2 extent) {
    return ((coord / extent) - float2(.5f)) * 2.f;
}

[ForceInline]
public float2 coord_to_uniform(uint2 coord, uint2 extent) {
    return coord_to_uniform(float2(coord) + float2(.5f, .5f), float2(extent));
}

}
