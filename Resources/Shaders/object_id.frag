#version 460
#extension GL_GOOGLE_include_directive : require
#include "material.glsl"
#include "material_alpha.glsl"
layout(location = 0) in vec2 TexCoord;
layout(location = 1) in flat uint MaterialIdx;
layout(location = 2) in flat uint PickToken;
layout(location = 0) out uint ObjectId;
void main()
{
    MaterialData material = FetchMaterial(MaterialIdx);
    vec4         base     = material.Albedo;
    if (material.AlbedoMap != INVALID_MAP_HANDLE)
        base = resolveBaseColor(base, texture(sampler2D(TextureArray[nonuniformEXT(material.AlbedoMap)], LinearWrapSampler), TexCoord), true, material.AlphaMode);
    applyMaterialAlpha(base.a, material.Factors.z, material.AlphaMode);
    if (material.AlphaMode == AlphaBlend && base.a <= 0.0)
        discard;
    ObjectId = PickToken;
}
