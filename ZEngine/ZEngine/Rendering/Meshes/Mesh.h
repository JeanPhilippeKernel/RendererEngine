#pragma once
#include <Rendering/GPUTypes.h>

#define INVALID_MAP_HANDLE 0xFFFFFFFFu

namespace ZEngine::Rendering::Meshes
{

    enum class MeshType
    {
        CUSTOM = 0,
        CUBE   = 1,
        QUAD   = 2,
        SQUARE = 3
    };

    struct MeshVNext
    {
        uint32_t VertexCount          = 0;
        uint32_t IndexCount           = 0;
        uint32_t VertexOffset         = 0;
        uint32_t IndexOffset          = 0;
        uint32_t StreamOffset         = 0;
        uint32_t IndexStreamOffset    = 0;
        uint32_t VertexUnitStreamSize = 0;
        uint32_t IndexUnitStreamSize  = 0;
        uint32_t TotalByteSize        = 0;
    };

    struct MeshMaterial
    {
        gpuvec4  AmbientColor   = 1.0f;
        gpuvec4  EmissiveColor  = 0.0f;
        gpuvec4  AlbedoColor    = 1.0f;
        gpuvec4  SpecularColor  = 1.0f;
        gpuvec4  RoughnessColor = 1.0f;
        gpuvec4  Factors        = 1.0f; // {x : transparency, y : Metallic, z : AlphaTest, w : _padding}
        uint64_t EmissiveMap    = INVALID_MAP_HANDLE;
        uint64_t AlbedoMap      = INVALID_MAP_HANDLE;
        uint64_t SpecularMap    = INVALID_MAP_HANDLE;
        uint64_t NormalMap      = INVALID_MAP_HANDLE;
        uint64_t OpacityMap     = INVALID_MAP_HANDLE;
        uint64_t _padding       = INVALID_MAP_HANDLE;
    };
} // namespace ZEngine::Rendering::Meshes
