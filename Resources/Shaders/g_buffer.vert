#version 460
#extension GL_GOOGLE_include_directive : require
#include "vertex_common.glsl"


layout (location = 0) out vec3 FragmentPosition;
layout (location = 1) out vec3 WorldNormal;
layout (location = 2) out vec2 TextureCoord;
layout (location = 3) out flat uint MaterialIdx;

void main()
{
    DrawVertex v;
    GET_VERTEX_DATA(DrawDataBuffer, gl_BaseInstance, gl_VertexIndex, IndexBuffer, VertexBuffer, v);

    mat4 model = TransformBuffer.Data[gl_BaseInstance];
    vec4 worldPosition = model * vec4(v.x, v.y, v.z, 1.0);
    FragmentPosition = worldPosition.xyz;

    WorldNormal = transpose(inverse(mat3(model))) * vec3(v.nx, v.ny, v.nz);
    TextureCoord = vec2(v.u, v.v);
    MaterialIdx = DrawDataBuffer.Data[gl_BaseInstance].MaterialIndex;

    gl_Position = Camera.Projection * Camera.View * worldPosition;
}