#version  450 core

precision mediump float;

/*
 * input variables
 */
layout (location = 0) in vec3 out_normal;
layout (location = 1) in vec2 out_texture_coord;
layout (set = 0, binding = 1) uniform sampler2D sTexture;
/*
 * Fragment output variables
 */
layout (location = 0) out vec4 output_fragment_color;

void main()
{
	output_fragment_color = vec4(out_texture_coord, 0.0, 1.0);
}
