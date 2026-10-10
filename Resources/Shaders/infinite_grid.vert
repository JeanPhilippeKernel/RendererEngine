#version 460
layout(location = 0) in vec3 pos;

layout(location = 0) out vec2 uv;
layout(location = 1) out vec2 camXZ;
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
    float fadeRadius;
    float lineWidth;
    int   maxLOD;
    float groundY;
    vec2  _pad;
}
Settings;

void main()
{
    // 0.001 epsilon avoids z-fighting against geometry exactly on the ground plane
    vec3 worldPos = pos + vec3(Camera.Position.x, Settings.groundY + 0.001, Camera.Position.z);

    uv            = worldPos.xz;
    camXZ         = Camera.Position.xz;
    camHeight     = Camera.Position.y - Settings.groundY;
    gl_Position   = Camera.Projection * Camera.View * vec4(worldPos, 1.0);
}
