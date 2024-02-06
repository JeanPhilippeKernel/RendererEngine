#version 460
#extension GL_GOOGLE_include_directive : require
#include "vertex_common.glsl"

void main()
{
    const float scaleFactor = 100.0f;

    DrawVertex v;
    GET_VERTEX_DATA(DrawDataBuffer, gl_BaseInstance, gl_VertexIndex, IndexBuffer, VertexBuffer, v);

    vec3 posScale = vec3(v.x, v.y, v.z) * scaleFactor;
    gl_Position = Camera.Projection * Camera.View * vec4(posScale, 1.0f);
}