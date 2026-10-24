#pragma once
#include <Tetragrama/Gizmo/GizmoTargetFrame.h>

namespace Tetragrama::Gizmo
{
    /// Value-only press intent. The viewport must validate its camera/bounds and
    /// input ownership; the session revalidates the complete expected target frame.
    struct TranslationBegin
    {
        TargetFrame                                     Expected      = {};
        ZEngine::Core::Maths::Geometry::ProjectionFrame Projection    = {};
        ZEngine::Core::Maths::Geometry::Viewport        Bounds        = {};
        ZEngine::Core::Maths::Vec2f                     Pointer       = {};
        uint64_t                                        Key           = 0;
        uint64_t                                        PointerSerial = 0;
        Handle                                          Constraint    = Handle::None;
    };

    /// Session-owned mapping only; authored before-values/history and identity
    /// validation remain in the shared TransformBatch or Grid transaction.
    struct TranslationInteraction
    {
        ZEngine::Core::Maths::Geometry::TranslationReference Mapping       = {};
        SnapSetting                                          Snap          = {};
        EntityTarget                                         Target        = {};
        SceneTargetIdentity                                  Scene         = {};
        ZEngine::Core::Maths::Mat4f                          StartWorld    = {};
        uint64_t                                             Key           = 0;
        uint64_t                                             PointerSerial = 0;
        Handle                                               Constraint    = Handle::None;
    };

    /// Main-thread presentation copy. Never retained ECS/component/camera memory.
    /// The pose is the synchronized, actually previewed transform; failed samples
    /// cannot advance the pill/guide beyond the shared transaction's last preview.
    struct TranslationSnapshot
    {
        EntityTarget                Target     = {};
        SceneTargetIdentity         Scene      = {};
        ZEngine::Core::Maths::Mat4f StartWorld = {}, World = {};
        ZEngine::Core::Maths::Vec3f StartPivot = {}, Pivot = {}, Components = {};
        uint64_t                    Key           = 0;
        uint64_t                    PointerSerial = 0;
        Handle                      Constraint    = Handle::None;
    };
} // namespace Tetragrama::Gizmo
