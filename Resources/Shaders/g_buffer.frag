#version 460
#extension GL_GOOGLE_include_directive : require
#include "fragment_common.glsl"

layout (location = 0) in vec3 FragmentPosition;
layout (location = 1) in vec3 WorldNormal;
layout (location = 2) in vec2 TextureCoord;
layout (location = 3) in flat uint MaterialIdx;

layout (location = 0) out vec4 OutAlbedoColor;
layout (location = 1) out vec4 OutSpecular;
layout (location = 2) out vec3 OutNormal;
layout (location = 3) out vec3 OutPosition;

void main()
{
    MaterialData material = MaterialDataBuffer.Data[MaterialIdx];

    OutNormal = normalize(WorldNormal);
    OutPosition = FragmentPosition;
    OutSpecular = vec4(1.0);

    if (material.AlbedoTextureMap < INVALID_TEXTURE_INDEX)
    {
        uint texId  = uint(material.AlbedoTextureMap);
        OutAlbedoColor = texture( TextureArray[nonuniformEXT(texId)], TextureCoord);
    }
}