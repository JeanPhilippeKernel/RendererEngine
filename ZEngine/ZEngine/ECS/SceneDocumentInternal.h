#pragma once
#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Memory/WorkingMemoryBudget.h>
#include <ZEngine/ECS/SceneDocument.h>
#include <ZEngine/ECS/SceneValue.h>
#include <ZEngine/ECS/SceneYAML.h>

// Implementation-only document storage shared by source and binary codecs.
// None of these native layouts or node handles is a wire-format contract.
namespace ZEngine::ECS::SceneDocumentDetail
{
    inline constexpr size_t MaxComponentFields = 64;
    using SceneFieldScratch                    = Core::Containers::FixedArray<SceneValueField, MaxComponentFields>;
    // One sequential codec writer borrows this storage. This principal bound
    // is not a bound on arbitrary callback/compiler call-stack usage.
    [[nodiscard]] constexpr size_t SceneFieldScratchWorkingBytes() noexcept
    {
        return sizeof(SceneFieldScratch) + sizeof(SceneValueWriter) + sizeof(Core::Memory::WorkingMemoryCharge);
    }
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
    struct SceneTreeWalkScratch
    {
        struct Frame
        {
            SceneYAML::Node                   Container = {};
            SceneYAML::Range<true>::Iterator  Field     = {};
            SceneYAML::Range<false>::Iterator Item      = {};
        };
        Core::Containers::FixedArray<Frame, 32> Parents = {};
        SceneYAML::Node                         Current = {};
        size_t                                  Active  = 0;
    };
    [[nodiscard]] constexpr size_t SceneTreeWalkWorkingBytes() noexcept
    {
        return sizeof(SceneTreeWalkScratch) + sizeof(Core::Memory::WorkingMemoryCharge);
    }
    [[nodiscard]] bool   LimitsValid(const SceneDocumentLimits& limits);
    [[nodiscard]] size_t MigrationScratchWorkingBytes() noexcept;
    // Forward-link traversal is linear in node visits, with at most 32 parent
    // cursors and no traversal allocation. Optional map uniqueness uses the
    // storage's existing bounded sorting scratch, not a second tree walk.
    [[nodiscard]] bool   CountCapturedNodes(const SceneYAML::Node& node, const SceneDocumentLimits& limits, uint32_t& count, uint32_t depth = 1, SceneDiagnosticCode* failure = nullptr, bool unique_keys = false);
    [[nodiscard]] bool   ValidateGraph(Core::Containers::ArrayView<const EntityRecord> entities, const SceneDocumentSettingsView& settings, const SceneComponentSchemaRegistry& schemas, Core::Memory::ArenaAllocator& arena, SceneFieldScratch& fields, Core::Memory::IWorkingMemoryBudget* budget, SceneDiagnostics* diagnostics);
} // namespace ZEngine::ECS::SceneDocumentDetail

namespace ZEngine::ECS
{
    struct SceneDocument::Impl
    {
        SceneYAML::Storage                                            Tree       = {};
        SceneDocumentSettingsView                                     Settings   = {};
        Core::Containers::Array<SceneDocumentDetail::EntityRecord>    Entities   = {};
        Core::Containers::Array<SceneDocumentDetail::ComponentRecord> Components = {};
        SceneDocumentLimits                                           Limits     = {};
        bool                                                          ReadOnly   = false;
        [[nodiscard]] bool                                            Initialize(const SceneDocumentLimits& limits, void* tree_memory = nullptr, Core::Memory::IAllocationRoute* allocation_route = nullptr);
        [[nodiscard]] bool                                            CreateRoot(const SceneDocumentSettingsView& settings, SceneYAML::Node* root, SceneYAML::Node* entities, SceneDiagnostics* diagnostics);
        [[nodiscard]] bool                                            Decode(const SceneYAML::Node& root, const SceneComponentSchemaRegistry& schemas, SceneDiagnostics* diagnostics, bool canonical_payloads = false);
        // The caller charges and exclusively owns fields through the complete
        // synchronous decode, including graph validation. No retained borrow.
        [[nodiscard]] bool                                            Decode(const SceneYAML::Node& root, const SceneComponentSchemaRegistry& schemas, SceneDocumentDetail::SceneFieldScratch& fields, SceneDiagnostics* diagnostics, bool canonical_payloads = false);
    };
} // namespace ZEngine::ECS
