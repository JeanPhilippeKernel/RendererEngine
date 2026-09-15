#version 460

layout(set = 2, binding = 0) uniform texture2D SceneColor;
layout(set = 2, binding = 1) uniform sampler LinearClampToEdgeSampler;

layout(location = 0) in vec2 TexCoord;
layout(location = 0) out vec4 OutColor;

vec3 aces_fitted(vec3 colour)
{
    const mat3 input_matrix  = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
    const mat3 output_matrix = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);

    colour                   = input_matrix * max(colour, vec3(0.0));
    const vec3 numerator     = colour * (colour + 0.0245786) - 0.000090537;
    const vec3 denominator   = colour * (0.983729 * colour + 0.4329510) + 0.238081;
    return clamp(output_matrix * (numerator / denominator), vec3(0.0), vec3(1.0));
}

float ordered_dither(ivec2 pixel)
{
    const int bayer[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
    const int index     = (pixel.y & 3) * 4 + (pixel.x & 3);
    return (float(bayer[index]) + 0.5) / 16.0 - 0.5;
}

void main()
{
    const vec3 hdr_colour     = texture(sampler2D(SceneColor, LinearClampToEdgeSampler), TexCoord).rgb;
    const vec3 display_colour = pow(aces_fitted(hdr_colour), vec3(1.0 / 2.2));
    OutColor                  = vec4(clamp(display_colour + vec3(ordered_dither(ivec2(gl_FragCoord.xy)) / 255.0), vec3(0.0), vec3(1.0)), 1.0);
}
