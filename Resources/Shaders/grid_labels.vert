#version 460
layout(push_constant) uniform GridLabels
{
    vec4 Rects[2];
    vec4 UVs[2];
    vec4 Colors[2];
    vec4 Depths;
}
Labels;

layout(location = 0) out vec2 UV;
layout(location = 1) flat out vec4 Color;
layout(location = 2) flat out float Depth;

void main()
{
    const vec2 corners[6] = vec2[6](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 0), vec2(1, 1), vec2(0, 1));
    uint       label      = gl_InstanceIndex;
    vec2       corner     = corners[gl_VertexIndex];
    vec4       rect       = Labels.Rects[label];
    UV                    = mix(Labels.UVs[label].xy, Labels.UVs[label].zw, corner);
    Color                 = Labels.Colors[label];
    Depth                 = Labels.Depths[label];
    gl_Position           = vec4(rect.xy + rect.zw * corner, Depth, 1.0);
}
