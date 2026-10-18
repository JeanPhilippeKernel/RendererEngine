// Hexagon field math adapted from redlamp/color-taylor, revision
// b0a895aa80a36adf75e7c9555cd331587cbb34f8, under the MIT License.
// Copyright (c) 2026 Taylor Wright
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#version 460 core
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) out vec4 fColor;
layout(set = 1, binding = 0) uniform texture2D TextureArray[];
layout(set = 1, binding = 2) uniform sampler LinearClampSampler;

layout(location = 0) in struct
{
    vec4 Color;
    vec4 TexData; // xy=UV, z=texIdx
} In;
layout(location = 2) flat in vec4 vShadow;
layout(location = 3) flat in vec4 vColorField;

// Analytic counterpart of ZUIColorMath::ColorAtPoint (color-taylor, MIT;
// see THIRD_PARTY_NOTICES.md). RGB stays display sRGB; alpha is edge coverage.
vec3 hueChannels(float hue, float chroma, float minimum)
{
    vec3 pure = clamp(abs(mod(hue / 60.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
    return pure * chroma + minimum;
}

vec4 colorField(vec2 point)
{
    const float pi         = 3.14159265358979323846;
    const float apothem    = 0.86602540378443864676;
    float       distance   = length(point);
    float       angle      = distance > 0.0 ? atan(-point.y, point.x) : 0.0;
    float       hue        = mod(degrees(angle) + 360.0, 360.0);
    float       edge       = apothem / cos(mod(angle + 2.0 * pi, pi / 3.0) - pi / 6.0);
    float       radial     = distance / edge;
    float       limit      = vColorField.z > 0.5 ? 1.0 - abs(2.0 * vColorField.y - 1.0) : vColorField.x;
    float       saturation = limit > 0.0 ? clamp(radial / limit, 0.0, 1.0) : 0.0;
    vec3        rgb        = vec3(0.0);
    if (vColorField.z > 0.5)
    {
        float lightness = radial <= limit ? vColorField.y : (vColorField.y <= 0.5 ? clamp(radial, 0.0, 1.0) * 0.5 : 1.0 - clamp(radial, 0.0, 1.0) * 0.5);
        if (radial > limit)
            saturation = 1.0;
        float chroma = (1.0 - abs(2.0 * lightness - 1.0)) * saturation;
        rgb          = hueChannels(hue, chroma, lightness - chroma * 0.5);
    }
    else
    {
        float brightness = radial <= limit ? vColorField.x : clamp(radial, 0.0, 1.0);
        if (radial > limit)
            saturation = 1.0;
        rgb = hueChannels(hue, brightness * saturation, brightness * (1.0 - saturation));
    }
    // Hexagon's six half planes avoid angular derivative discontinuities at AA edges.
    vec2  p       = abs(point);
    float outside = max(p.y - apothem, dot(p, vec2(apothem, 0.5)) - apothem);
    float alpha   = clamp(0.5 - outside / max(fwidth(outside), 0.000001), 0.0, 1.0);
    return vec4(floor(clamp(rgb, 0.0, 1.0) * 255.0 + 0.5) / 255.0, alpha);
}

// Approximate erf; sufficient precision for an 8-bit alpha channel.
vec2 erfApprox(vec2 x)
{
    vec2 a = abs(x);
    vec2 t = 1.0 / (1.0 + 0.3275911 * a);
    vec2 p = (((((1.061405429 * t - 1.453152027) * t) + 1.421413741) * t - 0.284496736) * t + 0.254829592) * t;
    return sign(x) * (1.0 - p * exp(-a * a));
}

float roundedShadow(vec2 p, vec2 halfSize, float radius, float sigma)
{
    // Integrate the horizontal Gaussian analytically, then integrate over y
    // with four fixed samples. Unlike an SDF falloff, this also handles corners
    // and boxes smaller than the blur. No texture or offscreen blur pass needed.
    float lo = max(-halfSize.y, p.y - 3.0 * sigma);
    float hi = min(halfSize.y, p.y + 3.0 * sigma);
    if (hi <= lo)
        return 0.0;
    float stepY    = (hi - lo) * 0.25;
    float coverage = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        float y         = lo + (float(i) + 0.5) * stepY;
        float cornerY   = max(abs(y) - (halfSize.y - radius), 0.0);
        float extentX   = halfSize.x - radius + sqrt(max(radius * radius - cornerY * cornerY, 0.0));
        vec2  integral  = erfApprox((vec2(extentX, -extentX) - p.x) * (0.70710678118 / sigma));
        float dy        = (p.y - y) / sigma;
        coverage       += 0.5 * (integral.x - integral.y) * exp(-0.5 * dy * dy);
    }
    return clamp(coverage * stepY * (0.3989422804 / sigma), 0.0, 1.0);
}

void main()
{
    if (vColorField.w > 1.5)
    {
        vec2  q        = abs(In.TexData.xy) - vShadow.xy + vShadow.z;
        float distance = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - vShadow.z;
        float coverage = clamp(0.5 - distance / max(fwidth(distance), 0.000001), 0.0, 1.0);
        vec4  paint    = In.Color;
        if (vColorField.z > 0.5)
        {
            vec2  cell  = floor((In.TexData.xy + vShadow.xy) / 4.0);
            float value = mod(cell.x + cell.y, 2.0) < 1.0 ? 104.0 / 255.0 : 58.0 / 255.0;
            paint       = vec4(mix(vec3(value), paint.rgb, paint.a), 1.0);
        }
        fColor = vec4(paint.rgb, paint.a * coverage);
        return;
    }
    if (vColorField.w > 0.5)
    {
        fColor = colorField(In.TexData.xy);
        return;
    }
    if (vShadow.w > 0.0)
    {
        float alpha = roundedShadow(In.TexData.xy, vShadow.xy, vShadow.z, vShadow.w);
        fColor      = vec4(In.Color.rgb, In.Color.a * alpha);
        return;
    }
    uint texId  = uint(floor(In.TexData.z + 0.5));
    vec4 texVal = texture(sampler2D(TextureArray[nonuniformEXT(texId)], LinearClampSampler), In.TexData.xy);
    fColor      = In.Color * texVal;
}
