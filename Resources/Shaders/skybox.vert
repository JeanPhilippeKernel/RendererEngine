#version 460

layout(location = 0) out vec3 dir;

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
    // A full-screen triangle has no clipped cube edges.  Reconstruct a world
    // ray at the far plane so the cubemap remains translation-invariant.
    vec2 ndc    = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
    vec4 world  = Camera.InvViewProj * vec4(ndc, 1.0, 1.0);
    dir         = world.xyz / world.w - Camera.Position.xyz;
    gl_Position = vec4(ndc, 1.0, 1.0);
}
