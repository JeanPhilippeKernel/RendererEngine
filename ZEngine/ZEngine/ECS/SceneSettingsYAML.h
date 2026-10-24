#pragma once
#include <ZEngine/ECS/SceneDiagnostics.h>
#include <ZEngine/ECS/SceneGridSettings.h>
#include <ZEngine/ECS/SceneYAML.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>

namespace ZEngine::ECS
{
    [[nodiscard]] bool EncodeSceneGridYAML(const SceneGridSettings& value, SceneYAML::Node* out, SceneDiagnostics* diagnostics);
    [[nodiscard]] bool DecodeSceneGridYAML(const SceneYAML::Node& input, SceneGridSettings* out, SceneDiagnostics* diagnostics);
    [[nodiscard]] bool EncodeSceneSkyYAML(const Rendering::Scenes::SkyConfig& value, SceneYAML::Node* out, SceneDiagnostics* diagnostics);
    [[nodiscard]] bool DecodeSceneSkyYAML(const SceneYAML::Node& input, Rendering::Scenes::SkyConfig* out, SceneDiagnostics* diagnostics);
} // namespace ZEngine::ECS
