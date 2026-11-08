#version 300
#ifdef GL_ARB_shading_language_420pack
#extension GL_ARB_shading_language_420pack : require
#endif
#extension GL_ARB_separate_shader_objects : require

struct _15
{
    vec4 _m0;
    vec4 _m1;
    vec4 _m2;
    vec4 _m3;
    vec4 _m4;
    vec4 _m5;
    vec4 _m6;
    vec4 _m7;
};

uniform _15 _6;

layout(binding = 0) uniform sampler2D _4;
layout(binding = 1) uniform sampler2D _5;

layout(location = 0) in vec2 _2;
out vec4 _3;

void main()
{
    vec2 _37 = vec2((_2.x - _6._m2.x) * _6._m2.z, (_2.y - _6._m2.y) * _6._m2.z);
    vec4 _38 = vec4(0.0);
    if (((_37.x < 0.0) || (_37.y < 0.0)) || ((_37.x > _6._m3.z) || (_37.y > _6._m3.w)))
    {
    }
    else
    {
        vec4 _39 = textureLod(_4, vec2((_6._m3.x + vec2(clamp(_37.x, 0.5, _6._m3.z - 0.5), clamp(_37.y, 0.5, _6._m3.w - 0.5)).x) * _6._m4.x, (_6._m3.y + vec2(clamp(_37.x, 0.5, _6._m3.z - 0.5), clamp(_37.y, 0.5, _6._m3.w - 0.5)).y) * _6._m4.y), 0.0);
        float _40 = mix(((_39.x * (1.0 - step(0.5, abs(_6._m4.z - 0.0)))) + (_39.y * (1.0 - step(0.5, abs(_6._m4.z - 1.0))))) + ((_39.z * (1.0 - step(0.5, abs(_6._m4.z - 2.0)))) + (_39.w * (1.0 - step(0.5, abs(_6._m4.z - 3.0))))), max(min(_39.x, _39.y), min(max(_39.x, _39.y), _39.z)), step(3.5, _6._m4.z));
        float _41 = clamp(0.5 + ((((_40 - 0.5) * _6._m2.w) / _6._m2.z) / _6._m4.w), 0.0, 1.0);
        vec4 _42 = _6._m5;
        if (abs(_6._m6.x) > 0.0)
        {
            _42 = _6._m5 * textureLod(_5, vec2(clamp((_2.x * _6._m6.x) + _6._m6.z, _6._m7.x, _6._m7.z), clamp((_2.y * _6._m6.y) + _6._m6.w, _6._m7.y, _6._m7.w)), 0.0);
        }
        _38 = vec4(_42.x * (_41 * _42.w), _42.y * (_41 * _42.w), _42.z * (_41 * _42.w), _41 * _42.w);
    }
    _3 = _38;
}

