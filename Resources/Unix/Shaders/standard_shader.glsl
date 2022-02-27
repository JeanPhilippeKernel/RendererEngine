#type vertex
#version 330 core

precision mediump float;

/*
 * Vertex global variables
 */
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

/*
 * Vertex input variables
 */
layout (location = 0) in vec3 a_position;
layout (location = 1) in vec3 a_normal;
layout (location = 2) in vec2 a_texture_coord;

/*
 * Vertex output variables 
 */
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
#version  330 core

precision mediump float;

struct StandardMaterial
{
	float tiling_factor;
	vec4 tint_color;

	float shininess;
    sampler2D specular;
    sampler2D diffuse;
};

struct LightMaterial
{
	vec3 position;

    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};

/*
 * Fragment input variables
 */
in vec2 texture_coord;
in vec3 fragment_position;
in vec3 normal_vec;

uniform vec3 view_position;
uniform StandardMaterial material;
uniform LightMaterial light;

/*
 * Fragment output variables
 */
out vec4 output_color;

void main()
{
	// ambient color
	vec3 ambient = light.ambient * vec3(texture(material.diffuse, texture_coord * material.tiling_factor) * material.tint_color);

	// diffuse color
	vec3 norm = normalize(normal_vec);
	vec3 light_direction = normalize(light.position - fragment_position);
	float diff = max(dot(norm, light_direction), 0.0);
	vec3 diffuse = light.diffuse * diff * vec3(texture(material.diffuse, texture_coord * material.tiling_factor) * material.tint_color);
	
	//specular color
	vec3 view_direction = normalize(view_position - fragment_position);
	vec3 reflect_direction = reflect(-light_direction, norm);
	float spec = pow(max(dot(view_direction, reflect_direction), 0.0), material.shininess);
	vec3 specular = light.specular * spec * vec3(texture(material.specular, texture_coord * material.tiling_factor) * material.tint_color);;

	vec4 result = vec4(ambient + diffuse + specular, 1.0);
	
	output_color = result;
}