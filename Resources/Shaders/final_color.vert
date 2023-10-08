#version 460

layout(location = 0) out vec3 uvw;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec4 worldPos;

struct DrawVertex
{
    float x, y, z;
    float u, v;
    float nx, ny, nz;
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

layout(binding = 0) uniform UBCamera { vec4 Position; mat4 View; mat4 Projection; } Camera;

layout(binding = 1) readonly buffer VertexSB { DrawVertex Data[]; } VertexBuffer;
layout(binding = 2) readonly buffer IndexSB { uint Data[]; } IndexBuffer;
layout(binding = 3) readonly buffer DrawDataSB { DrawData Data[]; } DrawDataBuffer;
layout(binding = 5) readonly buffer TransformSB { mat4 Data[]; } TransformBuffer;

void main()
{
	DrawData dd = DrawDataBuffer.Data[gl_BaseInstance];

	uint refIdx =  dd.IndexOffset + gl_VertexIndex;
	DrawVertex v = VertexBuffer.Data[IndexBuffer.Data[refIdx] + dd.VertexOffset];

	mat4 model = TransformBuffer.Data[gl_BaseInstance];
	worldPos   = model * vec4(v.x, v.y, v.z, 1.0);
	worldNormal = transpose(inverse(mat3(model))) * vec3(v.nx, v.ny, v.nz);

	gl_Position = Camera.Projection * Camera.View * worldPos;
    uvw = vec3(v.u, v.v, 1.0);
}
