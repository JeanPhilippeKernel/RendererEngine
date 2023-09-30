#version 460

layout(location = 0) out vec3 uvw;
layout(location = 1) out vec3 v_worldNormal;
layout(location = 2) out vec4 v_worldPos;

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

layout(binding = 0) uniform  UniformBufferCamera { vec4 Position; mat4 View; mat4 Projection; } Camera;
layout(binding = 1) readonly buffer VertexSB { DrawVertex data[]; } VertexBuffer;
layout(binding = 2) readonly buffer IndexSB { uint   data[]; } IndexBuffer;
layout(binding = 3) readonly buffer DrawDataSB { DrawData data[]; } DrawDataBuffer;
layout(binding = 5) readonly buffer TransformSB { mat4 data[]; } TransformBuffer;

void main()
{
	DrawData dd = DrawDataBuffer.data[gl_BaseInstance];

	uint refIdx = IndexBuffer.data[dd.IndexOffset + gl_VertexIndex];
	DrawVertex v = VertexBuffer.data[refIdx + dd.VertexOffset];

	mat4 model = TransformBuffer.data[gl_BaseInstance];

	v_worldPos   = model * vec4(v.x, v.y, v.z, 1.0);
	v_worldNormal = transpose(inverse(mat3(model))) * vec3(v.nx, v.ny, v.nz);

	/* Assign shader outputs */
	gl_Position = Camera.Projection * Camera.View * v_worldPos;
    uvw = vec3(v.u, v.v, 1.0);
}
