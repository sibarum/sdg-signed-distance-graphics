#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _15
{
    float4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
    float4 _m4;
    float4 _m5;
    float4 _m6;
    float4 _m7;
};

struct main0_out
{
    float4 m_3 [[color(0)]];
};

struct main0_in
{
    float2 m_2 [[user(locn0)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _15& _6 [[buffer(0)]], texture2d<float> _4 [[texture(0)]], texture2d<float> _5 [[texture(1)]], sampler _4Smplr [[sampler(0)]], sampler _5Smplr [[sampler(1)]])
{
    main0_out out = {};
    float2 _37 = float2((in.m_2.x - _6._m2.x) * _6._m2.z, (in.m_2.y - _6._m2.y) * _6._m2.z);
    float4 _38 = float4(0.0);
    if (((_37.x < 0.0) || (_37.y < 0.0)) || ((_37.x > _6._m3.z) || (_37.y > _6._m3.w)))
    {
    }
    else
    {
        float4 _39 = _4.sample(_4Smplr, float2((_6._m3.x + float2(fast::clamp(_37.x, 0.5, _6._m3.z - 0.5), fast::clamp(_37.y, 0.5, _6._m3.w - 0.5)).x) * _6._m4.x, (_6._m3.y + float2(fast::clamp(_37.x, 0.5, _6._m3.z - 0.5), fast::clamp(_37.y, 0.5, _6._m3.w - 0.5)).y) * _6._m4.y), level(0.0));
        float _40 = mix(((_39.x * (1.0 - step(0.5, abs(_6._m4.z - 0.0)))) + (_39.y * (1.0 - step(0.5, abs(_6._m4.z - 1.0))))) + ((_39.z * (1.0 - step(0.5, abs(_6._m4.z - 2.0)))) + (_39.w * (1.0 - step(0.5, abs(_6._m4.z - 3.0))))), fast::max(fast::min(_39.x, _39.y), fast::min(fast::max(_39.x, _39.y), _39.z)), step(3.5, _6._m4.z));
        float _41 = fast::clamp(0.5 + ((((_40 - 0.5) * _6._m2.w) / _6._m2.z) / _6._m4.w), 0.0, 1.0);
        float4 _42 = _6._m5;
        if (abs(_6._m6.x) > 0.0)
        {
            _42 = _6._m5 * _5.sample(_5Smplr, float2(fast::clamp((in.m_2.x * _6._m6.x) + _6._m6.z, _6._m7.x, _6._m7.z), fast::clamp((in.m_2.y * _6._m6.y) + _6._m6.w, _6._m7.y, _6._m7.w)), level(0.0));
        }
        _38 = float4(_42.x * (_41 * _42.w), _42.y * (_41 * _42.w), _42.z * (_41 * _42.w), _41 * _42.w);
    }
    out.m_3 = _38;
    return out;
}

