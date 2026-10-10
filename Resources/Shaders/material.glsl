// Material bindings and accessor — fragment-stage only.
// Declares set=1 (TextureArray, LinearWrapSampler) and MatSB.
// Does NOT pull in geometry buffers so no duplicate-binding conflicts with vertex stage.

#include "material_types.glsl"
#include "texture_bindings.glsl"

layout(std140, set = 0, binding = 5) readonly buffer MatSB
{
    MaterialData Data[];
}
MaterialDataBuffer;

MaterialData FetchMaterial(uint dataIndex)
{
    return MaterialDataBuffer.Data[dataIndex];
}
