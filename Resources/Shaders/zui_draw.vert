#version 460 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor; // RGBA8 UNORM — hardware unpacks packed uint32

layout(push_constant) uniform PC
{
    vec2 uScale;
    vec2 uTranslate;
    uint uTexIdx;
    uint _pad;
}
pc;

layout(location = 0) out struct
{
    vec4 Color;
    vec4 TexData; // xy=UV, z=texIdx (matches imgui.vert convention)
} Out;

void main()
{
    Out.Color   = aColor;
    Out.TexData = vec4(aUV, float(pc.uTexIdx), 0.0);
    gl_Position = vec4(aPos * pc.uScale + pc.uTranslate, 0.0, 1.0);
}
