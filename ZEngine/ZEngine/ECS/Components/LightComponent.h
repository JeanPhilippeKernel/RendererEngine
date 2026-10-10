#pragma once
#include <ZEngine/ZEngineDef.h>
#include <cstdint>

namespace ZEngine::ECS::Components
{
    struct LightComponent
    {
        enum class Type : uint8_t
        {
            Directional = 0,
            Point       = 1,
            Spot        = 2,
        };

        Type    LightType = Type::Directional;
        float   Intensity = 1.f;
        float   Range     = 10.f; // point and spot only
        float   SpotAngle = 45.f; // spot only, degrees
        float   Color[3]  = {1.f, 1.f, 1.f};
        uint8_t _pad[3]   = {};
    };

    static_assert(sizeof(LightComponent) <= 32, "LightComponent exceeds 32 bytes");
    static_assert(alignof(LightComponent) <= 16, "LightComponent misaligned");
} // namespace ZEngine::ECS::Components
