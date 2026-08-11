#pragma once
#include <cstdint>

namespace ZEngine::Managers
{
    enum class AssetType : uint8_t
    {
        MESH           = 0,
        MATERIAL       = 1,
        TEXTURE        = 2,
        MESH_HIERARCHY = 3,
    };

    // Packed slot index into AssetManager's flat CPU buffer.
    // High 4 bits = AssetType, low 28 bits = flat array index.
    using AssetHandle = uint32_t;
} // namespace ZEngine::Managers
