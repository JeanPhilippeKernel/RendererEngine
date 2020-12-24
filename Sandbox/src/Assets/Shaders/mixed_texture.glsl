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
out float	mesh_index;
out float 	texture_slot_id;
out vec2 	texture_coord;

void main()
{	
	gl_Position 			= uniform_viewprojection * vec4(a_position, 1.0f);
	mesh_index				= a_index;
	texture_slot_id			= a_texture_slot_id;
	texture_coord 			= a_texture_coord;
}


#type fragment
#version  450 core

precision mediump float;

in float	mesh_index;
in float 	texture_slot_id;
in vec2 	texture_coord;


uniform sampler2D	uniform_texture_slot[32];

uniform float		interpolation_factor_0;
uniform float		interpolation_factor_1;
uniform float		interpolation_factor_2;
uniform float		interpolation_factor_3;
uniform float		interpolation_factor_4;
uniform float		interpolation_factor_5;
uniform float		interpolation_factor_6;
uniform float		interpolation_factor_7;
uniform float		interpolation_factor_8;
uniform float		interpolation_factor_9;
uniform float		interpolation_factor_10;
uniform float		interpolation_factor_11;
uniform float		interpolation_factor_12;
uniform float		interpolation_factor_13;
uniform float		interpolation_factor_14;
uniform float		interpolation_factor_15;
uniform float		interpolation_factor_16;
uniform float		interpolation_factor_17;
uniform float		interpolation_factor_18;
uniform float		interpolation_factor_19;
uniform float		interpolation_factor_20;
uniform float		interpolation_factor_21;
uniform float		interpolation_factor_22;
uniform float		interpolation_factor_23;
uniform float		interpolation_factor_24;
uniform float		interpolation_factor_25;
uniform float		interpolation_factor_26;
uniform float		interpolation_factor_27;
uniform float		interpolation_factor_28;
uniform float		interpolation_factor_29;
uniform float		interpolation_factor_30;
uniform float		interpolation_factor_31;

out vec4 output_color;

void main() 
{
	output_color = vec4(1.0f, 1.0f, 1.0f, 1.0f);
//    output_color = mix(
//	texture(uniform_texture_slot[0], texture_coord), 
//	texture(uniform_texture_slot[0 + 16], texture_coord), interpolation_factor_0);
}