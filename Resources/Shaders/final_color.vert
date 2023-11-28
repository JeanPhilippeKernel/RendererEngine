#version 460
#extension GL_GOOGLE_include_directive : require
#include "vertex_common.glsl"

layout(location = 0) out vec3 uvw;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec4 worldPos;
layout(location = 3) out flat uint materialIdx;
layout(location = 4) out vec4 CameraPosition;

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
    uvw = vec3(v.u, v.v, 1.0);
    materialIdx = dd.MaterialIndex;
    CameraPosition = Camera.Position;
}
