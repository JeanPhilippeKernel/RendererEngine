struct DrawVertex
{
    float x, y, z;
    float nx, ny, nz;
    float u, v;
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


#define GET_VERTEX_DATA(ddBuffer, baseInstance, vertexIndex, indexBuffer, vertexBuffer, resultVertex) \
    DrawData dd = ddBuffer.Data[baseInstance]; \
    uint refIdx = dd.IndexOffset + vertexIndex; \
    uint verIdx = indexBuffer.Data[refIdx] + dd.VertexOffset; \
    resultVertex = vertexBuffer.Data[verIdx];

layout(set = 0, binding = 0) uniform UBCamera { mat4 View; mat4 Projection; vec4 Position; } Camera;

layout(set = 0, binding = 1) readonly buffer VertexSB { DrawVertex Data[]; } VertexBuffer;
layout(set = 0, binding = 2) readonly buffer IndexSB { uint Data[]; } IndexBuffer;
layout(set = 0, binding = 3) readonly buffer DrawDataSB { DrawData Data[]; } DrawDataBuffer;
layout(set = 0, binding = 4) readonly buffer TransformSB { mat4 Data[]; } TransformBuffer;
