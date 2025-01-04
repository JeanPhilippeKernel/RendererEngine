#version 460
layout (location = 0) in vec3 dir;
layout (location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform samplerCube EnvMap;

void main()
{
    outColor = texture(EnvMap, dir);
}