#type vertex
#version 450 core

layout (location = 0) in vec3 	a_position;
layout (location = 1) in vec4 	a_color;

layout (location = 2) in float 	a_texture_slot_id;
layout (location = 3) in vec2 	a_texture_coord;
layout (location = 4) in float	a_texture_tiling_factor;
layout (location = 5) in vec4	a_texture_tint_color;

//uniform variables
uniform mat4 uniform_viewprojection;


//output variables
out float 	texture_slot_id;
out vec2 	texture_coord;
out float	texture_tiling_factor;
out vec4	texture_tint_color;

void main()
{	
	gl_Position 			= uniform_viewprojection * vec4(a_position, 1.0f);
	texture_coord 			= a_texture_coord;
	texture_slot_id			= a_texture_slot_id;
	texture_tint_color		= a_texture_tint_color;
	texture_tiling_factor 	= a_texture_tiling_factor;
}

#type fragment
#version  450 core

#define MAX_TEXTURE_SLOT  32

in float 	texture_slot_id;
in vec2 	texture_coord;
in float	texture_tiling_factor;
in vec4		texture_tint_color;


//uniform variables
uniform sampler2D	uniform_texture_slot[MAX_TEXTURE_SLOT];

// output variables
out vec4 output_color;

void main()
{
	output_color = texture(
		uniform_texture_slot[int(texture_slot_id)], texture_coord * texture_tiling_factor) * texture_tint_color;
}


 