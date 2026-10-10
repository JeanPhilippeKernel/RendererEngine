// Full bindings — geometry pipeline + global bindless textures + sampler.
// Use this in shaders that sample from TextureArray (e.g. g_buffer.frag).
// For passes that only need geometry (e.g. depth pre-pass vertex), use geometry_bindings.glsl.
#extension GL_EXT_nonuniform_qualifier : require

#include "geometry_bindings.glsl"

#define INVALID_MAP_HANDLE 0xFFFFFFFFu

layout(set = 1, binding = 0) uniform texture2D TextureArray[];
layout(set = 1, binding = 1) uniform sampler LinearWrapSampler;
