#version 460
#extension GL_GOOGLE_include_directive : require
#include "material.glsl"
#include "surface.glsl"
#include "material_alpha.glsl"

layout(location = 0) in vec2 TexCoord;
layout(location = 1) in vec3 WorldNormal;
layout(location = 2) in flat uint MaterialIdx;
layout(location = 3) in vec3 WorldPos;
layout(location = 4) in flat uint PickToken;

layout(location = 0) out vec4 OutAlbedoAO;
layout(location = 1) out vec4 OutNormalRoughness;
layout(location = 2) out vec4 OutMetallicEmissive;

void main()
{
    MaterialData material  = FetchMaterial(MaterialIdx);

    if (material.AlphaMode == AlphaBlend) discard;
    vec3         albedo    = material.Albedo.rgb;
    float        alpha     = material.Albedo.a;
    float        ao        = 1.0;
    vec3         normal    = normalize(WorldNormal);
    float        roughness = material.Roughness.y;
    float        metallic  = material.Roughness.x;
    float        emissive  = material.Emissive.r;

    if (material.AlbedoMap < INVALID_MAP_HANDLE)
    {
        uint texId        = material.AlbedoMap;
        vec4 albedoSample = texture(sampler2D(TextureArray[nonuniformEXT(texId)], LinearWrapSampler), TexCoord);
        vec4 base         = resolveBaseColor(material.Albedo, albedoSample, true, material.AlphaMode);
        albedo            = base.rgb;
        alpha             = base.a;
    }

    // Alpha cutout — discard early before any further sampling
    applyMaterialAlpha(alpha, material.Factors.z, material.AlphaMode);

    if (material.NormalMap < INVALID_MAP_HANDLE)
    {
        uint texId        = material.NormalMap;
        vec3 normalSample = texture(sampler2D(TextureArray[nonuniformEXT(texId)], LinearWrapSampler), TexCoord).rgb;
        normal            = perturbNormal(normalize(WorldNormal), WorldPos, normalSample, TexCoord);
    }

    if (material.SpecularMap < INVALID_MAP_HANDLE)
    {
        // glTF metallicRoughnessTexture: R=occlusion, G=roughness, B=metallic
        uint texId = material.SpecularMap;
        vec3 orm   = texture(sampler2D(TextureArray[nonuniformEXT(texId)], LinearWrapSampler), TexCoord).rgb;
        ao         = material.AlphaMode == AlphaLegacy ? orm.r : 1.0;
        roughness  = material.AlphaMode == AlphaLegacy ? orm.g : roughness * orm.g;
        metallic   = material.AlphaMode == AlphaLegacy ? orm.b : metallic * orm.b;
    }
    if (material.AlphaMode != AlphaLegacy && material.OpacityMap < INVALID_MAP_HANDLE)
        ao = texture(sampler2D(TextureArray[nonuniformEXT(material.OpacityMap)], LinearWrapSampler), TexCoord).r;

    if (material.EmissiveMap < INVALID_MAP_HANDLE)
    {
        uint texId = material.EmissiveMap;
        emissive   = texture(sampler2D(TextureArray[nonuniformEXT(texId)], LinearWrapSampler), TexCoord).r;
    }

    OutAlbedoAO         = vec4(albedo, ao);
    OutNormalRoughness  = vec4(normal * 0.5 + 0.5, roughness);
    OutMetallicEmissive = vec4(metallic, emissive, 0.0, 0.0);
}
