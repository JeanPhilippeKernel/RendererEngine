#pragma once
#include <Tetragrama/EditorTarget.h>
#include <Tetragrama/Gizmo/GizmoState.h>
#include <ZEngine/Core/Maths/Geometry.h>
#include <ZEngine/ECS/AuthoredComponentValues.h>

namespace Tetragrama
{
    class EditorSession;
}

namespace Tetragrama::Gizmo
{
    /// Active selected ECS entity, independent of actor facades, mode and custom pivot.
    struct LightFrame
    {
        EntityTarget                      Target = {};
        ZEngine::Core::Maths::Mat4f       World  = {};
        ZEngine::ECS::LocalTransformValue Pose   = {};
        ZEngine::ECS::AuthoredLightValue  Light  = {};
        ZEngine::Core::Maths::Vec3f       Origin = {}, Right = {1.f, 0.f, 0.f}, Up = {0.f, 1.f, 0.f}, Forward = {0.f, 0.f, 1.f};
        uint64_t                          SelectionRevision = 0, StructuralRevision = 0, HierarchyRevision = 0, WorldRevision = 0, ParentWorldRevision = 0;
        bool                              Valid = false;
    };
    static_assert(sizeof(LightFrame) <= 320);

    struct LightHandle
    {
        ZEngine::Core::Maths::Vec3f      Position = {}, Direction = {};
        ZEngine::ECS::AuthoredFloatField Field     = ZEngine::ECS::AuthoredFloatField::LightRange;
        float                            Parameter = 0.f;
    };

    [[nodiscard]] inline constexpr bool IsLightHandle(Handle handle)
    {
        return handle >= Handle::LightRangePositiveX && handle <= Handle::SpotOuterAngle;
    }
    [[nodiscard]] bool ResolveLightFrame(EditorSession& session, LightFrame& output);
    [[nodiscard]] bool ValidLightGeometry(const LightFrame& frame);
    [[nodiscard]] bool SameLightFrame(const LightFrame& a, const LightFrame& b);
    [[nodiscard]] bool ResolveLightHandle(const LightFrame& frame, Handle handle, LightHandle& output);

    struct LightBegin
    {
        LightFrame                                      Expected   = {};
        ZEngine::Core::Maths::Geometry::ProjectionFrame Projection = {};
        ZEngine::Core::Maths::Geometry::Viewport        Bounds     = {};
        ZEngine::Core::Maths::Vec2f                     Pointer    = {};
        uint64_t                                        Key = 0, PointerSerial = 0;
        Handle                                          Constraint = Handle::None;
    };

    /// Pointer mapping only. The existing typed Field transaction owns values/history.
    struct LightInteraction
    {
        ZEngine::Core::Maths::Geometry::TranslationReference Mapping     = {};
        LightFrame                                           Frame       = {};
        LightHandle                                          HandleFrame = {};
        SnapSetting                                          Snap        = {};
        uint64_t                                             Key = 0, PointerSerial = 0;
        Handle                                               Constraint = Handle::None;
    };
    static_assert(sizeof(LightInteraction) <= 1024);

    struct LightSnapshot
    {
        LightFrame Frame = {};
        float      Value = 0.f;
        uint64_t   Key = 0, PointerSerial = 0;
        Handle     Constraint = Handle::None;
    };
    static_assert(sizeof(LightSnapshot) <= 384);
} // namespace Tetragrama::Gizmo
