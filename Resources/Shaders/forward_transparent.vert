#version 460
#extension GL_GOOGLE_include_directive : require
#include "draw.glsl"
layout(location = 0) out vec2 TexCoord;
layout(location = 1) out vec3 WorldNormal;
layout(location = 2) out flat uint MaterialIdx;
layout(location = 3) out vec3 WorldPos;
void main()
{
    DrawDataView data  = GetDrawDataView();
    vec4         world = data.Transform * data.Vertex;
    TexCoord           = data.TexCoord;
    WorldNormal        = transpose(inverse(mat3(data.Transform))) * data.Normal;
    MaterialIdx        = data.MaterialId;
    WorldPos           = world.xyz;
    gl_Position        = Camera.Projection * Camera.View * world;
}
