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
#define MAX_SIZE 10

in float	mesh_index;
in float 	texture_slot_id;
in vec2 	texture_coord;


uniform sampler2D	uniform_texture_slot[MAX_SIZE];

uniform float		texture_tiling_factor[MAX_SIZE];
uniform vec4		texture_tint_color[MAX_SIZE];

// output variables
out vec4 output_color;

void main()
{
	output_color = texture2D(uniform_texture_slot[int(texture_slot_id)],  texture_coord * texture_tiling_factor[int(mesh_index)]) * texture_tint_color[int(mesh_index)];
}



 
