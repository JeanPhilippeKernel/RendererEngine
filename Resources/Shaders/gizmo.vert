#version 460
layout(location = 0) in vec4 A;
layout(location = 1) in vec4 B;
layout(location = 2) in vec4 C;
layout(location = 3) in vec4 Parameters;
layout(location = 4) in vec4 Color;
layout(location = 5) in vec4 Outline;

layout(set = 0, binding = 0) uniform UBCamera
{
    mat4 View;
    mat4 Projection;
    vec4 Position;
    mat4 InvViewProj;
}
Camera;
layout(push_constant) uniform GizmoSettings
{
    vec2 Extent;
}
Settings;

layout(location = 0) noperspective out vec3 Local;
layout(location = 1) flat out vec4 Shape;
layout(location = 2) flat out vec4 Style;
layout(location = 3) flat out vec4 FillColor;
layout(location = 4) flat out vec4 OutlineColor;
layout(location = 5) flat out vec3 StartJoinPlane;
layout(location = 6) flat out vec3 EndJoinPlane;

const vec2 Corners[6] = vec2[](vec2(0, -1), vec2(1, -1), vec2(1, 1), vec2(1, 1), vec2(0, 1), vec2(0, -1));

void       main()
{
    Local          = vec3(0);
    Shape          = vec4(0);
    Style          = Parameters;
    FillColor      = Color;
    OutlineColor   = Outline;
    StartJoinPlane = vec3(0);
    EndJoinPlane   = vec3(0);
    gl_Position    = vec4(2, 2, 2, 1);
    int  kind      = int(Parameters.w);
    vec4 a         = Camera.Projection * Camera.View * vec4(A.xyz, 1);
    if (kind == 0)
    {
        vec4 b = Camera.Projection * Camera.View * vec4(B.xyz, 1);
        vec4 c = Camera.Projection * Camera.View * vec4(C.xyz, 1);
        if (min(a.w, min(b.w, c.w)) <= 0)
            return;
        int index    = gl_VertexIndex < 3 ? gl_VertexIndex : 0;
        gl_Position  = index == 0 ? a : (index == 1 ? b : c);
        Local[index] = 1;
        return;
    }
    if (a.w <= 0)
        return;
    vec2 corner = Corners[gl_VertexIndex];
    if (kind == 1)
    {
        vec4 b = Camera.Projection * Camera.View * vec4(B.xyz, 1);
        if (b.w <= 0)
            return;
        vec2  delta     = (b.xy / b.w - a.xy / a.w) * Settings.Extent * 0.5;
        float lengthPx  = length(delta);
        vec2  along     = lengthPx > 0 ? delta / lengthPx : vec2(1, 0);
        vec2  across    = vec2(-along.y, along.x);
        float halfWidth = A.w * 0.5;
        int   flags     = int(Parameters.y);
        float cap       = (flags & 1) != 0 ? halfWidth : 0;
        if ((flags & 2) != 0)
            StartJoinPlane = vec3(C.xy, -dot(C.xy, (a.xy / a.w * 0.5 + 0.5) * Settings.Extent));
        if ((flags & 4) != 0)
        {
            vec2 normal  = vec2(C.z, Parameters.z);
            EndJoinPlane = vec3(normal, -dot(normal, (b.xy / b.w * 0.5 + 0.5) * Settings.Extent));
        }
        float x         = mix(-cap - 1, lengthPx + cap + 1, corner.x);
        float y         = corner.y * (halfWidth + 1);
        vec4  clip      = corner.x < 0.5 ? a : b;
        vec2  offsetPx  = along * (x - corner.x * lengthPx) + across * y;
        clip.xy        += offsetPx * (2 / Settings.Extent) * clip.w;
        gl_Position     = clip;
        Local           = vec3(x, y, 0);
        Shape           = vec4(lengthPx, halfWidth, B.w, C.w);
        return;
    }
    float halfSize  = Parameters.y * 0.5;
    vec2  offsetPx  = vec2(corner.x * 2 - 1, corner.y) * (halfSize + 1);
    a.xy           += offsetPx * (2 / Settings.Extent) * a.w;
    gl_Position     = a;
    Local           = vec3(offsetPx, 0);
    Shape           = vec4(halfSize, Parameters.z, Parameters.x, 0);
}
