#type vertex
#version 450 core

precision mediump float;

/*
 * Uniform global variables
 */
layout (std140, binding = 0) uniform CameraProperties
{
	vec4 position;
	mat4 view;
	mat4 projection;
} Camera;

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
struct VertexOutput
{
	vec2 TextureCoord;
	vec3 FragmentPosition;
	vec3 Normal;
	vec4 CameraPosition;
};

layout (location = 0) out VertexOutput Output;

void main()
{
	Output.FragmentPosition = vec3((model * vec4(a_position, 1.0f)).xyz);
	Output.Normal = mat3(transpose(inverse(model))) * a_normal;
	Output.TextureCoord = a_texture_coord;
	Output.CameraPosition = Camera.position;

	gl_Position = Camera.projection * Camera.view * vec4(Output.FragmentPosition, 1.0);
}

#type fragment
#version  450 core

precision mediump float;

layout (std140, binding = 1) uniform DirectionalLightProperties
{
	vec4 direction;
    vec4 ambient;
    vec4 diffuse;
    vec4 specular;
} DirectionalLight;

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
struct VertexOutput
{
	vec2 TextureCoord;
	vec3 FragmentPosition;
	vec3 Normal;
	vec4 CameraPosition;
};

layout (location = 0) in VertexOutput Output;

uniform StandardMaterial material;

/*
 * Fragment output variables
 */
out vec4 output_color;

void main()
{
	vec4 diffuse_map_texture = texture(material.diffuse, Output.TextureCoord * material.tiling_factor) * material.diffuse_tint_color;
	vec4 specular_map_texture = texture(material.specular, Output.TextureCoord * material.tiling_factor) * material.specular_tint_color;

	// ambient color
	vec3 ambient = DirectionalLight.ambient.xyz * diffuse_map_texture.xyz;

	// diffuse color
	vec3 norm = normalize(Output.Normal);
	vec3 light_direction = normalize(-DirectionalLight.direction.xyz);
	float diff = max(dot(norm, light_direction), 0.0);
	vec3 diffuse = DirectionalLight.diffuse.xyz * diff * diffuse_map_texture.xyz;
	
	//specular color
	vec3 view_direction = normalize(Output.CameraPosition.xyz - Output.FragmentPosition);
	vec3 reflect_direction = reflect(-light_direction, norm);
	float spec = pow(max(dot(view_direction, reflect_direction), 0.0), material.shininess);
	vec3 specular = DirectionalLight.specular.xyz * spec * specular_map_texture.xyz;

	vec4 result = vec4(ambient + diffuse + specular, 1.0);
	
	output_color = result;
}