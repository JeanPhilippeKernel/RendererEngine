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
    /// A scoped YAML working budget charges principal parser/event/tree state
    /// before libyaml initialization; this is not a full helper-stack bound.
    [[nodiscard]] bool ReadSceneYAML(std::string_view source, const SceneDocumentLimits& limits, SceneYAML::Node* out, SceneDiagnostics* diagnostics);
} // namespace ZEngine::ECS
