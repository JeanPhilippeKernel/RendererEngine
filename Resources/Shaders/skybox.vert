#version 460
layout (location = 0) in vec3 pos;
layout (location = 0) out vec3 dir;

layout(set = 0, binding = 0) uniform UBCamera
{ 
    mat4 View;
    mat4 RotScaleView;
    mat4 Projection;
    vec4 Position;
} Camera;

void main()
{
    dir                 = normalize(vec3(pos.x, -pos.y, pos.z));
    vec4 position       = Camera.Projection * Camera.RotScaleView * vec4(pos, 1.0f);
    gl_Position         = position.xyww;
}
