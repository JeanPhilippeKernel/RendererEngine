#version 460
#extension GL_GOOGLE_include_directive : require
#include "vertex_common.glsl"

void main()
{
    DrawData dd = DrawDataBuffer.Data[gl_BaseInstance];

    uint refIdx = dd.IndexOffset + gl_VertexIndex;
    uint verIdx = IndexBuffer.Data[refIdx] + dd.VertexOffset;
    DrawVertex v = VertexBuffer.Data[verIdx];

    gl_Position = Camera.Projection * Camera.View * vec4(v.x, v.y, v.z, 1.0f);
}
