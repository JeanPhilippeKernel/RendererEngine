#version 460
#extension GL_GOOGLE_include_directive : require
#include "fragment_common.glsl"

layout(location = 0) in vec3 uvw;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec4 worldPos;
layout(location = 3) in flat uint materialIdx;
layout(location = 4) in vec4 CameraPosition;

layout(location = 0) out vec4 outColor;

void main()
{
    MaterialData material = MaterialDataBuffer.Data[materialIdx];

    vec4 emissive = vec4(0,0,0,0); //material.EmissiveColor;
    vec4 albedo = material.AlbedoColor;
    vec3 normalSample = vec3(0, 0, 0);

    const int INVALID_HANDLE = 2000; //0xFFFFFFFF

    if (material.AlbedoTextureMap < INVALID_HANDLE)
    {
        uint albedoTextureId    = uint(material.AlbedoTextureMap);
        albedo     = texture( TextureArray[nonuniformEXT(albedoTextureId)], uvw.xy);
    }
    if (material.EmissiveTextureMap < INVALID_HANDLE)
    {
        uint emissiveTextureId  = uint(material.EmissiveTextureMap);
        emissive   = texture( TextureArray[nonuniformEXT(emissiveTextureId)], uvw.xy);
    }
    if (material.NormalTextureMap < INVALID_HANDLE)
    {
        uint normalTextureId    = uint(material.NormalTextureMap);
        normalSample     = texture( TextureArray[nonuniformEXT(normalTextureId)], uvw.xy).xyz;
    }

    runAlphaTest(albedo.a, material.AlphaTest);

	// world-space normal
	vec3 n = normalize(worldNormal);

	// normal mapping: skip missing normal maps
	if (length(normalSample) > 0.5)
	{
		n = perturbNormal(n, normalize(CameraPosition.xyz - worldPos.xyz), normalSample, uvw.xy);
	}

	vec3 lightDir = normalize(vec3(-1.0, -1.0, 0.1));

	float NdotL = clamp( dot(n, lightDir), 0.3, 1.0 );

	outColor = vec4( albedo.rgb * NdotL + emissive.rgb, 1.0 );

}
