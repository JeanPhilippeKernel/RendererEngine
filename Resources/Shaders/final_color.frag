#version 460

layout(location = 0) in vec3 uvw;

layout(location = 0) out vec4 outColor;

void main()
{
	outColor = vec4(uvw, 0.5);
}
