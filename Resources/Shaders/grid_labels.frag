#version 460
layout(set = 2, binding = 0) uniform texture2D SceneDepth;
layout(set = 2, binding = 1) uniform texture2D FontAtlas;
layout(set = 2, binding = 2) uniform sampler AtlasSampler;
layout(push_constant) uniform GridLabels
{
    vec4 Rects[2];
    vec4 UVs[2];
    vec4 Colors[2];
    vec4 Depths;
}
Labels;

layout(location = 0) in vec2 UV;
layout(location = 1) flat in vec4 Color;
layout(location = 2) flat in float Depth;
layout(location = 0) out vec4 OutColor;

void main()
{
    const float DepthEpsilon = 2.0e-6; // same sampled-depth policy as Grid Pass
    float       scene_depth  = texelFetch(sampler2D(SceneDepth, AtlasSampler), ivec2(gl_FragCoord.xy), 0).r;
    bool        reverse_z    = Labels.Depths.z != 0.0;
    if (isnan(scene_depth) || isinf(scene_depth) || (reverse_z ? Depth + DepthEpsilon < scene_depth : Depth > scene_depth + DepthEpsilon))
        discard;
    vec4  texel = texture(sampler2D(FontAtlas, AtlasSampler), UV);
    float alpha = Color.a * texel.a;
    if (alpha <= 0.001)
        discard;
    // Atlas masks and authored Grid colors already target display-encoded
    // FrameColor. Match ZUI's straight-alpha text, without a second tone map.
    OutColor = vec4(Color.rgb * texel.rgb, alpha);
}
