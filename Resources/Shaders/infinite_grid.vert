#version 460
#extension GL_GOOGLE_include_directive : require
#include "vertex_common.glsl"

layout (location = 0) out vec2 uv;

float gridSize = 1000.0;

void main()
{
    DrawData dd = DrawDataBuffer.Data[gl_BaseInstance];

    uint refIdx = dd.IndexOffset + gl_VertexIndex;
    uint verIdx = IndexBuffer.Data[refIdx] + dd.VertexOffset;
    DrawVertex v = VertexBuffer.Data[verIdx];

    vec3 vpos = vec3(v.x, v.y, v.z) * gridSize;
    gl_Position = Camera.Projection * Camera.View * vec4(vpos, 1.0);
    uv = vpos.xz;
}