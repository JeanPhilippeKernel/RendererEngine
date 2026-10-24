#pragma once
#include <Tetragrama/Gizmo/GizmoTargetFrame.h>

namespace Tetragrama::Gizmo
{
    struct RotationBegin
    {
        TargetFrame                                     Expected      = {};
        ZEngine::Core::Maths::Geometry::ProjectionFrame Projection    = {};
        ZEngine::Core::Maths::Geometry::Viewport        Bounds        = {};
        ZEngine::Core::Maths::Vec2f                     Pointer       = {};
        ZEngine::Core::Maths::Vec3f                     RingPoint     = {};
        uint64_t                                        Key           = 0;
        uint64_t                                        PointerSerial = 0;
        Handle                                          Constraint    = Handle::None;
    };

    /// Shared TransformBatch/Grid owns before/after values and authored writes.
    /// Only bounded value-only pointer mapping/presentation state lives here.
    struct RotationInteraction
    {
        ZEngine::Core::Maths::Geometry::RotationReference Mapping       = {};
        ZEngine::Core::Maths::Geometry::RotationProgress  Progress      = {};
        ZEngine::Core::Maths::Mat4f                       StartWorld    = {};
        SnapSetting                                       Snap          = {};
        EntityTarget                                      Target        = {};
        SceneTargetIdentity                               Scene         = {};
        double                                            Angle         = 0.0;
        uint64_t                                          Key           = 0;
        uint64_t                                          PointerSerial = 0;
        Handle                                            Constraint    = Handle::None;
    };

    struct RotationSnapshot
    {
        EntityTarget                Target     = {};
        SceneTargetIdentity         Scene      = {};
        ZEngine::Core::Maths::Mat4f StartWorld = {}, World = {};
        ZEngine::Core::Maths::Vec3f Pivot = {}, Axis = {}, StartDirection = {};
        double                      Angle         = 0.0;
        uint64_t                    Key           = 0;
        uint64_t                    PointerSerial = 0;
        Handle                      Constraint    = Handle::None;
    };
} // namespace Tetragrama::Gizmo
