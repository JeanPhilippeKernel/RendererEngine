#version 460 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor; // RGBA8 UNORM — hardware unpacks packed uint32

layout(push_constant) uniform PC
{
    vec2                     uScale;
    vec2                     uTranslate;
    vec2                     uFbScale; // Actual render-target pixels per logical UI unit, per axis.
    uint                     uTexIdx;
    layout(offset = 32) vec4 uShadow;     // half-size xy, corner radius, Gaussian sigma; w=0 for atlas draws.
    layout(offset = 48) vec4 uColorField; // brightness, lightness, lightness mode, enabled.
}
pc;

layout(location = 0) out struct
{
    vec4 Color;
    vec4 TexData; // xy=UV, z=texIdx
} Out;
layout(location = 2) flat out vec4 vShadow;
layout(location = 3) flat out vec4 vColorField;

void main()
{
    Out.Color   = aColor;
    Out.TexData = vec4(aUV, float(pc.uTexIdx), 0.0);
    vShadow     = pc.uShadow;
    vColorField = pc.uColorField;

    // Preserve the tessellator's subpixel coverage geometry. Rounding every
    // vertex separately distorts circles, thin borders and their AA fringes.
    gl_Position = vec4(aPos * pc.uScale + pc.uTranslate, 0.0, 1.0);
}
