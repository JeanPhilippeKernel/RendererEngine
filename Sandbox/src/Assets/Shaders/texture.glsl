#type vertex
#version 430 core
layout (location = 0) in vec3 a_Position;
layout (location = 1) in vec4 a_Color;
layout (location = 2) in vec2 a_Texture;

uniform mat4 u_ViewProjectionMat;
uniform mat4 u_TransformMat;

out vec4 v_Color;
out vec2 v_Texture;

void main()
{	
	gl_Position = u_ViewProjectionMat * u_TransformMat * vec4(a_Position, 1.0f);
	v_Color		= a_Color;
	v_Texture	= a_Texture;
}


#type fragment
#version  430 core

in vec4 v_Color;
in vec2 v_Texture;


out vec4 color;

uniform vec3 u_Color;
uniform sampler2D u_SamplerTex;

void main()
{
	//color =  vec4(v_Texture, 0.0f, 1.0f);
	color =  texture(u_SamplerTex, v_Texture);  //vec4(u_Color, 1.0f);
}


