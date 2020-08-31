#type vertex
#version 430 core

layout (location = 0) in vec3 a_Position;
layout (location = 1) in vec4 a_Color;

uniform mat4 u_ViewProjectionMat;

out vec4 v_Color;

void main()
{	
	gl_Position = u_ViewProjectionMat * vec4(a_Position, 1.0f);
	v_Color = a_Color;
}

#type fragment
#version  430 core

in vec4 v_Color;

void main()
{
	gl_FragColor = v_Color;
}



