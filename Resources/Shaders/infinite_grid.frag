#version 460
layout(location = 0) in vec2 Ndc;
layout(location = 0) out vec4 OutColor;

layout(set = 0, binding = 0) uniform UBCamera
{
    mat4 View;
    mat4 Projection;
    vec4 Position;
    mat4 InvViewProj;
}
Camera;

layout(set = 2, binding = 0) uniform texture2D SceneDepth;
layout(set = 2, binding = 1) uniform sampler DepthSampler;

layout(push_constant) uniform GridSettings
{
    vec4  OriginAndLogSpacing;
    vec4  Orientation;
    vec4  ColorThin;
    vec4  ColorThick;
    vec4  ColorUAxis;
    vec4  ColorVAxis;
    vec4  Style;
    uvec4 Controls;
}
Grid;

const float DepthEpsilon = 2.0e-6;
const float LogLODStride = 3.32192809489; // log2(10), independent of major cadence

bool        finite3(vec3 value)
{
    return !any(isnan(value)) && !any(isinf(value));
}

vec3 rotate_axis(vec3 axis)
{
    vec4 q = Grid.Orientation;
    return axis + 2.0 * cross(q.xyz, cross(q.xyz, axis) + q.w * axis);
}

float line_coverage(vec2 uv, vec2 footprint, float log_spacing)
{
    // Out-of-range spacing/phase has no resolvable repeated lines; axes still
    // draw. Avoid zero/inf modulo and phase precision lost beyond float integers.
    if (log_spacing < -100.0 || log_spacing > 100.0)
        return 0.0;
    float spacing = exp2(log_spacing);
    vec2  phase   = uv / spacing;
    if (any(greaterThan(abs(phase), vec2(8388608.0))))
        return 0.0;
    // Multiply before dividing: spacing/footprint alone can overflow even
    // when an exactly-on-line distance is zero and perfectly representable.
    vec2 distance_in_pixels = (abs(fract(phase + 0.5) - 0.5) * spacing) / footprint;
    vec2 coverage           = clamp(vec2(min(Grid.Style.z, 1.0e20) * 0.5 + 0.5) - distance_in_pixels, 0.0, 1.0);
    return max(coverage.x, coverage.y);
}

