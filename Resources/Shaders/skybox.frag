#version 460
layout(location = 0) in vec3 dir;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform textureCube EnvMap;
layout(set = 0, binding = 2) uniform sampler LinearClampToEdgeSampler;

layout(push_constant) uniform EnvironmentLightingPushConstants
{
    vec4  TintIntensity;
    float YawRadians;
    float SpecularMaxLod;
    vec2  Padding;
}
Environment;

void main()
{
    float cosine    = cos(Environment.YawRadians);
    float sine      = sin(Environment.YawRadians);
    vec3  direction = normalize(dir);
    direction       = vec3(cosine * direction.x + sine * direction.z, direction.y, -sine * direction.x + cosine * direction.z);
    outColor        = vec4(texture(samplerCube(EnvMap, LinearClampToEdgeSampler), direction).rgb * Environment.TintIntensity.rgb, 1.0);
}
