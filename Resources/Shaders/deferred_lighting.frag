#version 460
#extension GL_GOOGLE_include_directive : require

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
#include "pbr_lighting.glsl"
layout(location = 0) in vec2 TexCoord;
layout(location = 0) out vec4 OutColor;

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
    OutColor           = vec4(shadePbr(WorldPos, N, albedo, roughness, metallic, ao, albedo * emissive), 1.0);
}
