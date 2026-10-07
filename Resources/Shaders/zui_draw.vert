#version 460 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor; // RGBA8 UNORM — hardware unpacks packed uint32

layout(push_constant) uniform PC
{
    vec2 uScale;
    vec2 uTranslate;
    vec2 uFbScale; // Actual render-target pixels per logical UI unit, per axis.
    uint uTexIdx;
}
pc;

layout(location = 0) out struct
{
    vec4 Color;
    vec4 TexData; // xy=UV, z=texIdx
} Out;

void main()
{
    Out.Color    = aColor;
    Out.TexData  = vec4(aUV, float(pc.uTexIdx), 0.0);

    // Snap to the nearest physical pixel before projecting.
    // Prevents sub-pixel drift that blurs glyph quads on non-Retina displays.
    vec2 fs      = max(pc.uFbScale, vec2(0.0001));
    vec2 snapped = round(aPos * fs) / fs;
    gl_Position  = vec4(snapped * pc.uScale + pc.uTranslate, 0.0, 1.0);
}
