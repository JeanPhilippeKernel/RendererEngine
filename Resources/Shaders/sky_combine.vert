#version 460

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec3 v_ray_dir;

layout(set = 0, binding = 0) uniform UBCamera
{
    mat4 View;
    mat4 Projection;
    vec4 Position;
    mat4 InvViewProj;
};

void main()
{
    vec2 pos    = vec2((gl_VertexIndex == 1) ? 3.0 : -1.0, (gl_VertexIndex == 2) ? 3.0 : -1.0);
    gl_Position = vec4(pos, 1.0, 1.0);
    v_uv        = pos * 0.5 + 0.5;

    vec4 world  = InvViewProj * vec4(pos, 1.0, 1.0);
    v_ray_dir   = normalize(world.xyz / world.w - Position.xyz);
}
