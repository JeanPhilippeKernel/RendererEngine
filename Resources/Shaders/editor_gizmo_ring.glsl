#ifndef EDITOR_GIZMO_RING_GLSL
#define EDITOR_GIZMO_RING_GLSL

// Kept valid in both GLSL and C++ so CPU regression tests exercise the exact
// tessellation used by the vertex shader. Each segment is a nondegenerate quad.
vec3 ringVertex(uint index)
{
    const uint  segment = index / 6u;
    const uint  corner  = index % 6u;
    const float a0      = 6.28318530718 * float(segment) / 48.0;
    const float a1      = 6.28318530718 * float(segment + 1u) / 48.0;
    const float radius  = (corner == 0u || corner == 2u || corner == 3u) ? 0.79 : 0.85;
    const float angle   = (corner == 0u || corner == 1u || corner == 4u) ? a0 : a1;
    // Axis models put their rotation axis in column X. The ring must therefore
    // lie in local YZ, matching the controller's hit-test and drag constraint.
    return vec3(0.0, cos(angle) * radius, sin(angle) * radius);
}

#endif
