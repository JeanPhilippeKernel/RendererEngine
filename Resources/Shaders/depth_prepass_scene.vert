#version 460
#extension GL_GOOGLE_include_directive : require
#include "vertex_common.glsl"

void main()
{
    DrawVertex v = FetchVertexData();

	mat4 model = FetchTransform();
	vec4 worldPosition = model * vec4(v.x, v.y, v.z, 1.0);
    gl_Position = Camera.Projection * Camera.View * worldPosition;
}
