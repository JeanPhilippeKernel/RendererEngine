#version 460
layout(location = 0) in vec3 pos;
layout(location = 0) out vec2 uv;
layout(location = 1) out float scaleFactor;

layout(set = 0, binding = 0) uniform UBCamera
{
    mat4 View;
    mat4 Projection;
    vec4 Position;
}
Camera;

void main()
{
    scaleFactor   = 1000.0f;
    vec3 worldPos = pos + vec3(Camera.Position.x, 0.0, Camera.Position.z);

    uv            = worldPos.xz;
    gl_Position   = Camera.Projection * Camera.View * vec4(worldPos, 1.0);
}
