#pragma once
#include <Tetragrama/EditorTarget.h>
#include <Tetragrama/Gizmo/GizmoFrameBuilder.h>
#include <ZEngine/ECS/SceneGridSettings.h>

namespace Tetragrama
{
    class EditorSession;
}

namespace Tetragrama::Gizmo
{
    /// A scene setting is not an entity. Keep its owner/epoch explicit rather
    /// than fabricating an EntityTarget or borrowing the settings owner.
    struct SceneTargetIdentity
    {
        uuids::uuid UUID                                         = {};
        uint64_t    Epoch                                        = 0;
        SceneTarget Kind                                         = SceneTarget::None;

        bool        operator==(const SceneTargetIdentity&) const = default;
    };
    static_assert(sizeof(SceneTargetIdentity) <= 32);

    /// Identity/revision tokens and copied transform only. No borrowed ECS memory.
    struct TargetFrame
    {
        EntityTarget                                                          Target             = {};
        SceneTargetIdentity                                                   Scene              = {};
        ZEngine::ECS::SceneGridBinary                                         Grid               = {};
        ZEngine::Core::Maths::Mat4f                                           World              = {};
        ZEngine::Core::Maths::Vec3f                                           Pivot              = {};
        ZEngine::Core::Maths::Vec3f                                           AuthoredScale      = {};
        ZEngine::Core::Containers::FixedArray<ZEngine::Core::Maths::Vec3f, 3> Axes               = {};
        uint64_t                                                              SelectionRevision  = 0;
        uint64_t                                                              StructuralRevision = 0;
        uint64_t                                                              HierarchyRevision  = 0;
        uint64_t                                                              WorldRevision      = 0;
        uint64_t                                                              SettingsRevision   = 0;
        bool                                                                  Valid              = false;
        bool                                                                  CustomPivot        = false;
    };
    static_assert(sizeof(TargetFrame) <= 512, "Keep value-only handle frames bounded");

    /// Reads already-synchronized hierarchy state. Call only outside ECS waves;
    /// does not synchronize/scan the scene or mutate authored components. Every
    /// selected entity must be editable; the explicit active target supplies the
    /// frame even when it is a descendant pruned from the authored root batch.
    /// Grid always uses its captured local U/N/V (quaternion X/Y/Z) frame,
    /// independently of the entity World/Local preference. No custom pivot.
    [[nodiscard]] bool ResolveTargetFrame(EditorSession& session, TargetFrame& output);
    [[nodiscard]] bool SameTargetFrame(const TargetFrame& a, const TargetFrame& b);
} // namespace Tetragrama::Gizmo