void main()
{
    // Take screen derivatives before any divergent rejection. The plane
    // homography below gives exact local derivatives even along occlusion/horizon
    // boundaries; no derivatives of values from a discarded neighbour are used.
    vec2 ndc_dx     = dFdx(Ndc);
    vec2 ndc_dy     = dFdy(Ndc);
    vec3 u          = rotate_axis(vec3(1.0, 0.0, 0.0));
    vec3 v          = rotate_axis(vec3(0.0, 0.0, 1.0));
    vec3 n          = cross(v, u); // default U=+X, V=+Z, N=+Y
    vec4 plane      = vec4(n, -dot(n, Grid.OriginAndLogSpacing.xyz));
    vec4 clip_plane = transpose(Camera.InvViewProj) * plane;
    if (isnan(clip_plane.z) || isinf(clip_plane.z) || abs(clip_plane.z) < 1.0e-20)
        discard;

    // Intersect the pixel's projective camera line with the plane. No far
    // endpoint is divided by zero: infinite-far and orthographic projections use
    // the same homogeneous expression. Vulkan clip depth is always [0,1].
    float depth = -(dot(clip_plane.xy, Ndc) + clip_plane.w) / clip_plane.z;
    if (isnan(depth) || isinf(depth) || depth < 0.0 || depth > 1.0)
        discard;
    vec4 hit_h = Camera.InvViewProj * vec4(Ndc, depth, 1.0);
    if (isnan(hit_h.w) || isinf(hit_h.w) || hit_h.w <= 1.0e-20)
        discard;
    vec3 hit  = hit_h.xyz / hit_h.w;
    vec4 dx_h = Camera.InvViewProj * vec4(ndc_dx, -dot(clip_plane.xy, ndc_dx) / clip_plane.z, 0.0);
    vec4 dy_h = Camera.InvViewProj * vec4(ndc_dy, -dot(clip_plane.xy, ndc_dy) / clip_plane.z, 0.0);
    vec3 dx   = (dx_h.xyz - hit * dx_h.w) / hit_h.w;
    vec3 dy   = (dy_h.xyz - hit * dy_h.w) / hit_h.w;
    if (!finite3(hit) || !finite3(dx) || !finite3(dy))
        discard;
    vec3 relative  = hit - Grid.OriginAndLogSpacing.xyz;
    vec2 uv        = vec2(dot(relative, u), dot(relative, v));
    vec2 footprint = max(vec2(length(vec2(dot(dx, u), dot(dy, u))), length(vec2(dot(dx, v), dot(dy, v)))), vec2(1.0e-20));
    if (any(isnan(uv)) || any(isinf(uv)) || any(isnan(footprint)) || any(isinf(footprint)))
        discard;
    float scene_depth = texelFetch(sampler2D(SceneDepth, DepthSampler), ivec2(gl_FragCoord.xy), 0).r;
    bool  reverse_z   = Grid.Controls.y != 0u;
    if (isnan(scene_depth) || isinf(scene_depth) || (reverse_z ? depth + DepthEpsilon < scene_depth : depth > scene_depth + DepthEpsilon))
        discard;

    float lod           = max(0.0, (log2(max(footprint.x, footprint.y)) + 1.0 - Grid.OriginAndLogSpacing.w) / LogLODStride);
    float cap_fade      = 1.0 - smoothstep(Grid.Style.w, Grid.Style.w + 1.0, lod);
    float level         = min(lod, Grid.Style.w);
    float spacing       = Grid.OriginAndLogSpacing.w + floor(level) * LogLODStride;
    float blend         = fract(level);
    float minor         = mix(line_coverage(uv, footprint, spacing), line_coverage(uv, footprint, spacing + LogLODStride), blend);
    float major_spacing = spacing + log2(float(Grid.Controls.x));
    float major         = mix(line_coverage(uv, footprint, major_spacing), line_coverage(uv, footprint, major_spacing + LogLODStride), blend);
    // Compose authored alpha in premultiplied form, converting once at output
    // for the pipeline's straight-alpha blend. A transparent axis or major
    // line must not recolor a visible minor line underneath it.
    float minor_alpha   = Grid.ColorThin.a * minor * cap_fade;
    float major_alpha   = Grid.ColorThick.a * major * cap_fade;
    vec4  color         = vec4(Grid.ColorThin.rgb * minor_alpha, minor_alpha);
    color               = vec4(Grid.ColorThick.rgb * major_alpha, major_alpha) + color * (1.0 - major_alpha);

    vec2  axis_distance = abs(uv) / footprint;
    float on_u          = clamp(min(Grid.Style.z, 1.0e20) * 0.5 + 0.5 - axis_distance.y, 0.0, 1.0);
    float on_v          = clamp(min(Grid.Style.z, 1.0e20) * 0.5 + 0.5 - axis_distance.x, 0.0, 1.0);
    float v_alpha       = Grid.ColorVAxis.a * on_v;
    float u_alpha       = Grid.ColorUAxis.a * on_u;
    color               = vec4(Grid.ColorVAxis.rgb * v_alpha, v_alpha) + color * (1.0 - v_alpha);
    color               = vec4(Grid.ColorUAxis.rgb * u_alpha, u_alpha) + color * (1.0 - u_alpha);

    vec3 camera_offset  = hit - Camera.Position.xyz;
    vec2 camera_uv      = vec2(dot(camera_offset, u), dot(camera_offset, v));
    if (!finite3(camera_offset) || any(isnan(camera_uv)) || any(isinf(camera_uv)))
        discard;
    float largest           = max(abs(camera_uv.x), abs(camera_uv.y));
    float log_distance      = largest > 0.0 ? log2(largest) + 0.5 * log2(dot(camera_uv / largest, camera_uv / largest)) : -200.0;
    float relative_distance = exp2(clamp(log_distance - Grid.Style.x, -100.0, 100.0));
    if (relative_distance >= 1.0 || isnan(relative_distance))
        discard;
    float falloff = Grid.Style.y == 0.0 ? 1.0 : pow(max(0.0, 1.0 - relative_distance), Grid.Style.y);
    float alpha   = color.a * falloff;
    if (alpha <= 0.001 || isnan(alpha) || isinf(alpha) || !finite3(color.rgb))
        discard;
    // FrameColor is display-encoded after tone mapping; authored overlay colors
    // already use that space. Never tone-map or encode them a second time.
    OutColor = vec4(color.rgb / color.a, alpha);
}
