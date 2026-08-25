#version 460 core
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) out vec4 out_color;

layout(set = 1, binding = 0) uniform texture2D TextureArray[];
layout(set = 1, binding = 2) uniform sampler   LinearClampSampler;

layout(location = 0) in vec2  v_sdf_pos;
layout(location = 1) in vec2  v_half_size;
layout(location = 2) in vec2  v_uv;
layout(location = 3) in vec4  v_color;
layout(location = 4) in float v_corner_radius;
layout(location = 5) in float v_border;
layout(location = 6) in float v_softness;
layout(location = 7) in float v_tex_index;

// Rounded-box signed distance function (RAD Debugger).
// Returns < 0 inside, > 0 outside.
float rect_sdf(vec2 p, vec2 half_size, float r)
{
    return length(max(abs(p) - half_size + r, 0.0)) - r;
}

void main()
{
    // Sample atlas texture. Solid rects use white-pixel UV → sample = (1,1,1,1).
    uint  tex_id = uint(floor(v_tex_index + 0.5));
    vec4  albedo = texture(sampler2D(TextureArray[tex_id], LinearClampSampler), v_uv);

    out_color = albedo * v_color;

    // v_softness <= 0 means "no SDF" (used by text glyphs — alpha comes from texture).
    if (v_softness > 0.0)
    {
        float softness = max(v_softness, 0.25); // minimum 0.25px ramp

        // Border mask — punch a hole in the interior, keep only the ring.
        float border_t = 1.0;
        if (v_border > 0.0)
        {
            float inner_sdf = rect_sdf(v_sdf_pos,
                                       v_half_size - vec2(softness * 2.0) - v_border,
                                       max(v_corner_radius - v_border, 0.0));
            border_t = smoothstep(0.0, softness * 2.0, inner_sdf);
            if (border_t < 0.001) { discard; }
        }

        // Outer-edge + corner mask.
        float corner_sdf = rect_sdf(v_sdf_pos,
                                     v_half_size - vec2(softness * 2.0),
                                     v_corner_radius);
        float corner_t = 1.0 - smoothstep(0.0, softness * 2.0, corner_sdf);

        out_color.a *= corner_t * border_t;
    }
}
