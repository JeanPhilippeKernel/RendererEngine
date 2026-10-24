#pragma once
#include <Tetragrama/Gizmo/GizmoFrameBuilder.h>

namespace Tetragrama::Gizmo
{
    /// Lower-priority component geometry. Invalid/over-budget light output is
    /// rolled back atomically, preserving room for the transform handles.
    void BuildLightGeometry(const FrameInput& input, const ZEngine::Core::Maths::Geometry::ProjectionFrame& camera, ZEngine::Rendering::Renderers::GizmoFrameData& output, HitFrame& hits);
} // namespace Tetragrama::Gizmo
