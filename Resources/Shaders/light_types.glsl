// Light data structures — composable by any pass that needs lighting.
// No descriptor bindings.

struct DirectionalLight
{
    vec4 Direction;
    vec4 Ambient;
    vec4 Diffuse;
    vec4 Specular;
};

struct PointLight
{
    vec4  Position;
    vec4  Ambient;
    vec4  Diffuse;
    vec4  Specular;

    float Constant;
    float Linear;
    float Quadratic;
    float _padding;
};

struct SpotLight
{
    vec4  Position;
    vec4  Direction;
    vec4  Ambient;
    vec4  Diffuse;
    vec4  Specular;

    float CutOff;
    float Constant;
    float Linear;
    float Quadratic;
};
