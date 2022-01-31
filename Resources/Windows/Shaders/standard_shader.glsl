#type vertex
#version 450 core

precision mediump float;

layout (location = 0) in vec3 a_position;
layout (location = 1) in vec3 a_normal;
layout (location = 2) in vec2 a_texture_coord;

//global uniform variables
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

//output variables
out vec3 fragment_position;
out vec3 normal_vec;
out vec2 texture_coord;

void main()
{
	gl_Position = projection * view * model * vec4(a_position, 1.0f);
	
	fragment_position = vec3(model * vec4(a_position, 1.0f));
	normal_vec = a_normal;
	texture_coord = a_texture_coord;
}

#type fragment
#version  450 core

precision mediump float;

/* Fragment input variables */
in vec3 normal_vec;
in vec2 texture_coord;

struct StandardMaterial
{
	float tiling_factor;
	vec4 tint_color;
};

struct LightMaterial
{
	float ambient_strength;
	vec3 source_color;
};

uniform StandardMaterial 	material;
uniform LightMaterial 		light;
uniform sampler2D 			texture_sampler;

/* Fragment output variables */
out vec4 output_color;

void main()
{
	vec3 ambient = light.ambient_strength * light.source_color;
	vec4 result = vec4(ambient, 1.0) * (texture(texture_sampler, texture_coord * material.tiling_factor) * material.tint_color);
	
	output_color = result;
}