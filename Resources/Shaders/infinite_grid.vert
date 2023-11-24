#version 460

layout (location = 0) out vec2 uv;

float gridSize = 1000.0;

struct DrawVertex
{
    float x, y, z;
};

struct DrawData
{
	uint Index;
    uint TransformIndex;
    uint MaterialIndex;
    uint VertexOffset;
    uint IndexOffset;
    uint VertexCount;
    uint IndexCount;
};

layout(set = 0, binding = 0) uniform UBCamera { mat4 View; mat4 Projection; vec4 Position; } Camera;
layout(set = 0, binding = 1) readonly buffer VertexSB { DrawVertex Data[]; } VertexBuffer;
layout(set = 0, binding = 2) readonly buffer IndexSB { uint Data[]; } IndexBuffer;
layout(set = 0, binding = 3) readonly buffer DrawDataSB { DrawData Data[]; } DrawDataBuffer;

void main()
{
    DrawData dd = DrawDataBuffer.Data[gl_BaseInstance];

    uint refIdx = dd.IndexOffset + gl_VertexIndex;
    uint verIdx = IndexBuffer.Data[refIdx] + dd.VertexOffset;
    DrawVertex v = VertexBuffer.Data[verIdx];

    vec3 vpos = vec3(v.x, v.y, v.z) * gridSize;
    gl_Position = Camera.Projection * Camera.View * vec4(vpos, 1.0);
    uv = vpos.xz;
}