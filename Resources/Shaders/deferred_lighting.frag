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
    vec3 ambient    = light.Ambient.xyz * albedoMap;

    float diff      = max(dot(normal, lightDir), 0.0);
    vec3 diffuse    = diff * light.Diffuse.xyz * albedoMap;

    vec3 reflectDir = reflect(-lightDir, normal);
    float spec      = pow(max(dot(viewDir, reflectDir), 0.0), 16);
    vec3 specular   = spec * light.Specular.xyz * specularMap;

    return vec3(ambient + diffuse + specular);
}

vec3 ComputePointLight(PointLight light, vec3 normal, vec3 viewDir, vec4 fragPos, vec3 albedoMap, vec3 specularMap)
{
    float dist          = length(light.Position.xyz - fragPos.xyz);
    float attenuation   = 1.0 / ( light.Constant + (light.Linear * dist) + (light.Quadratic * (dist * dist)) );

    vec3 lightDir       = normalize(light.Position.xyz - fragPos.xyz);
    vec3 ambient        = light.Ambient.xyz * albedoMap;

    float diff          = max(dot(normal, lightDir), 0.0);
    vec3 diffuse        = diff * light.Diffuse.xyz * albedoMap;

    vec3 reflectDir     = reflect(-lightDir, normal);
    float spec          = pow(max(dot(viewDir, reflectDir), 0.0), 16); // todo : 16 should be replaced by material.shininess
    vec3 specular       = spec * light.Specular.xyz * specularMap;

    ambient     *= attenuation;
    diffuse     *= attenuation;
    specular    *= attenuation;

    return vec3(ambient + diffuse + specular);
}

vec3 ComputeSpotLight(SpotLight light, vec3 normal, vec3 viewDir, vec3 fragPos, vec3 albedoMap, vec3 specularMap)
{
    vec3 lightDir       = normalize(light.Position.xyz - fragPos);
    vec3 direction      = normalize(light.Direction.xyz);
    float theta         = dot(lightDir, direction); // check if lighting is inside the spotlight cone

    if(theta > light.CutOff)
    {
        vec3 ambient        = light.Ambient.xyz * albedoMap;

        float diff          = max(dot(normal, lightDir), 0.0);
        vec3 diffuse        = diff * light.Diffuse.xyz * albedoMap;

        vec3 reflectDir     = reflect(-lightDir, normal);
        float spec          = pow(max(dot(viewDir, reflectDir), 0.0), 16); // todo : 16 should be replaced by material.shininess
        vec3 specular       = spec * light.Specular.xyz * specularMap;

        float dist          = length(light.Position.xyz - fragPos);
        float attenuation   = 1.0 / ( light.Constant + (light.Linear * dist) + (light.Quadratic * (dist * dist)) );

        diffuse     *= attenuation;
        specular    *= attenuation;

        return vec3(ambient + diffuse + specular);
    }
    else
    {
        return vec3(light.Ambient.xyz * albedoMap);
    }
}


void main()
{
    vec3 norm       = texture( NormalSampler, TexCoord ).rgb;
    vec4 fragPos    = texture( PositionSampler, TexCoord );
    vec3 albedo     = texture( AlbedoSampler, TexCoord ).rgb;
    vec3 specular   = texture( SpecularSampler, TexCoord ).rgb;

    vec3 viewDir    = normalize( ViewPos.xyz - fragPos.xyz);

    vec3 lighting = vec3(0.0);
    for (uint i = 0;  i < DirectionalLightBuffer.Count; ++i)
    {
        DirectionalLight directionalLight = DirectionalLightBuffer.Data[i];
        lighting += ComputeDirectionalLight(directionalLight, norm, viewDir, albedo, specular);
    }

    for (uint i = 0; i < PointLightBuffer.Count; ++i)
    {
        PointLight pointLight = PointLightBuffer.Data[i];
        lighting += ComputePointLight(pointLight, norm, viewDir, fragPos, albedo, specular);
    }

    for (uint i = 0; i < SpotLightBuffer.Count; ++i)
    {
        SpotLight spotLight = SpotLightBuffer.Data[i];
        lighting += ComputeSpotLight(spotLight, norm, viewDir, fragPos.xyz, albedo, specular);
    }

    OutColor = vec4(lighting, 1.0);
}