#type vertex
#version 450 core

precision mediump float;

layout (location = 0) in float 	a_index;
layout (location = 1) in vec3 	a_position;
layout (location = 2) in vec4 	a_color;

layout (location = 3) in float 	a_texture_slot_id;
layout (location = 4) in vec2 	a_texture_coord;


//uniform variables
uniform mat4 uniform_viewprojection;


//output variables
out vec2 texture_coord;

void main()
{	
	gl_Position 	= uniform_viewprojection * vec4(a_position, 1.0f);
	texture_coord 	= a_texture_coord;
}


#type fragment
#version  450 core

precision mediump float;

in vec2 texture_coord;

struct MixedMaterial 
{
	float interpolation_factor;
};

uniform MixedMaterial 	material;
uniform sampler2D 		uniform_texture_0;
uniform sampler2D 		uniform_texture_1;

out vec4 output_color;

void main() 
{
   output_color = mix(
	texture(uniform_texture_0, texture_coord), 
	texture(uniform_texture_1, texture_coord), material.interpolation_factor);
}