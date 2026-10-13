#pragma once
#include <ZEngine/Core/Maths/Matrix.h>
#include <cstdint>

namespace ZEngine::Rendering::Renderers
{
    /// @brief Editor-facing manipulation mode carried through the frame mailbox.
    enum class GizmoOperation : uint8_t
    {
        None,
        Translate,
        Rotate,
        Scale,
    };

    /// @brief Identifies both the visible primitive and its CPU interaction constraint.
    enum class GizmoHandle : uint8_t
    {
        None,
        AxisX,
        AxisY,
        AxisZ,
        PlaneXY,
        PlaneXZ,
        PlaneYZ,
        Uniform,
    };

    /// @brief Immutable native-gizmo presentation data for one submitted frame.
    /// @details Basis is an orthonormal world transform at Pivot. Scale is measured in
    /// world units and computed from the captured editor camera so handles keep a
    /// consistent apparent size. It deliberately owns no pointers into editor state.
    struct GizmoRenderState
    {
        Core::Maths::Mat4f Basis     = Core::Maths::Identity<Core::Maths::Mat4f>();
        float              Scale     = 1.f;
        GizmoOperation     Operation = GizmoOperation::None;
        GizmoHandle        Hovered   = GizmoHandle::None;
        GizmoHandle        Active    = GizmoHandle::None;
        bool               Visible   = false;
    };
} // namespace ZEngine::Rendering::Renderers
