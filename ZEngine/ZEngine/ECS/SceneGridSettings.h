#pragma once
#include <ZEngine/Core/Maths/Quaternion.h>
#include <ZEngine/Core/Maths/Vec.h>
#include <cstdint>

namespace ZEngine::ECS
{
    struct SceneGridSettings
    {
        static constexpr uint32_t MAX_LOD        = 16;

        bool                      Enabled        = true;
        Core::Maths::Vec3f        Origin         = {0.0f, 0.0f, 0.0f};
        Core::Maths::Quatf        Orientation    = {};
        float                     BaseSpacing    = 1.0f;
        uint32_t                  MajorLineEvery = 10;
        Core::Maths::Vec4f        MinorColor     = {0.6f, 0.6f, 0.6f, 1.0f};
        Core::Maths::Vec4f        MajorColor     = {0.3f, 0.3f, 0.3f, 1.0f};
        Core::Maths::Vec4f        UAxisColor     = {0.9f, 0.2f, 0.2f, 1.0f};
        Core::Maths::Vec4f        VAxisColor     = {0.2f, 0.4f, 1.0f, 1.0f};
        float                     FadeDistance   = 500.0f;
        float                     FadeStrength   = 0.5f;
        float                     LineWidth      = 1.5f;
        uint32_t                  MaxLOD         = 5;
    };
} // namespace ZEngine::ECS
