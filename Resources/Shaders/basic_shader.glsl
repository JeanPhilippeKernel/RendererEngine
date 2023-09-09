#type vertex
#version 450 core

precision mediump float;

/*
 * Uniform global variables
 */

layout(std140, binding = 0) uniform ViewProjectionMatrices
{
	mat4 view;
	mat4 projection;
};

uniform mat4 model;

/*
 * Vertex input variables
 */
layout (location = 0) in vec3 a_position;
layout (location = 1) in vec3 a_normal;
layout (location = 2) in vec2 a_texture_coord;

/*
 * Vertex output variables
 */
out vec2 texture_coord;

void main()
{
	gl_Position = projection * view * model * vec4(a_position, 1.0f);
	texture_coord = a_texture_coord;
}

#type fragment
#version 450 core

precision mediump float;

/*
 * Fragment input variables
 */
in vec2 			texture_coord;
uniform sampler2D	texture_sampler;

/*
 * Fragment output variables
 */
out vec4 output_color;

void main()
{
	output_color = texture(texture_sampler, texture_coord);
}