#version 460
#extension GL_GOOGLE_include_directive : require
#include "vertex_common.glsl"

layout (location = 0) out vec3 dir;

void main()
{
    DrawVertex v = FetchVertexData();

    dir = vec3(v.x, -v.y, v.z);
    vec4 position = Camera.Projection * Camera.RotScaleView * vec4(v.x, v.y, v.z, 1.0f);
    gl_Position = position.xyww;
}
