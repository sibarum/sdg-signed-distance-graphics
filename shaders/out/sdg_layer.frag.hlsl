cbuffer _15_6
{
    float4 _6_m0 : packoffset(c0);
    float4 _6_m1 : packoffset(c1);
    float4 _6_m2 : packoffset(c2);
    float4 _6_m3 : packoffset(c3);
    float4 _6_m4 : packoffset(c4);
    float4 _6_m5 : packoffset(c5);
    float4 _6_m6 : packoffset(c6);
    float4 _6_m7 : packoffset(c7);
};

Texture2D<float4> _4 : register(t0);
SamplerState __4_sampler : register(s0);
Texture2D<float4> _5 : register(t1);
SamplerState __5_sampler : register(s1);

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
    float2 _37 = float2((_2.x - _6_m2.x) * _6_m2.z, (_2.y - _6_m2.y) * _6_m2.z);
    float4 _38 = 0.0f.xxxx;
    if (((_37.x < 0.0f) || (_37.y < 0.0f)) || ((_37.x > _6_m3.z) || (_37.y > _6_m3.w)))
    {
    }
    else
    {
        float4 _39 = _4.SampleLevel(__4_sampler, float2((_6_m3.x + float2(clamp(_37.x, 0.5f, _6_m3.z - 0.5f), clamp(_37.y, 0.5f, _6_m3.w - 0.5f)).x) * _6_m4.x, (_6_m3.y + float2(clamp(_37.x, 0.5f, _6_m3.z - 0.5f), clamp(_37.y, 0.5f, _6_m3.w - 0.5f)).y) * _6_m4.y), 0.0f);
        float _40 = lerp(((_39.x * (1.0f - step(0.5f, abs(_6_m4.z - 0.0f)))) + (_39.y * (1.0f - step(0.5f, abs(_6_m4.z - 1.0f))))) + ((_39.z * (1.0f - step(0.5f, abs(_6_m4.z - 2.0f)))) + (_39.w * (1.0f - step(0.5f, abs(_6_m4.z - 3.0f))))), max(min(_39.x, _39.y), min(max(_39.x, _39.y), _39.z)), step(3.5f, _6_m4.z));
        float _41 = clamp(0.5f + ((((_40 - 0.5f) * _6_m2.w) / _6_m2.z) / _6_m4.w), 0.0f, 1.0f);
        float4 _42 = _6_m5;
        if (abs(_6_m6.x) > 0.0f)
        {
            _42 = _6_m5 * _5.SampleLevel(__5_sampler, float2(clamp((_2.x * _6_m6.x) + _6_m6.z, _6_m7.x, _6_m7.z), clamp((_2.y * _6_m6.y) + _6_m6.w, _6_m7.y, _6_m7.w)), 0.0f);
        }
        _38 = float4(_42.x * (_41 * _42.w), _42.y * (_41 * _42.w), _42.z * (_41 * _42.w), _41 * _42.w);
    }
    _3 = _38;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _2 = stage_input._2;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output._3 = _3;
    return stage_output;
}
