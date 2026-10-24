#pragma once
#include <Tetragrama/Gizmo/GizmoTargetFrame.h>
#include <ZEngine/ECS/AuthoredComponentValues.h>

namespace Tetragrama::Gizmo
{
    struct PivotState
    {
        ZEngine::Core::Maths::Vec3f Position          = {};
        uint64_t                    SelectionRevision = 0, SceneEpoch = 0;
        bool                        Valid = false;
    };

    struct PivotBegin
    {
        TargetFrame                                     Expected   = {};
        ZEngine::Core::Maths::Geometry::ProjectionFrame Projection = {};
        ZEngine::Core::Maths::Geometry::Viewport        Bounds     = {};
        ZEngine::Core::Maths::Vec2f                     Pointer    = {};
        uint64_t                                        Key = 0, PointerSerial = 0;
    };

    /// Editor-only capture. No TransformBatch, authored writes or history bytes.
    struct PivotInteraction
    {
        ZEngine::Core::Maths::Geometry::TranslationReference Mapping  = {};
        TargetFrame                                          Frame    = {};
        ZEngine::ECS::LocalTransformValue                    Pose     = {};
        PivotState                                           Before   = {};
        ZEngine::Core::Maths::Vec3f                          Position = {};
        SnapSetting                                          Snap     = {};
        uint64_t                                             Key = 0, PointerSerial = 0, ParentWorldRevision = 0;
    };
    static_assert(sizeof(PivotInteraction) <= 1024);

    struct PivotSnapshot
    {
        EntityTarget                Target   = {};
        ZEngine::Core::Maths::Vec3f Position = {}, Origin = {};
        uint64_t                    Key = 0, PointerSerial = 0;
    };
    static_assert(sizeof(PivotSnapshot) <= 96);
} // namespace Tetragrama::Gizmo
