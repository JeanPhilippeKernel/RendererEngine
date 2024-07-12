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
    DrawDataView dataView   = GetDrawDataView();
	worldPos                = dataView.Transform * dataView.Vertex;
	worldNormal             = transpose(inverse(mat3(dataView.Transform))) * dataView.Normal;

	gl_Position     = Camera.Projection * Camera.View * worldPos;
    uvw             = vec3(dataView.TexCoord, 1.0);
    materialIdx     = dataView.MaterialId;
    CameraPosition  = Camera.Position;
}
