#version 460

layout(location = 0) out vec3 uvw;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec4 worldPos;
layout(location = 3) out flat uint materialIdx;
layout(location = 4) out vec4 CameraPosition;

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

layout(set = 0, binding = 0) uniform UBCamera { mat4 View; mat4 Projection; vec4 Position; } Camera;
layout(set = 0, binding = 1) readonly buffer VertexSB { DrawVertex Data[]; } VertexBuffer;
layout(set = 0, binding = 2) readonly buffer IndexSB { uint Data[]; } IndexBuffer;
layout(set = 0, binding = 3) readonly buffer DrawDataSB { DrawData Data[]; } DrawDataBuffer;
layout(set = 0, binding = 4) readonly buffer TransformSB { mat4 Data[]; } TransformBuffer;

void main()
{
	DrawData dd = DrawDataBuffer.Data[gl_BaseInstance];

    uint refIdx = dd.IndexOffset + gl_VertexIndex;
    uint verIdx = IndexBuffer.Data[refIdx] + dd.VertexOffset;
    DrawVertex v = VertexBuffer.Data[verIdx];

	mat4 model = TransformBuffer.Data[gl_BaseInstance];
	worldPos   = model * vec4(v.x, v.y, v.z, 1.0);
	worldNormal = transpose(inverse(mat3(model))) * vec3(v.nx, v.ny, v.nz);

	gl_Position = Camera.Projection * Camera.View * worldPos;
    //gl_Position.z = 0
    uvw = vec3(v.u, v.v, 1.0);
    materialIdx = dd.MaterialIndex;
    CameraPosition = Camera.Position;
}
