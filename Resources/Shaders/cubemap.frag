#version 460

layout (location = 0) in vec3 dir;

layout (location = 0) out vec4 outColor;

layout(set = 0, binding = 5) uniform samplerCube CubemapTexture;

void main()
{
    outColor = texture(CubemapTexture, dir);
}