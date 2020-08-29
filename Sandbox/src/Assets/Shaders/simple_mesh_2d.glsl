#type vertex
#version 430 core

layout (location = 0) in vec3 a_Position;

//uniform variables
uniform mat4 uniform_viewprojection;
uniform mat4 uniform_transform;

void main()
{	
	gl_Position = uniform_viewprojection * uniform_transform * vec4(a_Position, 1.0f);
}

#type fragment
#version  430 core

//uniform variables
uniform vec4 uniform_color;
uniform sampler2D uniform_texture;

// output variables
out vec4 output_color;

void main()
{
	output_color = uniform_color;
}



