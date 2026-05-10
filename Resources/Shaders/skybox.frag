#version 460
layout(location = 0) in vec3 dir;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform textureCube EnvMap;
layout(set = 0, binding = 2) uniform sampler LinearClampToEdgeSampler;

void main()
{
    outColor = texture(samplerCube(EnvMap, LinearClampToEdgeSampler), normalize(dir));
}
