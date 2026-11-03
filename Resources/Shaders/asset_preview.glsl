layout(set = 0, binding = 0) uniform UBPreview
{
    mat4  ViewProjection;
    vec4  CenterRadius;
    vec4  Albedo;
    vec4  Emissive;
    vec4  Surface;
    uvec4 Maps0;
    uvec4 Maps1;
}
Preview;
