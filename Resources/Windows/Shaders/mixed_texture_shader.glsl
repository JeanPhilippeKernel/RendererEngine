#type vertex
#version 450 core

precision mediump float;

/*
 * Vertex global variables
 */
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

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
#version  450 core

precision mediump float;

struct MixedMaterial
{
	float interpolation_factor;
};

/*
 * Fragment input variables
 */
in vec2 texture_coord;

uniform MixedMaterial 	material;
uniform sampler2D 		texture_sampler_0;
uniform sampler2D 		texture_sampler_1;

/*
 * Fragment output variables
 */
out vec4 output_color;

void main()
{
   output_color = mix(
	texture(texture_sampler_0, texture_coord), 
	texture(texture_sampler_1, texture_coord), material.interpolation_factor);
}