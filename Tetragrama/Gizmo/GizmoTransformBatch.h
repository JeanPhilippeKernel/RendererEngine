#pragma once
#include <Tetragrama/EditorTarget.h>
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Maths/Geometry.h>
#include <ZEngine/Rendering/Renderers/GizmoFrameData.h>

namespace Tetragrama::Gizmo
{
    /// Only one operation owns a drag. Share its maximum-sized per-root capture
    /// storage instead of reserving separate translate/rotate/scale arrays.
    union TransformRoot
    {
        ZEngine::Core::Maths::Geometry::TranslationTarget Translation = {};
        ZEngine::Core::Maths::Geometry::RotationTarget    Rotation;
        ZEngine::Core::Maths::Geometry::ScaleTarget       Scale;
    };

    /// Selected descendants are not written, but their authored local pose
    /// remains part of the captured target set and must not change externally.
    struct TransformObserver
    {
        EntityTarget                             Target = {};
        ZEngine::Core::Maths::Geometry::LocalTRS Before = {};
    };

    /// Immutable visual capture, not another edit target set. Only a bounded
    /// priority prefix is retained; the complete authored batch is independent.
    struct RootFrame
    {
        EntityTarget                Target     = {};
        ZEngine::Core::Maths::Mat4f StartWorld = {};
        uuids::uuid                 MeshUUID   = {};
    };

    struct RootFrameView
    {
        static constexpr uint32_t                             Capacity   = ZEngine::Rendering::Renderers::GizmoFrameData::MaxMeshLineGroups;
        ZEngine::Core::Containers::ArrayView<const RootFrame> Frames     = {};
        uint64_t                                              Generation = 0;
        uint32_t                                              TotalRoots = 0;
    };
    static_assert(sizeof(RootFrame) <= 128);
} // namespace Tetragrama::Gizmo
