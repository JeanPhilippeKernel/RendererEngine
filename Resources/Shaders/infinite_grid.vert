#version 460
#extension GL_GOOGLE_include_directive : require
#include "vertex_common.glsl"

layout (location = 0) out vec2 uv;
layout (location = 1) out float scaleFactor;


void main()
{
    scaleFactor = 300.0;

    DrawVertex v =  FetchVertexData();
    vec3 posScale = vec3(v.x, v.y, v.z) * scaleFactor;
    uv = posScale.xz;
    gl_Position = Camera.Projection * Camera.View * vec4(posScale, 1.0);
}