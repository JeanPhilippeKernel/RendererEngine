#version 460
#extension GL_GOOGLE_include_directive : require
#include "material.glsl"

layout(location = 0) in vec2 TexCoord;
layout(location = 1) in vec3 WorldNormal;
layout(location = 2) in vec4 FragPos;
layout(location = 3) in flat uint MaterialIdx;

layout(location = 0) out vec4 OutAlbedo;
layout(location = 1) out vec4 OutSpecular;
layout(location = 2) out vec3 OutNormal;
layout(location = 3) out vec4 OutPosition;

void main()
{
    MaterialData material = FetchMaterial(MaterialIdx);

    OutNormal             = normalize(WorldNormal);
    OutSpecular           = material.Specular;
    OutAlbedo             = material.Albedo;
    OutPosition           = FragPos;

    if (material.AlbedoMap < INVALID_MAP_HANDLE)
    {
        uint texId = uint(material.AlbedoMap);
        OutAlbedo  = texture(sampler2D(TextureArray[nonuniformEXT(texId)], LinearWrapSampler), TexCoord);
    }

    if (material.SpecularMap < INVALID_MAP_HANDLE)
    {
        uint texId  = uint(material.SpecularMap);
        OutSpecular = texture(sampler2D(TextureArray[nonuniformEXT(texId)], LinearWrapSampler), TexCoord);
    }

    if (material.NormalMap < INVALID_MAP_HANDLE)
    {
        uint texId = uint(material.NormalMap);
        OutNormal  = texture(sampler2D(TextureArray[nonuniformEXT(texId)], LinearWrapSampler), TexCoord).rgb;
    }
}
