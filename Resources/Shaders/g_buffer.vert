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
    DrawVertex v = FetchVertexData();
    mat4 model = FetchTransform();

    vec4 worldPosition = model * vec4(v.x, v.y, v.z, 1.0);
    FragmentPosition = worldPosition.xyz;

    WorldNormal = transpose(inverse(mat3(model))) * vec3(v.nx, v.ny, v.nz);
    TextureCoord = vec2(v.u, v.v);
    MaterialIdx = DrawDataBuffer.Data[gl_BaseInstance].MaterialIndex;
    ViewPosition = Camera.Position;

    gl_Position = Camera.Projection * Camera.View * worldPosition;
}