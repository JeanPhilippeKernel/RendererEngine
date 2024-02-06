#version 460
#extension GL_GOOGLE_include_directive : require
#include "vertex_common.glsl"

void main()
{
    DrawVertex v;
    GET_VERTEX_DATA(DrawDataBuffer, gl_BaseInstance, gl_VertexIndex, IndexBuffer, VertexBuffer, v);

	mat4 model = TransformBuffer.Data[gl_BaseInstance];
	vec4 worldPosition = model * vec4(v.x, v.y, v.z, 1.0);
    gl_Position = Camera.Projection * Camera.View * worldPosition;
}
