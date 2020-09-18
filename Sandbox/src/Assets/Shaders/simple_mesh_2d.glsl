#type vertex
#version 450 core

layout (location = 0) in vec3 a_position;
layout (location = 1) in vec4 a_color;
layout (location = 2) in vec3 a_texture_coord;

//uniform variables
uniform mat4 uniform_viewprojection;
uniform mat4 uniform_transform;



//output variables
out vec3 texture_coord;


void main()
{	
	gl_Position 	= uniform_viewprojection * vec4(a_position, 1.0f);
	//gl_Position 	= uniform_viewprojection * uniform_transform * vec4(a_position, 1.0f);
	texture_coord 	= a_texture_coord;
}

#type fragment
#version  450 core

in vec3 texture_coord;

//uniform variables
uniform sampler2D 		uniform_texture[2];
uniform vec4 			uniform_tex_tint_color;
uniform float 			uniform_tex_tiling_factor;

// output variables
out vec4 output_color;

void main()
{
	int texture_id = int(texture_coord.z);
	output_color = texture(uniform_texture[texture_id], vec2(texture_coord.x, texture_coord.y) * uniform_tex_tiling_factor) * uniform_tex_tint_color;
	//output_color = texture(uniform_texture[texture_id], texture_coord * 1.0) * vec4(1.0f, 1.0f, 1.0f, 1.0f);
}


 
