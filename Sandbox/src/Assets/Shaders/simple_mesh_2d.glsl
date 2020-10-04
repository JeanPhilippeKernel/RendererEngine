#type vertex
#version 450 core

layout (location = 0) in vec3 	a_position;
layout (location = 1) in vec4 	a_color;
layout (location = 2) in vec2 	a_texture_coord;
layout (location = 3) in float 	a_texture_id;

//uniform variables
uniform mat4 uniform_viewprojection;
uniform mat4 uniform_transform;



//output variables
out vec2 	texture_coord;
out float 	texture_id;


void main()
{	
	gl_Position 	= uniform_viewprojection * vec4(a_position, 1.0f);
	//gl_Position 	= uniform_viewprojection * uniform_transform * vec4(a_position, 1.0f);
	texture_coord 	= a_texture_coord;
	texture_id		= a_texture_id;
}

#type fragment
#version  450 core

in vec2 	texture_coord;
in float 	texture_id;


//uniform variables
uniform sampler2D 		uniform_texture[2];
uniform vec4 			uniform_tex_tint_color;
uniform float 			uniform_tex_tiling_factor;

// output variables
out vec4 output_color;

void main()
{
	int id =  int(texture_id);
	output_color = texture(uniform_texture[id], texture_coord * uniform_tex_tiling_factor) * uniform_tex_tint_color;
	//output_color = texture(uniform_texture[texture_id], texture_coord * 1.0) * vec4(1.0f, 1.0f, 1.0f, 1.0f);
}


 
