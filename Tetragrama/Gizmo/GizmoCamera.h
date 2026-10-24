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
    struct CameraFrame
    {
        EntityTarget                      Target = {};
        ZEngine::Core::Maths::Mat4f       World  = {};
        ZEngine::ECS::LocalTransformValue Pose   = {};
        ZEngine::ECS::AuthoredCameraValue Camera = {};
        ZEngine::Core::Maths::Vec3f       Origin = {}, Right = {1.f, 0.f, 0.f}, Up = {0.f, 1.f, 0.f}, Forward = {0.f, 0.f, -1.f};
        uint64_t                          SelectionRevision = 0, StructuralRevision = 0, HierarchyRevision = 0, WorldRevision = 0, ParentWorldRevision = 0;
        bool                              Valid = false;
    };
    static_assert(sizeof(CameraFrame) <= 320);

    struct CameraHandle
    {
        ZEngine::Core::Maths::Vec3f      Position = {}, Direction = {};
        ZEngine::ECS::AuthoredFloatField Field     = ZEngine::ECS::AuthoredFloatField::CameraFar;
        // Radius at capture for FOV, world ray units per unit Far otherwise.
        double                           Parameter = 0.0, Factor = 0.0;
    };
    [[nodiscard]] inline constexpr bool IsCameraHandle(Handle handle)
    {
        return handle >= Handle::CameraFovUpperLeft && handle <= Handle::CameraNearRight;
    }
    [[nodiscard]] bool ResolveCameraFrame(EditorSession& session, CameraFrame& output);
    [[nodiscard]] bool SameCameraFrame(const CameraFrame& a, const CameraFrame& b);
    [[nodiscard]] bool ValidCameraGeometry(const CameraFrame& frame);
    [[nodiscard]] bool CameraPoint(const CameraFrame& frame, double right, double up, double forward, ZEngine::Core::Maths::Vec3f& output);
    [[nodiscard]] bool CameraCorner(const CameraFrame& frame, uint32_t corner, double distance, ZEngine::Core::Maths::Vec3f& output);
    [[nodiscard]] bool ResolveCameraHandle(const CameraFrame& frame, Handle handle, CameraHandle& output);

    struct CameraBegin
    {
        CameraFrame                                     Expected   = {};
        ZEngine::Core::Maths::Geometry::ProjectionFrame Projection = {};
        ZEngine::Core::Maths::Geometry::Viewport        Bounds     = {};
        ZEngine::Core::Maths::Vec2f                     Pointer    = {};
        uint64_t                                        Key = 0, PointerSerial = 0;
        Handle                                          Constraint = Handle::None;
    };
    struct CameraInteraction
    {
        ZEngine::Core::Maths::Geometry::TranslationReference Mapping     = {};
        CameraFrame                                          Frame       = {};
        CameraHandle                                         HandleFrame = {};
        SnapSetting                                          Snap        = {};
        uint64_t                                             Key = 0, PointerSerial = 0;
        Handle                                               Constraint = Handle::None;
    };
    static_assert(sizeof(CameraInteraction) <= 1024);
    struct CameraSnapshot
    {
        CameraFrame Frame = {};
        float       Value = 0.f;
        uint64_t    Key = 0, PointerSerial = 0;
        Handle      Constraint = Handle::None;
    };
    static_assert(sizeof(CameraSnapshot) <= 384);
} // namespace Tetragrama::Gizmo
