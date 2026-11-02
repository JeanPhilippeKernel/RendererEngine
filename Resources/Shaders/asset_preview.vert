#version 460
#extension GL_GOOGLE_include_directive : require
#include "draw_types.glsl"
#include "asset_preview.glsl"
layout(set = 0, binding = 1) readonly buffer VertexSB { DrawVertex Data[]; } Vertices;
layout(set = 0, binding = 2) readonly buffer IndexSB { uint Data[]; } Indices;
layout(push_constant) uniform DrawOffsets { uint VertexOffset; uint IndexOffset; } Offsets;
layout(location = 0) out vec3 Position;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec2 UV;
void main()
{
    if (Preview.Surface.w > 0.5)
    {
        const ivec2 corners[6] = ivec2[6](ivec2(0, 0), ivec2(1, 0), ivec2(1, 1), ivec2(0, 0), ivec2(1, 1), ivec2(0, 1));
        uint cell = uint(gl_VertexIndex) / 6u;
        vec2 grid = vec2(cell % 64u, cell / 64u) + vec2(corners[gl_VertexIndex % 6]);
        UV = grid / vec2(64, 32);
        float longitude = UV.x * 6.28318530718;
        float latitude = UV.y * 3.14159265359;
        Normal = vec3(sin(latitude) * cos(longitude), cos(latitude), sin(latitude) * sin(longitude));
        Position = Normal;
    }
    else
    {
        uint index = Indices.Data[Offsets.IndexOffset + uint(gl_VertexIndex)];
        DrawVertex v = Vertices.Data[Offsets.VertexOffset + index];
        Position = (vec3(v.x, v.y, v.z) - Preview.CenterRadius.xyz) / Preview.CenterRadius.w;
        Normal = vec3(v.nx, v.ny, v.nz);
        UV = vec2(v.u, v.v);
    }
    gl_Position = Preview.ViewProjection * vec4(Position, 1);
}
