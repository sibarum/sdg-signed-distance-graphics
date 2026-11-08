cbuffer _13_5
{
    float4 _5_m0 : packoffset(c0);
    float4 _5_m1 : packoffset(c1);
    float4 _5_m2 : packoffset(c2);
};


static float4 gl_Position;
static int gl_VertexIndex;
static float2 _4;

struct SPIRV_Cross_Input
{
    uint gl_VertexIndex : SV_VertexID;
};

struct SPIRV_Cross_Output
{
    float2 _4 : TEXCOORD0;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    gl_Position = float4(_5_m0.x + ((_5_m0.z - _5_m0.x) * float((22 >> gl_VertexIndex) & 1)), _5_m0.y + ((_5_m0.w - _5_m0.y) * float((52 >> gl_VertexIndex) & 1)), 0.0f, 1.0f);
    _4 = float2(_5_m1.x + ((_5_m1.z - _5_m1.x) * float((22 >> gl_VertexIndex) & 1)), _5_m1.y + ((_5_m1.w - _5_m1.y) * float((52 >> gl_VertexIndex) & 1)));
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_VertexIndex = int(stage_input.gl_VertexIndex);
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output._4 = _4;
    return stage_output;
}
