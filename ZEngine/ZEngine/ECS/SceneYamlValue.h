#pragma once
#include <ZEngine/ECS/SceneDiagnostics.h>
#include <ZEngine/ECS/SceneValue.h>
#include <ZEngine/ECS/SceneYAML.h>

namespace ZEngine::ECS
{
    /// Missing/wrongly typed fields return an undefined value without allocating.
    [[nodiscard]] SceneYAML::Node FindSceneYAMLField(const SceneYAML::Node& node, std::string_view key);
    [[nodiscard]] bool            ReadSceneYAMLFloat(const SceneYAML::Node& node, float& out);
    [[nodiscard]] bool            ReadSceneYAMLUInt(const SceneYAML::Node& node, uint32_t& out);
    /// Owned YAML output; no borrowed SceneValue keys or strings survive this call.
    [[nodiscard]] bool            EncodeSceneValueYAML(const SceneValue& value, SceneYAML::Node* out, SceneDiagnostics* diagnostics);
    /// Decode exactly the keys/types described by shape. Strings borrow input's scalar storage.
    /// Callers validate semantics through their component codec before publishing the writer.
    [[nodiscard]] bool            DecodeSceneValueYAML(const SceneValue& shape, const SceneYAML::Node& input, SceneValueWriter* out, SceneDiagnostics* diagnostics);
} // namespace ZEngine::ECS
