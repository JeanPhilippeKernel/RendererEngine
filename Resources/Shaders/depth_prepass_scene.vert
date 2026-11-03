#version 460
#extension GL_GOOGLE_include_directive : require
#include "draw.glsl"
layout(location = 0) out vec2 TexCoord;
layout(location = 1) out flat uint MaterialIdx;

void main()
{
    DrawDataView dataView = GetDrawDataView();
    TexCoord              = dataView.TexCoord;
    MaterialIdx           = dataView.MaterialId;
    vec4 worldPosition    = dataView.Transform * dataView.Vertex;
    gl_Position           = Camera.Projection * Camera.View * worldPosition;
}
