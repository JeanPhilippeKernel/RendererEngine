#ifndef ZENGINE_MATERIAL_ALPHA_GLSL
#define ZENGINE_MATERIAL_ALPHA_GLSL
// Keep these values in sync with Materials::AlphaMode.
const uint AlphaLegacy = 0u;
const uint AlphaOpaque = 1u;
const uint AlphaMask = 2u;
const uint AlphaBlend = 3u;

vec4 resolveBaseColor(vec4 factor, vec4 sampled, bool hasMap, uint mode)
{
    if (!hasMap) return factor;
    return mode == AlphaLegacy ? sampled : factor * sampled;
}

void applyMaterialAlpha(float alpha, float cutoff, uint mode)
{
    // Explicit glTF MASK is a binary test, not stochastic/dithered blending.
    if (mode == AlphaMask && alpha < cutoff) discard;
    if (mode == AlphaLegacy && cutoff > 0.0)
    {
        mat4 thresholds = mat4(1.0, 9.0, 3.0, 11.0, 13.0, 5.0, 15.0, 7.0,
                               4.0, 12.0, 2.0, 10.0, 16.0, 8.0, 14.0, 6.0) / 17.0;
        float coverage = clamp(alpha - 0.5 * thresholds[int(mod(gl_FragCoord.x, 4.0))][int(mod(gl_FragCoord.y, 4.0))], 0.0, 1.0);
        if (coverage < cutoff) discard;
    }
}
#endif
