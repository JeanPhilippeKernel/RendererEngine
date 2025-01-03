#version 460

layout (location = 0) in vec3 pos;

layout(set = 0, binding = 0) uniform UBCamera
{ 
    mat4 View;
    mat4 Projection;
    vec4 Position;
} Camera;

void main()
{
    gl_Position = vec4(pos, 1.0f);
}