#version 460 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;

layout(push_constant) uniform uPushConstant
{
    vec2 uScale;
    vec2 uTranslate;
    uint index;
}
pc;

layout(location = 0) out struct
{
    vec4 Color;
    vec4 TexData;
} Out;

void main()
{
    Out.Color   = aColor;
    Out.TexData = vec4(aUV, pc.index, 0.0);
    gl_Position = vec4(aPos * pc.uScale + pc.uTranslate, 0, 1);
}