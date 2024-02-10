#version 460
#extension GL_GOOGLE_include_directive : require
#include "vertex_common.glsl"

layout (location = 0) out vec3 dir;

void main()
{
    const float cubeScale = 2000.0;

    DrawVertex v = FetchVertexData();

    dir = vec3(v.x, -v.y, v.z);

    vec3 posScale = cubeScale * vec3(v.x, v.y, v.z);
    gl_Position = Camera.Projection * Camera.View * vec4(posScale, 1.0f);
}
