#version 460
layout (location = 0) in vec3 pos;
layout (location = 0) out vec3 dir;

void main()
{
    dir                 = vec3(pos.x, -pos.y, pos.z);
    vec4 position       = Camera.Projection * Camera.RotScaleView * vec4(pos, 1.0f);
    gl_Position         = position.xyzw;
}
