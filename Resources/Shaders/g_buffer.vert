#version 460
#extension GL_GOOGLE_include_directive : require
#include "vertex_common.glsl"


layout (location = 0) out vec3 FragmentPosition;
layout (location = 1) out vec3 WorldNormal;
layout (location = 2) out vec2 TextureCoord;
layout (location = 3) out flat uint MaterialIdx;
layout (location = 4) out vec4 ViewPosition;

void main()
{
    DrawDataView dataView = GetDrawDataView();

    vec4 worldPosition  = dataView.Transform * dataView.Vertex;
    FragmentPosition    = worldPosition.xyz;

    WorldNormal     = transpose(inverse(mat3(dataView.Transform))) * dataView.Normal;
    TextureCoord    = dataView.TexCoord;
    MaterialIdx     = dataView.MaterialId;
    ViewPosition    = Camera.Position;

    gl_Position = Camera.Projection * Camera.View * worldPosition;
}