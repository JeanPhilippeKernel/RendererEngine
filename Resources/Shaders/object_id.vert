#version 460
#extension GL_GOOGLE_include_directive : require
#include "draw.glsl"
layout(location = 0) out vec2 TexCoord;
layout(location = 1) out flat uint MaterialIdx;
layout(location = 2) out flat uint PickToken;
void main()
{
    DrawDataView data = GetDrawDataView();
    TexCoord          = data.TexCoord;
    MaterialIdx       = data.MaterialId;
    PickToken         = FetchDrawData().PickToken;
    gl_Position       = Camera.Projection * Camera.View * data.Transform * data.Vertex;
}
