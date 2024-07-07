#version 460
#extension GL_GOOGLE_include_directive : require
#include "fragment_common.glsl"

layout (location = 0) in vec2 TexCoord;
layout (location = 1) in vec4 ViewPos;

layout (std140, set = 0, binding = 6) readonly buffer DirectionalLightSB
{
    uint Count;
    DirectionalLight Data[];
} 	DirectionalLightBuffer;

layout (std140, set = 0, binding = 7) readonly buffer PointLightSB
{
    uint Count;
    PointLight Data[];
} PointLightBuffer;

layout (std140, set = 0, binding = 8) readonly buffer SpotLightSB
{
    uint Count;
    SpotLight Data[];
} SpotLightBuffer;

layout (set = 0, binding = 10) uniform sampler2D AlbedoSampler;
layout (set = 0, binding = 11) uniform sampler2D PositionSampler;
layout (set = 0, binding = 12) uniform sampler2D NormalSampler;
layout (set = 0, binding = 13) uniform sampler2D SpecularSampler;


layout (location = 0) out vec4 OutColor;


vec3 ComputeDirectionalLight(DirectionalLight light, vec3 normal, vec3 viewDir, vec3 albedoMap, vec3 specularMap)
{
    vec3 direction  = light.Direction.xyz;

    vec3 lightDir   = normalize(direction);
    vec3 ambient    = 0.1 * light.Ambient.xyz;

    float diff      = max(dot(normal, lightDir), 0.0);
    vec3 diffuse    = diff * light.Diffuse.xyz * albedoMap;

    vec3 reflectDir = reflect(-lightDir, normal);
    float spec      = pow(max(dot(viewDir, reflectDir), 0.0), 16);
    vec3 specular   = 0.5 * spec * light.Specular.xyz * specularMap;

    return vec3(ambient + diffuse + specular);
}


void main()
{
    vec3 norm       = texture( NormalSampler, TexCoord).rgb;
    vec4 fragPos    = texture( PositionSampler, TexCoord);
    vec3 albedo     = texture( AlbedoSampler, TexCoord).rgb;
    vec3 specular   = texture( SpecularSampler, TexCoord).rgb;

    vec3 viewDir    = normalize( ViewPos.xyz - fragPos.xyz);

    //Computing Directional Lights
    vec3 lighting = vec3(0.0);
    for (uint i = 0;  i < DirectionalLightBuffer.Count; ++i)
    {
        DirectionalLight directionalLight = DirectionalLightBuffer.Data[i];
        lighting += ComputeDirectionalLight(directionalLight, norm, viewDir, albedo, specular);
    }

    OutColor = vec4(lighting, 1.0);
}