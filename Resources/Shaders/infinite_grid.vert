#version 460
layout(location = 0) in vec3 pos;

layout(location = 0) out vec2 uv;
layout(location = 1) out float scaleFactor;
layout(location = 2) out float camHeight;

layout(set = 0, binding = 0) uniform UBCamera
{
    mat4 View;
    mat4 Projection;
    vec4 Position;
}
Camera;

layout(push_constant) uniform GridSettings
{
    vec4  colorThin;
    vec4  colorThick;
    vec4  colorXAxis;
    vec4  colorZAxis;
    float cellSize;
    float fadeStrength;
}
Settings;

void main()
{
    // scaleFactor matches the half-extent of the quad geometry
    scaleFactor   = abs(pos.x);
    vec3 worldPos = pos + vec3(Camera.Position.x, 0.0, Camera.Position.z);

    uv            = worldPos.xz;
    camHeight     = Camera.Position.y;
    gl_Position   = Camera.Projection * Camera.View * vec4(worldPos, 1.0);
}
