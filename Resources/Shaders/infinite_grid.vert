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
    scaleFactor   = 80.0;

    vec3 posScale = pos * scaleFactor;
    uv            = posScale.xz;
    gl_Position   = Camera.Projection * Camera.View * vec4(posScale, 1.0);
}