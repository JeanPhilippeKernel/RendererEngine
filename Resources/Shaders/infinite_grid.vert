#version 460
layout(location = 0) out vec2 Ndc;

void main()
{
    // No finite world-space geometry, vertex allocation or camera-following edge.
    Ndc         = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
    gl_Position = vec4(Ndc, 0.0, 1.0);
}
