// Material data structure.
// No descriptor bindings — include wherever MaterialData is needed.
// Note: MaterialData uses uint64_t; include material.glsl (not this file directly)
// in shader stages that need the full material pipeline — it enables Int64 there.

struct MaterialData
{
    vec4     Ambient;
    vec4     Emissive;
    vec4     Albedo;
    vec4     Specular;
    vec4     Roughness;
    vec4     Factors; // {x : transparency, y : Metallic, z : AlphaTest, w : _padding}

    uint64_t EmissiveMap;
    uint64_t AlbedoMap;
    uint64_t SpecularMap;
    uint64_t NormalMap;
    uint64_t OpacityMap;
    uint64_t _padding;
};
