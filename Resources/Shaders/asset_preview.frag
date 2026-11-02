#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#include "asset_preview.glsl"
#include "material_alpha.glsl"
layout(set = 1, binding = 0) uniform texture2D TextureArray[];
layout(set = 0, binding = 3) uniform sampler PreviewSampler;
layout(location = 0) in vec3 Position;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 UV;
layout(location = 0) out vec4 Color;
const uint Missing = 0xffffffffu;
vec4 sampleMap(uint index) { return texture(sampler2D(TextureArray[nonuniformEXT(index)], PreviewSampler), UV); }
void main()
{
    vec4 albedo = Preview.Albedo;
    uint alphaMode = Preview.Maps1.y;
    if (Preview.Maps0.x != Missing) albedo = resolveBaseColor(albedo, sampleMap(Preview.Maps0.x), true, alphaMode);
    float opacity = albedo.a;
    // This slot stores glTF occlusion, not opacity. As in scene rendering,
    // coverage comes only from base-color alpha and the material alpha mode.
    applyMaterialAlpha(opacity, Preview.Surface.z, alphaMode);
    vec3 n = length(Normal) > 0.0001 ? normalize(Normal) : normalize(cross(dFdx(Position), dFdy(Position)));
    if (!gl_FrontFacing) n = -n;
    if (Preview.Maps0.z != Missing)
    {
        vec3 dp1 = dFdx(Position), dp2 = dFdy(Position);
        vec2 duv1 = dFdx(UV), duv2 = dFdy(UV);
        vec3 t = cross(dp2, n) * duv1.x + cross(n, dp1) * duv2.x;
        vec3 b = cross(dp2, n) * duv1.y + cross(n, dp1) * duv2.y;
        float scale = inversesqrt(max(max(dot(t, t), dot(b, b)), 1e-8));
        n = normalize(mat3(t * scale, b * scale, n) * (sampleMap(Preview.Maps0.z).xyz * 2.0 - 1.0));
    }
    vec3 eye = normalize(vec3(1.0, 0.65, 1.35)) * 3.2;
    vec3 v = normalize(eye - Position);
    vec3 key = normalize(vec3(-0.6, 0.9, 1.1));
    vec3 fill = normalize(vec3(1.0, 0.35, -0.8));
    float roughness = clamp(Preview.Surface.x, 0.04, 1.0);
    float metallic = clamp(Preview.Surface.y, 0.0, 1.0);
    float ao = 1.0;
    if (Preview.Maps1.x != Missing)
    {
        vec3 orm = sampleMap(Preview.Maps1.x).rgb;
        ao = alphaMode == AlphaLegacy ? orm.r : 1.0;
        roughness = clamp(alphaMode == AlphaLegacy ? orm.g : roughness * orm.g, 0.04, 1.0);
        metallic = clamp(alphaMode == AlphaLegacy ? orm.b : metallic * orm.b, 0.0, 1.0);
    }
    if (alphaMode != AlphaLegacy && Preview.Maps0.w != Missing) ao = sampleMap(Preview.Maps0.w).r;
    vec3 f0 = mix(vec3(0.04), albedo.rgb, metallic);
    vec3 h = normalize(v + key);
    float exponent = mix(256.0, 2.0, roughness * roughness);
    float specular = pow(max(dot(n, h), 0.0), exponent) * (exponent + 2.0) / 16.0;
    vec3 light = albedo.rgb * (1.0 - metallic) * (0.16 + 0.75 * max(dot(n, key), 0.0) + 0.25 * max(dot(n, fill), 0.0));
    light += f0 * specular * max(dot(n, key), 0.0);
    vec3 emissive = Preview.Emissive.rgb;
    if (Preview.Maps0.y != Missing) emissive *= sampleMap(Preview.Maps0.y).rgb;
    light = light * ao + emissive;
    // ZUI expects display-encoded values in its UNORM target.
    // Straight-alpha single-surface preview. ZUI composites this over the
    // checkerboard; don't premultiply here (that would multiply alpha twice).
    Color = vec4(pow(clamp(light, 0.0, 1.0), vec3(1.0 / 2.2)), alphaMode == AlphaBlend ? clamp(opacity, 0.0, 1.0) : 1.0);
}
