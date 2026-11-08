#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _14
{
    float4 _m0;
    float4 _m1;
    float4 _m2;
};

struct main0_out
{
    float4 m_3 [[color(0)]];
};

struct main0_in
{
    float2 m_2 [[user(locn0)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _14& _5 [[buffer(0)]], texture2d<float> _4 [[texture(0)]], sampler _4Smplr [[sampler(0)]])
{
    main0_out out = {};
    float4 _29 = _4.sample(_4Smplr, in.m_2, level(0.0));
    float4 _30 = _4.sample(_4Smplr, float2(in.m_2.x + _5._m2.x, in.m_2.y), level(0.0));
    out.m_3 = float4(_29.x * (_5._m2.z * mix(1.0, mix(((0.2125999927520751953125 * _30.x) + (0.715200006961822509765625 * _30.y)) + (0.072200000286102294921875 * _30.z), _30.w, step(1.5, _5._m2.y)), step(0.5, _5._m2.y))), _29.y * (_5._m2.z * mix(1.0, mix(((0.2125999927520751953125 * _30.x) + (0.715200006961822509765625 * _30.y)) + (0.072200000286102294921875 * _30.z), _30.w, step(1.5, _5._m2.y)), step(0.5, _5._m2.y))), _29.z * (_5._m2.z * mix(1.0, mix(((0.2125999927520751953125 * _30.x) + (0.715200006961822509765625 * _30.y)) + (0.072200000286102294921875 * _30.z), _30.w, step(1.5, _5._m2.y)), step(0.5, _5._m2.y))), _29.w * (_5._m2.z * mix(1.0, mix(((0.2125999927520751953125 * _30.x) + (0.715200006961822509765625 * _30.y)) + (0.072200000286102294921875 * _30.z), _30.w, step(1.5, _5._m2.y)), step(0.5, _5._m2.y))));
    return out;
}

