#version 460
#extension GL_GOOGLE_include_directive : require
#include "vertex_common.glsl"

layout (location = 0) out vec2 TexCoord;
layout (location = 1) out vec3 WorldNormal;
layout (location = 2) out vec4 FragPos;
layout (location = 3) out flat uint MaterialIdx;

void main()
{
    DrawDataView dataView = GetDrawDataView();

    vec4 worldPos   = dataView.Transform * dataView.Vertex;
    WorldNormal     = transpose(inverse(mat3(dataView.Transform))) * dataView.Normal;
    TexCoord        = dataView.TexCoord;
    MaterialIdx     = dataView.MaterialId;
    FragPos         = worldPos;
    gl_Position     = Camera.Projection * Camera.View * worldPos;
}