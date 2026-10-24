#pragma once
#include <Tetragrama/Gizmo/GizmoFrameBuilder.h>

namespace Tetragrama::Gizmo
{
    void BuildCameraGeometry(const FrameInput& input, const ZEngine::Core::Maths::Geometry::ProjectionFrame& camera, ZEngine::Rendering::Renderers::GizmoFrameData& output, HitFrame& hits);
}
