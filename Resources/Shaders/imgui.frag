#version 460 core
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

layout(location = 0) out vec4 fColor;
layout(set = 0, binding = 0) uniform sampler2D TextureArray[];

layout(location = 0) in struct
{
    vec4 Color;
    vec4 TexData;
} In;

void main()
{
    uint texId = uint(In.TexData.z);
    if (texId < 0xFFFFFFFFu)
    {
        vec4 texVal = texture(TextureArray[nonuniformEXT(texId)], In.TexData.xy);
        fColor      = In.Color * texVal;
    }
}