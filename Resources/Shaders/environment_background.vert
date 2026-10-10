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

layout(push_constant) uniform EnvironmentBackgroundPushConstants
{
    vec4  TintIntensity;
    float YawRadians;
    float UseSolidColorFallback;
    float FarDepth;
    float Padding;
}
Environment;

void main()
{
    // A full-screen triangle has no clipped cube edges.  Reconstruct a world
    // ray at the far plane so the cubemap remains translation-invariant.
    vec2  ndc            = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
    vec4  world          = Camera.InvViewProj * vec4(ndc, Environment.FarDepth, 1.0);
    float safe_w         = abs(world.w) > 1.0e-6 ? world.w : (world.w < 0.0 ? -1.0e-6 : 1.0e-6);
    vec3  ray            = world.xyz / safe_w - Camera.Position.xyz;
    float length_squared = dot(ray, ray);
    dir                  = length_squared > 1.0e-12 ? ray * inversesqrt(length_squared) : vec3(0.0, 1.0, 0.0);
    gl_Position          = vec4(ndc, Environment.FarDepth, 1.0);
}
