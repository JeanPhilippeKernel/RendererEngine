#type vertex
#version 450 core

precision mediump float;

layout (location = 0) in vec3 	a_position;
layout (location = 1) in vec2 	a_texture_coord;


//uniform variables
uniform mat4 uniform_viewprojection;


//output variables
out vec2 texture_coord;

void main()
{	
	gl_Position 	= uniform_viewprojection *  vec4(a_position, 1.0f);
	texture_coord 	= a_texture_coord;
}

#type fragment
#version  450 core

precision mediump float;

in vec2 texture_coord;

struct StandardMaterial 
{
	float tiling_factor;
	vec4 tint_color;
};

uniform StandardMaterial 	material;
uniform sampler2D 			uniform_texture;

// output variables
out vec4 output_color;

void main()
{
	output_color = texture(uniform_texture, texture_coord * material.tiling_factor) * material.tint_color;
}



 
