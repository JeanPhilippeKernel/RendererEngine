#pragma once
#include <Tetragrama/Gizmo/GizmoFrameBuilder.h>
#include <cstdint>

namespace ZEngine::Rendering::Renderers
{
    class GizmoPrimitiveBuilder;
}

namespace Tetragrama::Gizmo
{
    /// Shared world-space filled camera icon. It deliberately has no hit targets,
    /// so passive and selected cameras retain the same visual identity.
    [[nodiscard]] bool BuildCameraIcon(const CameraFrame& frame, ZEngine::Rendering::Renderers::GizmoPrimitiveBuilder& builder, uint32_t fill_color);
    void               BuildCameraGeometry(const FrameInput& input, const ZEngine::Core::Maths::Geometry::ProjectionFrame& camera, ZEngine::Rendering::Renderers::GizmoFrameData& output, HitFrame& hits);
} // namespace Tetragrama::Gizmo
