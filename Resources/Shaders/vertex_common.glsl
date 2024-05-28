struct DrawVertex
{
    float x, y, z;
    float nx, ny, nz;
    float u, v;
};

struct DrawData
{
    uint TransformIndex;
    uint MaterialIndex;
    uint VertexOffset;
    uint IndexOffset;
    uint VertexCount;
    uint IndexCount;
};

layout(set = 0, binding = 0) uniform UBCamera
{ 
    mat4 View; 
    mat4 RotScaleView;
    mat4 Projection; 
    vec4 Position; 
} Camera;

layout(set = 0, binding = 1) readonly buffer VertexSB { DrawVertex Data[]; } VertexBuffer;
layout(set = 0, binding = 2) readonly buffer IndexSB { uint Data[]; } IndexBuffer;
layout(set = 0, binding = 3) readonly buffer DrawDataSB { DrawData Data[]; } DrawDataBuffer;
layout(set = 0, binding = 4) readonly buffer TransformSB { mat4 Data[]; } TransformBuffer;


DrawVertex FetchVertexData()
{
    DrawData dd = DrawDataBuffer.Data[gl_BaseInstance];

    uint refIdx = dd.IndexOffset + gl_VertexIndex;
    uint verIdx = IndexBuffer.Data[refIdx] + dd.VertexOffset;
    DrawVertex v = VertexBuffer.Data[verIdx];

    return v;
}

mat4 FetchTransform()
{
    return TransformBuffer.Data[gl_BaseInstance];
}