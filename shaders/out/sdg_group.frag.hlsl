cbuffer _14_5
{
    float4 _5_m0 : packoffset(c0);
    float4 _5_m1 : packoffset(c1);
    float4 _5_m2 : packoffset(c2);
};

Texture2D<float4> _4 : register(t0);
SamplerState __4_sampler : register(s0);

static float2 _2;
static float4 _3;

struct SPIRV_Cross_Input
{
    float2 _2 : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float4 _3 : SV_Target0;
};

void frag_main()
{
    float4 _29 = _4.SampleLevel(__4_sampler, _2, 0.0f);
    float4 _30 = _4.SampleLevel(__4_sampler, float2(_2.x + _5_m2.x, _2.y), 0.0f);
    _3 = float4(_29.x * (_5_m2.z * lerp(1.0f, lerp(((0.2125999927520751953125f * _30.x) + (0.715200006961822509765625f * _30.y)) + (0.072200000286102294921875f * _30.z), _30.w, step(1.5f, _5_m2.y)), step(0.5f, _5_m2.y))), _29.y * (_5_m2.z * lerp(1.0f, lerp(((0.2125999927520751953125f * _30.x) + (0.715200006961822509765625f * _30.y)) + (0.072200000286102294921875f * _30.z), _30.w, step(1.5f, _5_m2.y)), step(0.5f, _5_m2.y))), _29.z * (_5_m2.z * lerp(1.0f, lerp(((0.2125999927520751953125f * _30.x) + (0.715200006961822509765625f * _30.y)) + (0.072200000286102294921875f * _30.z), _30.w, step(1.5f, _5_m2.y)), step(0.5f, _5_m2.y))), _29.w * (_5_m2.z * lerp(1.0f, lerp(((0.2125999927520751953125f * _30.x) + (0.715200006961822509765625f * _30.y)) + (0.072200000286102294921875f * _30.z), _30.w, step(1.5f, _5_m2.y)), step(0.5f, _5_m2.y))));
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _2 = stage_input._2;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output._3 = _3;
    return stage_output;
}
