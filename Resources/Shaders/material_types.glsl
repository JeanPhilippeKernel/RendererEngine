// Material data structure.
// No descriptor bindings — include wherever MaterialData is needed.
// Texture map fields use uint (32-bit bindless indices) — no shaderInt64 required.
struct MaterialData
{
    vec4 Ambient;
    vec4 Emissive;
    vec4 Albedo;
    vec4 Specular;
    vec4 Roughness;
    vec4 Factors; // {x : transparency, y : Metallic, z : AlphaTest, w : _padding}

    uint EmissiveMap;
    uint AlbedoMap;
    uint SpecularMap;
    uint NormalMap;
    uint OpacityMap;
    uint _pad;
};
