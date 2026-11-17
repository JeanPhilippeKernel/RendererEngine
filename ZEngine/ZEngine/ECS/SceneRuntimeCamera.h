#pragma once
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/SceneDiagnostics.h>
#include <ZEngine/Rendering/Cameras/Camera.h>
#include <uuid.h>

namespace ZEngine::ECS
{
    /// Capture a renderer-only launch view after candidate hierarchy sync. A
    /// non-nil UUID selects that camera; otherwise exactly one main is required.
    /// Failure preserves the output and never mutates the candidate scene.
    [[nodiscard]] bool CaptureSceneRuntimeCamera(const Scene& scene, const uuids::uuid& requested_camera, uint32_t viewport_width, uint32_t viewport_height, Rendering::Cameras::CameraFrameData* output, SceneDiagnostics* diagnostics);
} // namespace ZEngine::ECS
