#pragma once
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::ECS::Components
{
    struct CameraComponent
    {
        float   FovY        = 60.f; // vertical field of view, degrees
        float   Near        = 0.1f;
        float   Far         = 1000.f;
        float   AspectRatio = 16.f / 9.f;
        bool    IsMain      = false; // exactly one camera should be main
        uint8_t _pad[3]     = {};
    };

    static_assert(sizeof(CameraComponent) <= 24, "CameraComponent exceeds 24 bytes");
    static_assert(alignof(CameraComponent) <= 16, "CameraComponent misaligned");
} // namespace ZEngine::ECS::Components
