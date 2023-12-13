#version 460
#extension GL_GOOGLE_include_directive : require
#include "vertex_common.glsl"


layout (location = 0) out vec3 dir;

void main()
{
    float cubeScale = 2000.0;

    DrawData dd = DrawDataBuffer.Data[gl_BaseInstance];

    uint refIdx = dd.IndexOffset + gl_VertexIndex;
    uint verIdx = IndexBuffer.Data[refIdx] + dd.VertexOffset;
    DrawVertex v = VertexBuffer.Data[verIdx];

    vec3 vertexPosition = vec3(v.x, v.y, v.z);
    gl_Position = Camera.Projection * Camera.View * vec4(cubeScale * vertexPosition, 1.0f);
    dir = vec3(vertexPosition.x, vertexPosition.y, vertexPosition.z);
}
