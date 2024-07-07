#version 460
#extension GL_GOOGLE_include_directive : require
#include "vertex_common.glsl"

layout (location = 0) out vec2 TexCoord;
layout (location = 1) out vec4 ViewPos;

void main()
{
    DrawDataView dataView = GetDrawDataView();

    vec4 worldPos   = dataView.Transform * dataView.Vertex;
    ViewPos         = Camera.Position;
    gl_Position     = Camera.Projection * Camera.View * worldPos;
    // Convert gl_Position from NDC [-1, 1] to texture coordinates [0, 1]
    TexCoord        = (gl_Position.xy / gl_Position.w) * 0.5 + 0.5;
}