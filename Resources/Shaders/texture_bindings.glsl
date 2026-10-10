// Set=1 bindings — global bindless texture array and sampler.
// Safe to include in fragment shaders without pulling in geometry buffers.
#extension GL_EXT_nonuniform_qualifier : require

#define INVALID_MAP_HANDLE 0xFFFFFFFFu

layout(set = 1, binding = 0) uniform texture2D TextureArray[];
layout(set = 1, binding = 1) uniform sampler LinearWrapSampler;
