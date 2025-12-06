//
// Created by Matrix on 2025/10/28.
//

export module mo_yanxi.hlsl_alias;

export import mo_yanxi.math.vector2;
export import mo_yanxi.graphic.color;

namespace mo_yanxi::graphic{

export using float1 = float;
export using float2 = math::vec2;
export struct alignas(16) float4 : color{};

}