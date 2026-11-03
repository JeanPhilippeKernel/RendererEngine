#pragma once
#include <ZEngine/ECS/SceneDocument.h>
#include <ZEngine/ECS/SceneYAML.h>

// Implementation-only document storage shared by source and binary codecs.
// None of these native layouts or node handles is a wire-format contract.
namespace ZEngine::ECS::SceneDocumentDetail
{
    inline constexpr size_t MaxComponentFields = 64;
    struct ComponentRecord
    {
        std::string_view Key       = {};
        uint32_t         Version   = 0;
        SceneYAML::Node  Payload   = {};
        bool             Supported = false;
    };
    struct EntityRecord
    {
        uuids::uuid                                  UUID       = {};
        Core::Containers::ArrayView<ComponentRecord> Components = {};
    };
    [[nodiscard]] bool LimitsValid(const SceneDocumentLimits& limits);
    [[nodiscard]] bool CountCapturedNodes(const SceneYAML::Node& node, const SceneDocumentLimits& limits, uint32_t& count, uint32_t depth = 1);
    [[nodiscard]] bool ValidateGraph(Core::Containers::ArrayView<const EntityRecord> entities, const SceneDocumentSettings& settings, const SceneComponentSchemaRegistry& schemas, Core::Memory::ArenaAllocator& arena, SceneDiagnostics* diagnostics);
} // namespace ZEngine::ECS::SceneDocumentDetail

namespace ZEngine::ECS
{
    struct SceneDocument::Impl
    {
        SceneYAML::Storage                                            Tree       = {};
        SceneDocumentSettings                                         Settings   = {};
        Core::Containers::Array<SceneDocumentDetail::EntityRecord>    Entities   = {};
        Core::Containers::Array<SceneDocumentDetail::ComponentRecord> Components = {};
        SceneDocumentLimits                                           Limits     = {};
        bool                                                          ReadOnly   = false;
        [[nodiscard]] bool                                            Initialize(const SceneDocumentLimits& limits);
        [[nodiscard]] bool                                            CreateRoot(const SceneDocumentSettings& settings, SceneYAML::Node* root, SceneYAML::Node* entities, SceneDiagnostics* diagnostics);
        [[nodiscard]] bool                                            Decode(const SceneYAML::Node& root, const SceneComponentSchemaRegistry& schemas, SceneDiagnostics* diagnostics, bool canonical_payloads = false);
    };
} // namespace ZEngine::ECS
