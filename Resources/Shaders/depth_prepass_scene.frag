#version 460
#extension GL_GOOGLE_include_directive : require
#include "material.glsl"
#include "material_alpha.glsl"
layout(location = 0) in vec2 TexCoord;
layout(location = 1) in flat uint MaterialIdx;
void main()
{
    MaterialData material = FetchMaterial(MaterialIdx);
    if (material.AlphaMode == AlphaBlend) discard;
    float alpha = material.Albedo.a;
    if (material.AlbedoMap != INVALID_MAP_HANDLE)
    {
        vec4 sampled = texture(sampler2D(TextureArray[nonuniformEXT(material.AlbedoMap)], LinearWrapSampler), TexCoord);
        alpha = resolveBaseColor(material.Albedo, sampled, true, material.AlphaMode).a;
    }
    applyMaterialAlpha(alpha, material.Factors.z, material.AlphaMode);
}
