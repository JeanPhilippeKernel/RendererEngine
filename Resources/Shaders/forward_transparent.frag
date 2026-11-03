#version 460
#extension GL_GOOGLE_include_directive : require
#include "material.glsl"
#include "material_alpha.glsl"
#include "surface.glsl"
layout(set = 0, binding = 0) uniform UBCamera
{
    mat4 View;
    mat4 Projection;
    vec4 Position;
    mat4 InvViewProj;
}
Camera;
#include "pbr_lighting.glsl"
layout(location = 0) in vec2 TexCoord;
layout(location = 1) in vec3 WorldNormal;
layout(location = 2) in flat uint MaterialIdx;
layout(location = 3) in vec3 WorldPos;
layout(location = 0) out vec4 Color;
void main()
{
    MaterialData material = FetchMaterial(MaterialIdx);
    if (material.AlphaMode != AlphaBlend)
        discard;
    vec4 base = material.Albedo;
    if (material.AlbedoMap != INVALID_MAP_HANDLE)
        base = resolveBaseColor(base, texture(sampler2D(TextureArray[nonuniformEXT(material.AlbedoMap)], LinearWrapSampler), TexCoord), true, material.AlphaMode);
    float alpha = clamp(base.a, 0.0, 1.0);
    if (alpha <= 0.0)
        discard;
    vec3 n = normalize(WorldNormal);
    if (!gl_FrontFacing)
        n = -n;
    if (material.NormalMap != INVALID_MAP_HANDLE)
        n = perturbNormal(n, WorldPos, texture(sampler2D(TextureArray[nonuniformEXT(material.NormalMap)], LinearWrapSampler), TexCoord).rgb, TexCoord);
    float roughness = material.Roughness.y;
    float metallic  = material.Roughness.x;
    float ao        = 1.0;
    if (material.SpecularMap != INVALID_MAP_HANDLE)
    {
        vec3 orm   = texture(sampler2D(TextureArray[nonuniformEXT(material.SpecularMap)], LinearWrapSampler), TexCoord).rgb;
        roughness *= orm.g;
        metallic  *= orm.b;
    }
    if (material.OpacityMap != INVALID_MAP_HANDLE)
        ao = texture(sampler2D(TextureArray[nonuniformEXT(material.OpacityMap)], LinearWrapSampler), TexCoord).r;
    vec3 emissive = material.Emissive.rgb;
    if (material.EmissiveMap != INVALID_MAP_HANDLE)
        emissive *= texture(sampler2D(TextureArray[nonuniformEXT(material.EmissiveMap)], LinearWrapSampler), TexCoord).rgb;
    Color = vec4(shadePbr(WorldPos, n, base.rgb, clamp(roughness, 0.04, 1.0), clamp(metallic, 0.0, 1.0), ao, emissive), alpha);
}
