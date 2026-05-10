#version 460

layout(location = 0) in vec2 outUV;

layout(set = 0, binding = 0) uniform texture2D sharedRTAsTex;
layout(set = 1, binding = 1) uniform sampler LinearWrapSampler;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 color = texture(sampler2D(sharedRTAsTex, LinearWrapSampler), outUV);
    outColor   = color;
}
