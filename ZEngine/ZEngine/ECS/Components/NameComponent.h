#pragma once
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::ECS::Components
{
    // Display name shown in the Outliner and used for debug identification.
    // Intentionally exceeds the 64-byte cache-line guideline — a display
    // name that fits in one cache line (128 bytes) is a justified exception.
    struct NameComponent
    {
        char Value[128] = {};
    };

    static_assert(sizeof(NameComponent) <= 128, "NameComponent exceeds 128 bytes");
    static_assert(alignof(NameComponent) <= 16, "NameComponent misaligned");
} // namespace ZEngine::ECS::Components
