#version 460

layout(set = 2, binding = 0) uniform utexture2D ObjectId;
layout(std430, set = 2, binding = 1) readonly buffer SelectionTokenStates
{
    uint State[];
}
TokenStates;
layout(set = 2, binding = 2) uniform sampler ObjectIdSampler;

layout(location = 0) out vec4 OutColor;

const uint kNoOutline = 0u;
const uint kHovered   = 1u;
const uint kSelected  = 2u;

uint       state_at(ivec2 pixel, ivec2 extent)
{
    const ivec2 clamped = clamp(pixel, ivec2(0), extent - ivec2(1));
    const uint  token   = texelFetch(usampler2D(ObjectId, ObjectIdSampler), clamped, 0).r;
    return token <= 8192u ? TokenStates.State[token] : kNoOutline;
}

void main()
{
    const ivec2 extent          = textureSize(usampler2D(ObjectId, ObjectIdSampler), 0);
    const ivec2 pixel           = ivec2(gl_FragCoord.xy);
    const uint  center          = state_at(pixel, extent);
    const uint  left            = state_at(pixel + ivec2(-1, 0), extent);
    const uint  right           = state_at(pixel + ivec2(1, 0), extent);
    const uint  up              = state_at(pixel + ivec2(0, -1), extent);
    const uint  down            = state_at(pixel + ivec2(0, 1), extent);
    const uint  neighbour_state = max(max(left, right), max(up, down));
    // Emit only on the visible object's inside edge. Emitting from both sides
    // of a state transition makes the line two physical pixels wide and gives
    // the background an outline of its own.
    const bool  edge            = center != kNoOutline && center >= neighbour_state && (center != left || center != right || center != up || center != down);
    const vec4  color           = center == kSelected ? vec4(1.0, 0.57, 0.12, 0.86) : center == kHovered ? vec4(0.38, 0.68, 1.0, 0.72) : vec4(0.0);
    OutColor                    = edge ? color : vec4(0.0);
}
