#pragma once
#include <Tetragrama/Gizmo/GizmoCamera.h>
#include <Tetragrama/Gizmo/GizmoHitTest.h>
#include <Tetragrama/Gizmo/GizmoLight.h>
#include <Tetragrama/Gizmo/GizmoState.h>
#include <ZEngine/Core/Maths/Geometry.h>
#include <ZEngine/Rendering/Cameras/Camera.h>
#include <ZEngine/Rendering/Renderers/GizmoFrameData.h>

namespace Tetragrama::Gizmo
{
    struct TransformDecorations
    {
        ZEngine::Core::Maths::Vec3f                StartPivot   = {};
        ZEngine::Core::Maths::Vec3f                CurrentPivot = {};
        ZEngine::Core::Maths::Geometry::BoxCorners Ghost = {}, Current = {};
        uuids::uuid                                MeshUUID = {};
        bool                                       HasBox = false, Visible = false;
        bool                                       GhostVisible = true;
    };

    struct RotationDecorations
    {
        ZEngine::Core::Maths::Vec3f Axis = {}, StartDirection = {};
        double                      Angle   = 0.0;
        bool                        Visible = false;
    };

    struct ScaleDecorations
    {
        float Ratio = 1.f, HandleLength = 0.f;
        bool  Visible = false;
    };

    struct PivotDecorations
    {
        ZEngine::Core::Maths::Vec3f Origin  = {};
        bool                        Visible = false;
    };

    /// Resolved, value-only inputs. The caller owns identity/revision validation;
    /// this builder neither resolves targets nor retains ECS/panel/camera pointers.
    /// Axes are already in the requested space; scale is removed by normalization.
    struct FrameInput
    {
        ZEngine::Rendering::Cameras::CameraFrameData                          Camera   = {};
        ZEngine::Core::Maths::Geometry::Viewport                              Viewport = {};
        ZEngine::Core::Maths::Vec3f                                           Pivot    = {};
        ZEngine::Core::Containers::FixedArray<ZEngine::Core::Maths::Vec3f, 3> Axes     = {
            {{1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}}
        };
        uint32_t                                                         TargetWidth            = 0;
        uint32_t                                                         TargetHeight           = 0;
        Operation                                                        Mode                   = Operation::Select;
        Handle                                                           Hovered                = Handle::None;
        Handle                                                           Active                 = Handle::None;
        bool                                                             Editable               = false;
        TransformDecorations                                             Drag                   = {};
        RotationDecorations                                              Rotation               = {};
        ZEngine::Core::Maths::Vec3f                                      AuthoredScale          = {1.f, 1.f, 1.f};
        ScaleDecorations                                                 Scale                  = {};
        PivotDecorations                                                 PivotMarker            = {};
        bool                                                             PivotEditable          = false;
        LightFrame                                                       Light                  = {};
        bool                                                             TransformsVisible      = true;
        CameraFrame                                                      Frustum                = {};
        /// Borrowed main-thread input consumed synchronously by BuildFrame;
        /// only copied primitives leave this boundary, never these views.
        ZEngine::Core::Containers::ArrayView<const TransformDecorations> Roots                  = {};
        bool                                                             RootDecorationsOmitted = false;
    };

    struct HitFrame
    {
        // At most four 64-segment rings, with axis back halves excluded. Reused
        // storage: reset only live counts, not the unused shape/world-span arrays.
        static constexpr uint32_t                                  MaxComponentShapes = 16;
        static constexpr uint32_t                                  MaxShapes          = 4 * Constants::RingSegments + MaxComponentShapes;
        ZEngine::Core::Containers::FixedArray<HitShape, MaxShapes> Shapes             = {};
        struct RingSpan
        {
            ZEngine::Core::Maths::Vec3f A = {}, B = {};
        };
        // Entries are initialized/read only for the matching live ring shapes.
        ZEngine::Core::Containers::FixedArray<RingSpan, MaxShapes> RingSpans = {};
        ZEngine::Core::Maths::Geometry::Viewport                   Viewport  = {};
        uint32_t                                                   Count     = 0;

        void                                                       Reset()
        {
            Count    = 0;
            Viewport = {};
        }
        [[nodiscard]] ZEngine::Core::Containers::ArrayView<const HitShape> View() const
        {
            return {Shapes.data(), Count <= MaxShapes ? Count : 0};
        }
        // Excludes off-viewport padding; GPU scissoring never moves a handle to
        // the edge. Session capture, rather than Pick(), owns an active drag.
        [[nodiscard]] HitResult Pick(const ZEngine::Core::Maths::Vec2f& point, Handle active = Handle::None) const;
        /// Explicit press-time work. Reconstruct a perspective-correct point on
        /// the closest visible/front-half ring chord for the tangent mapping.
        [[nodiscard]] bool      PickRingPoint(Handle handle, const ZEngine::Core::Maths::Vec2f& point, const ZEngine::Core::Maths::Geometry::ProjectionFrame& camera, ZEngine::Core::Maths::Vec3f& output) const;
    };
    static_assert(sizeof(HitFrame) <= 32 * 1024);

    enum class FrameBuildResult : uint8_t
    {
        Hidden,
        Built,
        Invalid,
    };

    /// Reuse both outputs. Only live counts reset; unused renderer storage is not
    /// zeroed. Paint and picking consume the same clipped world primitives. A
    /// failed build exposes neither partially drawn nor invisible hit handles.
    [[nodiscard]] FrameBuildResult BuildFrame(const FrameInput& input, ZEngine::Rendering::Renderers::GizmoFrameData& output, HitFrame& hits);
} // namespace Tetragrama::Gizmo
