#pragma once
#include <ZEngine/ECS/SceneDocument.h>
#include <ZEngine/ECS/SceneYAML.h>
#include <string_view>

namespace ZEngine::ECS
{
    /// Checked event parsing; builds no aliases and publishes only one complete,
    /// bounded tree. Source errors are returned through diagnostics, not unwinding.
    /// `out` must belong to initialized Storage (e.g. Storage::Destination()).
    /// Failure preserves its node and rewinds newly allocated tree records/text.
    /// The owner must outlive the borrowed result and all of its child views.
    [[nodiscard]] bool ReadSceneYAML(std::string_view source, const SceneDocumentLimits& limits, SceneYAML::Node* out, SceneDiagnostics* diagnostics);
} // namespace ZEngine::ECS
