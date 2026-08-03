#version 460 core
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) out vec4 fColor;
layout(set = 1, binding = 0) uniform texture2D TextureArray[];
layout(set = 1, binding = 1) uniform sampler LinearWrapSampler;

layout(location = 0) in struct
{
    vec4 Color;
    vec4 TexData;
} In;

void main()
{
    uint texId  = uint(floor(In.TexData.z + 0.5));
    vec4 texVal = texture(sampler2D(TextureArray[nonuniformEXT(texId)], LinearWrapSampler), In.TexData.xy);
    fColor      = In.Color * texVal;
}
