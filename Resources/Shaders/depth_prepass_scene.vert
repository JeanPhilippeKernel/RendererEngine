#version 460
#extension GL_GOOGLE_include_directive : require
#include "vertex_common.glsl"

void main()
{
    DrawDataView dataView   = GetDrawDataView();
	vec4 worldPosition      = dataView.Transform * dataView.Vertex;
    gl_Position = Camera.Projection * Camera.View * worldPosition;
}
