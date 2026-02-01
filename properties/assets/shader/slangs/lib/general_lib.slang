module general_lib;

public extension<T> vector<T, 2> where T : IArithmetic {
    [ForceInline]
    public vector<T, 2> rotate_rt_counter_clockwise() {
        return vector<T, 2>(this.y, -this.x);
    }
}

[ForceInline]
public func cross<T>(vector<T, 2> l, vector<T, 2> r)->T where T : IArithmetic {
    return l.x * r.y - l.y * r.x;
}

[ForceInline]
public float angle_between(float2 from, float2 to) {
    return atan2(cross(from, to), dot(from, to));
}

[ForceInline]
public func rotate_rt_counter_clockwise<T>(vector<T, 2> vec)->vector<T, 2> where T : IArithmetic {
    return vector<T, 2>(vec.y, -vec.x);
}

public extension float
{
    public static const float sqrt2 = 1.4142135623730950488016887242097f;
}

public extension double
{
    public static const double sqrt2 = 1.4142135623730950488016887242097;
}

public struct WorldProjection {
    public float3x3 proj;
    public float3x3 view;
    public float3x3 inv;
}

public extension<T> Texture2D<T> where T : ITexelElement {
    [ForceInline]
    public uint2 imageSize2D() {
        uint2 imgSz;
        this.GetDimensions(imgSz.x, imgSz.y);
        return imgSz;
    }
}

public extension<T> RWTexture2D<T> where T : ITexelElement {
    [ForceInline]
    public uint2 imageSize2D() {
        uint2 imgSz;
        this.GetDimensions(imgSz.x, imgSz.y);
        return imgSz;
    }
}

public extension<T> WTexture2D<T> where T : ITexelElement {
    [ForceInline]
    public uint2 imageSize2D() {
        uint2 imgSz;
        this.GetDimensions(imgSz.x, imgSz.y);
        return imgSz;
    }
}

namespace interp {

[[ForceInline]]
public float pow_inout(float a, constexpr uint power) {
    let cond = a > 0.5f;
    let base = select(cond, a - 1.f, a) * 2.f;
    let powed = pow(base, power) * .5f;
    return copysign(powed, (power % 2 == 0 && cond) ? -1 : 1) + float(cond);
}

[[ForceInline]]
public float pow_out(float a, constexpr uint power) {
    return copysign(pow(1.0f - a, power), (power % 2 == 0) ? 1 : -1) + 1.0f;
}

}

[ForceInline]
public func curve<T>(T value, T srcFrom, T srcTo)->T where T : __BuiltinFloatingPointType {
    return (value - srcFrom) / (srcTo - srcFrom);
}

[ForceInline]
public func map<T>(T value, T srcFrom, T srcTo, T dstFrom, T dstTo)->T where T : IArithmetic {
    return (value - srcFrom) / (srcTo - srcFrom) * (dstTo - dstFrom) + dstFrom;
}

[ForceInline]
public func map<T>(T value, T srcFrom, T srcTo, T dstFrom, T dstTo)->T where T : __BuiltinFloatingPointType {
    return fma((value - srcFrom) / (srcTo - srcFrom), (dstTo - dstFrom), dstFrom);
}