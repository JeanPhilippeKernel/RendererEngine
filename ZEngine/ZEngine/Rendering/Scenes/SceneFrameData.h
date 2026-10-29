#pragma once
#include <ZEngine/Rendering/Scenes/RenderScene.h>

namespace ZEngine::Rendering::Scenes
{
    /// @brief Immutable mesh draw input. Entity is an opaque value used only to
    /// build a frame-local selection token table; the render thread never touches ECS.
    struct SceneDrawInstance
    {
        Core::Maths::Mat4f Transform = {};
        uuids::uuid        MeshUUID  = {};
        ECS::EntityID      Entity    = ECS::INVALID_ENTITY;
    };

    /// @brief Immutable scene authoring snapshot. Instance storage belongs to a retained mailbox slot.
    /// No scene/ECS pointers, and no ownership of GPU resources or authored entity identity.
    struct SceneFrameData
    {
        static constexpr uint32_t MaxInstances  = 65536;

        const SceneDrawInstance*  Instances     = nullptr;
        uint32_t                  InstanceCount = 0;
        LightArrayUBO             Lights        = {};
        SceneGridRenderSnapshot   Grid          = {};
        bool                      HasScene      = false;
    };
} // namespace ZEngine::Rendering::Scenes
