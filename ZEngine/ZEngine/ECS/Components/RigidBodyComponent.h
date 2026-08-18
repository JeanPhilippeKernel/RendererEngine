#pragma once
#include <ZEngine/ZEngineDef.h>
#include <cstdint>
#include <limits>

namespace ZEngine::ECS::Components
{
    // Physics body descriptor for Jolt integration (Sprint 6).
    // BodyID is assigned by PhysicsWorld on activation and reset on removal.
    struct RigidBodyComponent
    {
        enum class MotionType : uint8_t
        {
            Static    = 0,
            Kinematic = 1,
            Dynamic   = 2,
        };

        MotionType MotionKind  = MotionType::Dynamic;
        uint8_t    _pad[3]     = {};
        float      Mass        = 1.f;
        float      Friction    = 0.5f;
        float      Restitution = 0.f;
        uint32_t   BodyID      = std::numeric_limits<uint32_t>::max(); // UINT32_MAX = inactive
    };

    static_assert(sizeof(RigidBodyComponent) <= 24, "RigidBodyComponent exceeds 24 bytes");
    static_assert(alignof(RigidBodyComponent) <= 16, "RigidBodyComponent misaligned");
} // namespace ZEngine::ECS::Components
