#version 450 core

precision mediump float;

/*
 * Vertex input variables
 */
layout (location = 0) in vec3 a_position;
layout (location = 1) in vec3 a_normal;
layout (location = 2) in vec2 a_texture_coord;

/*
 * Uniform global variables
 */
layout (binding = 0) uniform CameraProperties
{
	vec4 Position;
	mat4 View;
	mat4 Projection;
} Camera;

layout (binding = 1) uniform ModelProperties
{ 
	mat4 LocalTransform;
} Model;

/*
 * Vertex output variables
 */
layout (location = 0) out vec3 out_normal;
layout (location = 1) out vec2 out_texture_coord;

void main()
{
	gl_Position = Camera.Projection * Camera.View * (Model.LocalTransform * vec4(a_position, 1.0f));
	out_normal = a_normal;
	out_texture_coord = a_texture_coord;
}
