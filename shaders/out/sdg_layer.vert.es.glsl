#version 300
#ifdef GL_ARB_shading_language_420pack
#extension GL_ARB_shading_language_420pack : require
#endif
#extension GL_ARB_separate_shader_objects : require

out gl_PerVertex
{
    vec4 gl_Position;
};

struct _13
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

uniform _13 _5;

layout(location = 0) out vec2 _4;

void main()
{
    gl_Position = vec4(_5._m0.x + ((_5._m0.z - _5._m0.x) * float((22 >> gl_VertexID) & 1)), _5._m0.y + ((_5._m0.w - _5._m0.y) * float((52 >> gl_VertexID) & 1)), 0.0, 1.0);
    _4 = vec2(_5._m1.x + ((_5._m1.z - _5._m1.x) * float((22 >> gl_VertexID) & 1)), _5._m1.y + ((_5._m1.w - _5._m1.y) * float((52 >> gl_VertexID) & 1)));
}

