#type vertex
#version 450 core

precision mediump float;

/*
 * Uniform global variables
 */
layout (std140, binding = 0) uniform CameraProperties
{
	vec4 Position;
	mat4 View;
	mat4 Projection;
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
	Output.CameraPosition = Camera.Position;

	gl_Position = Camera.Projection * Camera.View * vec4(Output.FragmentPosition, 1.0);
}

#type fragment
#version  450 core

precision mediump float;

layout (std140, binding = 1) uniform DirectionalLightProperties
{
	vec4 Direction;
    vec4 Ambient;
    vec4 Diffuse;
    vec4 Specular;
} DirectionalLight;

struct StandardMaterial
{
	float TilingFactor;
	float Shininess;
	vec4 DiffuseTintColor;
	vec4 SpecularTintColor;
    sampler2D Diffuse;
    sampler2D Specular;
};

/*
 * Fragment input variables
 */
struct VertexInput
{
	vec2 TextureCoord;
	vec3 FragmentPosition;
	vec3 Normal;
	vec4 CameraPosition;
};

layout (location = 0) in VertexInput Input;

uniform StandardMaterial Material;

/*
 * Fragment output variables
 */
out vec4 outputColor;

vec4 ComputeDirectionalLight(vec4 diffuseTexture, vec4 specularTexture, float shininess)
{
	// ambient color
	vec3 ambient = DirectionalLight.Ambient.xyz * diffuseTexture.xyz;

	// diffuse color
	vec3 norm = normalize(Input.Normal);
	vec3 lightDirection = normalize(-DirectionalLight.Direction.xyz);
	float diff = max(dot(norm, lightDirection), 0.0);
	vec3 diffuse = DirectionalLight.Diffuse.xyz * diff * diffuseTexture.xyz;
	
	//specular color
	vec3 viewDirection = normalize(Input.CameraPosition.xyz - Input.FragmentPosition);
	vec3 reflectDirection = reflect(-lightDirection, norm);
	float spec = pow(max(dot(viewDirection, reflectDirection), 0.0), shininess);
	vec3 specular = DirectionalLight.Specular.xyz * spec * specularTexture.xyz;

	vec4 result = vec4(ambient + diffuse + specular, 1.0);

	return result;
}

void main()
{
	vec4 diffuseMapTexture = texture(Material.Diffuse, Input.TextureCoord * Material.TilingFactor) * Material.DiffuseTintColor;
	vec4 specularMapTexture = texture(Material.Specular, Input.TextureCoord * Material.TilingFactor) * Material.SpecularTintColor;

	outputColor = ComputeDirectionalLight(diffuseMapTexture, specularMapTexture, Material.Shininess);
}