#version 460
#extension GL_GOOGLE_include_directive : require
#include "utility.glsl"

layout(location = 0) in vec2 uv;
layout(location = 1) in vec2 camXZ;
layout(location = 2) in float camHeight;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform GridSettings
{
    vec4  colorThin;
    vec4  colorThick;
    vec4  colorXAxis;
    vec4  colorZAxis;
    float cellSize;
    float fadeStrength;
    float fadeRadius;
    float lineWidth;
    int   maxLOD;
    float groundY;
    vec2  _pad;
}
Settings;

void main()
{
    vec2 dudv             = vec2(length(vec2(dFdx(uv.x), dFdy(uv.x))), length(vec2(dFdx(uv.y), dFdy(uv.y))));
    dudv                  = max(dudv, vec2(0.0001));

    // LOD level clamped to maxLOD — grid fades out entirely beyond the cap
    float rawLOD          = max(0.0, log_10((length(dudv) * 2.0) / Settings.cellSize) + 1.0);
    float lodLevel        = clamp(rawLOD, 0.0, float(Settings.maxLOD));
    float lodFade         = smoothstep(0.0, 1.0, fract(lodLevel));

    // Smooth fade-out as lodLevel approaches maxLOD
    float lodCapFade      = 1.0 - smoothstep(float(Settings.maxLOD) - 1.0, float(Settings.maxLOD), rawLOD);

    float lod0            = Settings.cellSize * pow(10.0, floor(lodLevel));
    float lod1            = Settings.cellSize * pow(10.0, floor(lodLevel + 1.0));
    float lod2            = Settings.cellSize * pow(10.0, floor(lodLevel + 2.0));

    dudv                 *= Settings.lineWidth;

    float lod0a           = max2(vec2(1.0) - abs(satv(mod(uv, lod0) / dudv) * 2.0 - vec2(1.0)));
    float lod1a           = max2(vec2(1.0) - abs(satv(mod(uv, lod1) / dudv) * 2.0 - vec2(1.0)));
    float lod2a           = max2(vec2(1.0) - abs(satv(mod(uv, lod2) / dudv) * 2.0 - vec2(1.0)));

    vec4  c               = lod2a > 0.0 ? Settings.colorThick : lod1a > 0.0 ? mix(Settings.colorThick, Settings.colorThin, lodFade) : Settings.colorThin;

    // Distance fade measured from camera XZ position, not world origin
    float dist            = length(uv - camXZ);
    float opacityFalloff  = pow(1.0 - satf(dist / Settings.fadeRadius), Settings.fadeStrength);

    // Fade out when camera is nearly coplanar with the grid (within 2 world units)
    float angleFade       = satf(abs(camHeight) / 2.0);

    c.a                   = lod2a > 0.0 ? lod2a : lod1a > 0.0 ? lod1a : (lod0a * (1.0 - lodFade));
    c.a                  *= opacityFalloff * angleFade * lodCapFade;

    // Axis lines: override color for X (uv.y == 0) and Z (uv.x == 0) axes
    float axisWidth       = length(dudv) * 0.5;
    float onXAxis         = satf(1.0 - abs(uv.y) / axisWidth);
    float onZAxis         = satf(1.0 - abs(uv.x) / axisWidth);

    if (onZAxis > 0.5)
    {
        c.rgb = mix(c.rgb, Settings.colorZAxis.rgb, onZAxis);
        c.a   = max(c.a, onZAxis * opacityFalloff * angleFade * lodCapFade);
    }
    if (onXAxis > 0.5)
    {
        c.rgb = mix(c.rgb, Settings.colorXAxis.rgb, onXAxis);
        c.a   = max(c.a, onXAxis * opacityFalloff * angleFade * lodCapFade);
    }

    if (c.a < 0.001)
        discard;

    outColor = c;
}
