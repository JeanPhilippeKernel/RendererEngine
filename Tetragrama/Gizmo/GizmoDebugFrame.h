#pragma once
#include <ZEngine/Rendering/Cameras/Camera.h>
#include <ZEngine/Rendering/Renderers/GizmoFrameData.h>

namespace Tetragrama::Gizmo
{
    /// WP2 opt-in renderer acceptance fixture, not an editable handle model.
    /// Uses the exact camera snapshot published with these primitives.
    [[nodiscard]] bool BuildDebugFrame(const ZEngine::Rendering::Cameras::CameraFrameData& camera, float logical_height, uint32_t width, uint32_t height, ZEngine::Rendering::Renderers::GizmoFrameData& output);
} // namespace Tetragrama::Gizmo
