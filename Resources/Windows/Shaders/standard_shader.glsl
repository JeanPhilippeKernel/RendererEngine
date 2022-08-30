#type vertex
#version 450 core

precision mediump float;

/*
 * Uniform global variables
 */
layout (std140, binding = 0) uniform ViewProjectionMatrices
{
	mat4 view;
	mat4 projection;
};

uniform mat4 model;

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
	fragment_position = vec3((model * vec4(a_position, 1.0f)).xyz);
	normal_vec = mat3(transpose(inverse(model))) * a_normal;
	texture_coord = a_texture_coord;

	gl_Position = projection * view * vec4(fragment_position, 1.0);
}

#type fragment
#version  450 core

precision mediump float;

layout (std140, binding = 1) uniform CameraProperties
{
	vec4 position;
} camera;

layout (std140, binding = 2) uniform DirectionalLightProperties
{
	vec4 direction;
    vec4 ambient;
    vec4 diffuse;
    vec4 specular;
} directional_light;

struct StandardMaterial
{
	float tiling_factor;
	float shininess;
	vec4 diffuse_tint_color;
	vec4 specular_tint_color;
    sampler2D diffuse;
    sampler2D specular;
};

/*
 * Fragment input variables
 */
in vec2 texture_coord;
in vec3 fragment_position;
in vec3 normal_vec;

uniform StandardMaterial material;

/*
 * Fragment output variables
 */
out vec4 output_color;

void main()
{
	vec4 diffuse_map_texture = texture(material.diffuse, texture_coord * material.tiling_factor) * material.diffuse_tint_color;
	vec4 specular_map_texture = texture(material.specular, texture_coord * material.tiling_factor) * material.specular_tint_color;

	// ambient color
	vec3 ambient = directional_light.ambient.xyz * diffuse_map_texture.xyz;

	// diffuse color
	vec3 norm = normalize(normal_vec);
	vec3 light_direction = normalize(-directional_light.direction.xyz);
	float diff = max(dot(norm, light_direction), 0.0);
	vec3 diffuse = directional_light.diffuse.xyz * diff * diffuse_map_texture.xyz;
	
	//specular color
	vec3 view_direction = normalize(camera.position.xyz - fragment_position);
	vec3 reflect_direction = reflect(-light_direction, norm);
	float spec = pow(max(dot(view_direction, reflect_direction), 0.0), material.shininess);
	vec3 specular = directional_light.specular.xyz * spec * specular_map_texture.xyz;

	vec4 result = vec4(ambient + diffuse + specular, 1.0);
	
	output_color = result;
}