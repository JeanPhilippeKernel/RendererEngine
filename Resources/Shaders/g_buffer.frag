#version 460
#extension GL_GOOGLE_include_directive : require
#include "fragment_common.glsl"

layout (location = 0) in vec2 TexCoord;
layout (location = 1) in vec3 WorldNormal;
layout (location = 2) in vec4 FragPos;
layout (location = 3) in flat uint MaterialIdx;

layout (location = 0) out vec4 OutAlbedo;
layout (location = 1) out vec4 OutSpecular;
layout (location = 2) out vec3 OutNormal;
layout (location = 3) out vec4 OutPosition;

void main()
{
    MaterialData material = FetchMaterial(MaterialIdx);

    OutNormal   = normalize(WorldNormal);
    OutSpecular = material.Specular;
    OutAlbedo   = material.Albedo;
    OutPosition = FragPos;

    if (material.AlbedoMap < INVALID_MAP_HANDLE)
    {
        uint texId = uint(material.AlbedoMap);
        OutAlbedo  = texture( TextureArray[nonuniformEXT(texId)], TexCoord );
    }

    if(material.SpecularMap < INVALID_MAP_HANDLE)
    {
        uint texId  = uint(material.SpecularMap);
        OutSpecular = texture( TextureArray[nonuniformEXT(texId)], TexCoord );
    }

    if(material.NormalMap < INVALID_MAP_HANDLE)
    {
        uint texId  = uint(material.NormalMap);
        OutNormal   = texture( TextureArray[nonuniformEXT(texId)], TexCoord ).rgb;
    }
}