#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _13
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
    float2 m_4 [[user(locn0)]];
    float4 gl_Position [[position]];
};

vertex main0_out main0(constant _13& _5 [[buffer(0)]], uint gl_VertexIndex [[vertex_id]])
{
    main0_out out = {};
    out.gl_Position = float4(_5._m0.x + ((_5._m0.z - _5._m0.x) * float((22 >> int(gl_VertexIndex)) & 1)), _5._m0.y + ((_5._m0.w - _5._m0.y) * float((52 >> int(gl_VertexIndex)) & 1)), 0.0, 1.0);
    out.m_4 = float2(_5._m1.x + ((_5._m1.z - _5._m1.x) * float((22 >> int(gl_VertexIndex)) & 1)), _5._m1.y + ((_5._m1.w - _5._m1.y) * float((52 >> int(gl_VertexIndex)) & 1)));
    return out;
}

