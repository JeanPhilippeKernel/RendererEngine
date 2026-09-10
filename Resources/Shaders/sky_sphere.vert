#version 460

layout(location = 0) out vec3 v_ray_dir;

layout(set = 0, binding = 0) uniform UBCamera
{
    mat4 View;
    mat4 Projection;
    vec4 Position;
    mat4 InvViewProj;
}
Camera;

void main()
{
    // Fullscreen triangle — no vertex buffer required.
    // Vertex 0: (-1,-1), Vertex 1: (3,-1), Vertex 2: (-1, 3).
    vec2 pos        = vec2((gl_VertexIndex == 1) ? 3.0 : -1.0, (gl_VertexIndex == 2) ? 3.0 : -1.0);

    // Render at maximum depth so the sky is drawn only on background pixels.
    gl_Position     = vec4(pos, 1.0, 1.0);

    // Reconstruct world-space ray direction from clip position.
    vec4 world      = Camera.InvViewProj * vec4(pos, 1.0, 1.0);
    v_ray_dir       = normalize(world.xyz / world.w - Camera.Position.xyz);
}
