#pragma once
#include <ZEngine/ECS/SceneComponentSchemaRegistry.h>

namespace ZEngine::ECS
{
    /// @brief Explicit engine-startup registration of built-in authored component codecs.
    /// Shared semantic values/codecs serve persistence and editor operations; no static registration.
    [[nodiscard]] bool RegisterBuiltInSceneComponentSchemas(SceneComponentSchemaRegistry& registry, SceneDiagnostics* diagnostics = nullptr);
    [[nodiscard]] bool RegisterSceneIdentityComponentSchemas(SceneComponentSchemaRegistry& registry, SceneDiagnostics* diagnostics = nullptr);
} // namespace ZEngine::ECS
