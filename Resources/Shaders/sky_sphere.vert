#version 460

layout(set = 0, binding = 0) uniform UBCamera
{
    mat4 View;
    mat4 Projection;
    vec4 Position;
    mat4 InvViewProj;
}
Camera;

layout(push_constant) uniform SkySpherePushConstants
{
    vec4  HorizonColor;
    vec4  ZenithColor;
    vec4  GroundColor;
    vec4  SunDirection;
    float SunDiscAngularRadiusRadians;
    float SunDiscIntensity;
    float HorizonSharpness;
    float ShowSunDisc;
}
Sky;

layout(location = 0) out vec3 ViewDirection;

vec3 unproject(vec2 ndc, float depth)
{
    vec4  world  = Camera.InvViewProj * vec4(ndc, depth, 1.0);
    float safe_w = abs(world.w) > 1.0e-6 ? world.w : (world.w < 0.0 ? -1.0e-6 : 1.0e-6);
    return world.xyz / safe_w;
}

void main()
{
    const vec2  ndc            = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
    const float far_depth      = Sky.SunDirection.w;
    const vec3  near_point     = unproject(ndc, 1.0 - far_depth);
    const vec3  far_point      = unproject(ndc, far_depth);
    const vec3  ray            = far_point - near_point;
    const float length_squared = dot(ray, ray);
    ViewDirection              = length_squared > 1.0e-12 ? ray * inversesqrt(length_squared) : vec3(0.0, 1.0, 0.0);
    gl_Position                = vec4(ndc, far_depth, 1.0);
}
