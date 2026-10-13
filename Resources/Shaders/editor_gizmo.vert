#version 460

layout(set = 0, binding = 0) uniform UBCamera
{
    mat4 View;
    mat4 Projection;
    vec4 Position;
    mat4 InvViewProj;
}
Camera;

layout(push_constant) uniform GizmoPrimitive
{
    mat4 Model;
    vec4 Color;
    uint Primitive;
    uint Padding0;
    uint Padding1;
    uint Padding2;
}
Push;

layout(location = 0) out vec4 OutColor;

const vec3 kBoxCorners[8]  = vec3[8](vec3(0.0, -1.0, -1.0), vec3(1.0, -1.0, -1.0), vec3(1.0, 1.0, -1.0), vec3(0.0, 1.0, -1.0), vec3(0.0, -1.0, 1.0), vec3(1.0, -1.0, 1.0), vec3(1.0, 1.0, 1.0), vec3(0.0, 1.0, 1.0));
const int  kBoxIndices[36] = int[36](0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 5, 4, 7, 7, 6, 5, 4, 0, 3, 3, 7, 4, 3, 2, 6, 6, 7, 3, 4, 5, 1, 1, 0, 4);

vec3       axisVertex(uint index)
{
    if (index < 36u)
    {
        vec3 p  = kBoxCorners[kBoxIndices[index]];
        p.x     = mix(0.10, 0.82, p.x);
        p.yz   *= 0.026;
        return p;
    }

    const vec3 cone[4]     = vec3[4](vec3(0.80, -0.075, -0.075), vec3(0.80, 0.075, -0.075), vec3(0.80, 0.075, 0.075), vec3(0.80, -0.075, 0.075));
    const int  indices[12] = int[12](0, 1, 4, 1, 2, 4, 2, 3, 4, 3, 0, 4);
    const int  local       = indices[index - 36u];
    return local == 4 ? vec3(1.12, 0.0, 0.0) : cone[local];
}

vec3 planeVertex(uint index)
{
    const vec2 positions[6] = vec2[6](vec2(0.24, 0.24), vec2(0.53, 0.24), vec2(0.53, 0.53), vec2(0.53, 0.53), vec2(0.24, 0.53), vec2(0.24, 0.24));
    return vec3(positions[index], 0.0);
}

vec3 ringVertex(uint index)
{
    const uint  segment = index / 6u;
    const uint  corner  = index % 6u;
    const float a0      = 6.28318530718 * float(segment) / 48.0;
    const float a1      = 6.28318530718 * float(segment + 1u) / 48.0;
    const float radius  = (corner == 0u || corner == 2u || corner == 3u) ? 0.79 : 0.85;
    const float angle   = (corner == 0u || corner == 1u || corner == 2u) ? a0 : a1;
    return vec3(cos(angle) * radius, sin(angle) * radius, 0.0);
}

vec3 cubeVertex(uint index)
{
    vec3 p = kBoxCorners[kBoxIndices[index]];
    return (p - vec3(0.5)) * 0.13;
}

void main()
{
    vec3 local  = Push.Primitive == 0u ? axisVertex(gl_VertexIndex) : (Push.Primitive == 1u ? planeVertex(gl_VertexIndex) : (Push.Primitive == 2u ? ringVertex(gl_VertexIndex) : cubeVertex(gl_VertexIndex)));
    gl_Position = Camera.Projection * Camera.View * Push.Model * vec4(local, 1.0);
    OutColor    = Push.Color;
}
