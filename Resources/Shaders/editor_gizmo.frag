#version 460

layout(location = 0) in vec4 OutColor;
layout(location = 0) out vec4 FragmentColor;

void main()
{
    FragmentColor = OutColor;
}
