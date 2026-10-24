#pragma once
#include <Tetragrama/Gizmo/GizmoTargetFrame.h>

namespace Tetragrama::Gizmo
{
    struct ScaleBegin
    {
        TargetFrame                                     Expected      = {};
        ZEngine::Core::Maths::Geometry::ProjectionFrame Projection    = {};
        ZEngine::Core::Maths::Geometry::Viewport        Bounds        = {};
        ZEngine::Core::Maths::Vec2f                     Pointer       = {};
        uint64_t                                        Key           = 0;
        uint64_t                                        PointerSerial = 0;
        Handle                                          Constraint    = Handle::None;
    };

    /// Value-only mapping/presentation state. The shared TransformBatch still
    /// owns all authored before/after values, cancellation and history.
    struct ScaleInteraction
    {
        ZEngine::Core::Maths::Geometry::ScaleReference                        Mapping       = {};
        ZEngine::Core::Maths::Geometry::ScaleFrame                            Frame         = {};
        ZEngine::Core::Maths::Mat4f                                           StartWorld    = {};
        ZEngine::Core::Containers::FixedArray<ZEngine::Core::Maths::Vec3f, 3> Axes          = {};
        ZEngine::Core::Maths::Vec3f                                           AuthoredScale = {};
        SnapSetting                                                           Snap          = {};
        EntityTarget                                                          Target        = {};
        float                                                                 Ratio         = 1.f;
        float                                                                 HandleLength  = 0.f;
        uint64_t                                                              Key           = 0;
        uint64_t                                                              PointerSerial = 0;
        Handle                                                                Constraint    = Handle::None;
        bool                                                                  Group         = false;
    };

    struct ScaleSnapshot
    {
        EntityTarget                                                          Target     = {};
        ZEngine::Core::Maths::Mat4f                                           StartWorld = {}, World = {};
        ZEngine::Core::Maths::Vec3f                                           Pivot         = {};
        ZEngine::Core::Containers::FixedArray<ZEngine::Core::Maths::Vec3f, 3> Axes          = {};
        ZEngine::Core::Maths::Vec3f                                           AuthoredScale = {};
        float                                                                 Ratio         = 1.f;
        float                                                                 HandleLength  = 0.f;
        uint64_t                                                              Key           = 0;
        uint64_t                                                              PointerSerial = 0;
        Handle                                                                Constraint    = Handle::None;
    };
    static_assert(sizeof(ScaleSnapshot) <= 320, "Scale presentation must remain bounded value-only storage");
} // namespace Tetragrama::Gizmo
