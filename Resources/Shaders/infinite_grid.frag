#version 460
#extension GL_GOOGLE_include_directive : require
#include "utility.glsl"

layout(location = 0) in vec2 uv;
layout(location = 1) in float scaleFactor;

layout(location = 0) out vec4 outColor;

float       gridCellSize              = 0.025;
vec4        gridColorThin             = vec4(1.0, 1.0, 1.0, 1.0);
vec4        gridColorThick            = vec4(0.5, 0.5, 0.5, 1.0);
const float gridMinPixelsBetweenCells = 2.0;

void        main()
{
    vec2 dudv             = vec2(length(vec2(dFdx(uv.x), dFdy(uv.x))), length(vec2(dFdx(uv.y), dFdy(uv.y))));
    dudv                  = max(dudv, vec2(0.0001));

    float lodLevel        = max(0.0, log_10((length(dudv) * gridMinPixelsBetweenCells) / gridCellSize) + 1.0);
    lodLevel              = min(lodLevel, 2.0);

    // Smooth fade between LOD levels to kill flickering
    float lodFade         = smoothstep(0.0, 1.0, fract(lodLevel));

    float lod0            = gridCellSize * pow(10.0, floor(lodLevel));
    float lod1            = gridCellSize * pow(10.0, floor(lodLevel + 1.0));
    float lod2            = gridCellSize * pow(10.0, floor(lodLevel + 2.0));

    dudv                 *= 4.0;

    float lod0a           = max2(vec2(1.0) - abs(satv(mod(uv, lod0) / dudv) * 2.0 - vec2(1.0)));
    float lod1a           = max2(vec2(1.0) - abs(satv(mod(uv, lod1) / dudv) * 2.0 - vec2(1.0)));
    float lod2a           = max2(vec2(1.0) - abs(satv(mod(uv, lod2) / dudv) * 2.0 - vec2(1.0)));

    vec4  c               = lod2a > 0.0 ? gridColorThick : lod1a > 0.0 ? mix(gridColorThick, gridColorThin, lodFade) : gridColorThin;

    // Fade based on distance from camera, using actual world-space diagonal
    float maxDist         = scaleFactor * 1.5; // covers the quad diagonal
    float opacityFalloff  = pow(1.0 - satf(length(uv) / maxDist), 0.5);

    c.a                   = lod2a > 0.0 ? lod2a : lod1a > 0.0 ? lod1a : (lod0a * (1.0 - lodFade));
    c.a                  *= opacityFalloff;

    // Discard fully transparent fragments to avoid depth artifacts
    if (c.a < 0.001)
        discard;

    outColor = c;
}
