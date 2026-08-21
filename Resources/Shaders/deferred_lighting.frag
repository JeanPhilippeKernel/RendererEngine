#version 460

layout(set = 0, binding = 0) uniform UBCamera
{
    mat4 View;
    mat4 Projection;
    vec4 Position;
    mat4 InvViewProj;
}
Camera;

layout(set = 2, binding = 0) uniform texture2D GBufferAlbedoAO;
layout(set = 2, binding = 1) uniform texture2D GBufferNormalRoughness;
layout(set = 2, binding = 2) uniform texture2D GBufferMetallicEmissive;
layout(set = 2, binding = 3) uniform texture2D GBufferDepth;
layout(set = 2, binding = 5) uniform sampler GBufferSampler;

struct GpuDirectionalLight
{
    vec4  Direction;
    vec4  Color;
    float Intensity;
    float _p0;
    float _p1;
    float _p2;
};
struct GpuPointLight
{
    vec4  Position;
    vec4  Color;
    float Intensity;
    float Radius;
    float _p0;
    float _p1;
};

layout(std430, set = 2, binding = 4) readonly buffer LightSB
{
    GpuDirectionalLight DirectionalLights[4];
    GpuPointLight       PointLights[8];
    uint                DirectionalCount;
    uint                PointCount;
    uint                _pad[2];
}
LightBuffer;

layout(location = 0) in vec2 TexCoord;
layout(location = 0) out vec4 OutColor;

const float PI = 3.14159265359;

vec3        reconstruct_position(vec2 uv, float depth)
{
    vec4 ndc   = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = Camera.InvViewProj * ndc;
    return world.xyz / world.w;
}

float distribution_ggx(vec3 N, vec3 H, float roughness)
{
    float a   = roughness * roughness;
    float a2  = a * a;
    float NdH = max(dot(N, H), 0.0);
    float d   = NdH * NdH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float geometry_schlick_ggx(float NdV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdV / (NdV * (1.0 - k) + k);
}

float geometry_smith(vec3 N, vec3 V, vec3 L, float roughness)
{
    return geometry_schlick_ggx(max(dot(N, V), 0.0), roughness) * geometry_schlick_ggx(max(dot(N, L), 0.0), roughness);
}

vec3 fresnel_schlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 pbr_directional(GpuDirectionalLight light, vec3 N, vec3 V, vec3 albedo, float roughness, float metallic)
{
    vec3  F0       = mix(vec3(0.04), albedo, metallic);
    vec3  L        = normalize(-light.Direction.xyz);
    vec3  H        = normalize(V + L);
    vec3  radiance = light.Color.rgb * light.Intensity;
    float NDF      = distribution_ggx(N, H, roughness);
    float G        = geometry_smith(N, V, L, roughness);
    vec3  F        = fresnel_schlick(max(dot(H, V), 0.0), F0);
    vec3  spec     = (NDF * G * F) / (4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001);
    vec3  kD       = (1.0 - F) * (1.0 - metallic);
    return (kD * albedo / PI + spec) * radiance * max(dot(N, L), 0.0);
}

vec3 pbr_point(GpuPointLight light, vec3 WorldPos, vec3 N, vec3 V, vec3 albedo, float roughness, float metallic)
{
    vec3  F0          = mix(vec3(0.04), albedo, metallic);
    vec3  L           = normalize(light.Position.xyz - WorldPos);
    vec3  H           = normalize(V + L);
    float dist        = length(light.Position.xyz - WorldPos);
    float attenuation = 1.0 / (dist * dist);
    vec3  radiance    = light.Color.rgb * light.Intensity * attenuation;
    float NDF         = distribution_ggx(N, H, roughness);
    float G           = geometry_smith(N, V, L, roughness);
    vec3  F           = fresnel_schlick(max(dot(H, V), 0.0), F0);
    vec3  spec        = (NDF * G * F) / (4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001);
    vec3  kD          = (1.0 - F) * (1.0 - metallic);
    return (kD * albedo / PI + spec) * radiance * max(dot(N, L), 0.0);
}

void main()
{
    vec4  sAlbedoAO    = texture(sampler2D(GBufferAlbedoAO, GBufferSampler), TexCoord);
    vec4  sNormalRough = texture(sampler2D(GBufferNormalRoughness, GBufferSampler), TexCoord);
    vec4  sMetEmit     = texture(sampler2D(GBufferMetallicEmissive, GBufferSampler), TexCoord);
    float depth        = texture(sampler2D(GBufferDepth, GBufferSampler), TexCoord).r;

    vec3  albedo       = sAlbedoAO.rgb;
    float ao           = sAlbedoAO.a;
    vec3  N            = normalize(sNormalRough.rgb * 2.0 - 1.0);
    float roughness    = max(sNormalRough.a, 0.04);
    float metallic     = sMetEmit.r;
    float emissive     = sMetEmit.g;

    vec3  WorldPos     = reconstruct_position(TexCoord, depth);
    vec3  V            = normalize(Camera.Position.xyz - WorldPos);

    vec3  Lo           = vec3(0.0);
    for (uint i = 0; i < LightBuffer.DirectionalCount; ++i)
        Lo += pbr_directional(LightBuffer.DirectionalLights[i], N, V, albedo, roughness, metallic);
    for (uint i = 0; i < LightBuffer.PointCount; ++i)
        Lo += pbr_point(LightBuffer.PointLights[i], WorldPos, N, V, albedo, roughness, metallic);

    vec3 ambient = vec3(0.03) * albedo * ao;
    vec3 color   = ambient + Lo + albedo * emissive;

    color        = color / (color + vec3(1.0));
    color        = pow(color, vec3(1.0 / 2.2));
    OutColor     = vec4(color, 1.0);
}