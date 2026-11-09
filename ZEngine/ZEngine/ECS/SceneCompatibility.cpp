#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Serialization/YAMLAllocation.h>
#include <ZEngine/ECS/SceneCompatibility.h>
#include <ZEngine/ECS/SceneDocumentInternal.h>
#include <ZEngine/ECS/SceneSchemaKey.h>
#include <ZEngine/ECS/SceneYamlValue.h>
#include <algorithm>
#include <cstdio>
#include <optional>

namespace ZEngine::ECS
{
    namespace
    {
        struct MigrationScratchSlot
        {
            std::optional<SceneYAML::Storage> Tree  = {};
            SceneYAML::Storage::Checkpoint    Empty = {};
        };
        struct MigrationScratchState
        {
            Core::Containers::FixedArray<MigrationScratchSlot, 2> Slots         = {};
            SceneYAML::Node                                       Current       = {};
            SceneYAML::Node                                       Next          = {};
            uint32_t                                              InputIndex    = 0;
            uint32_t                                              Nodes         = 0;
            uint32_t                                              ActualVersion = 0;
            SceneDiagnosticCode                                   Failure       = SceneDiagnosticCode::InvalidData;
        };
        bool Reject(SceneDiagnostics* diagnostics, cstring message, SceneDiagnosticCode code = SceneDiagnosticCode::InvalidData)
        {
            SceneDiagnosticError(diagnostics, message, code);
            return false;
        }
        bool RejectStep(SceneDiagnostics* diagnostics, cstring key, uint32_t from, uint32_t to, cstring reason, SceneDiagnosticCode code = SceneDiagnosticCode::InvalidData)
        {
            if (diagnostics)
            {
                if (auto* entry = diagnostics->Emplace(SceneDiagnosticSeverity::Error, code, key && key[0] ? key : "scene"))
                {
                    // Keep a key borrowed from this reused diagnostic slot safe:
                    // move it before writing the prefix, then format the suffix
                    // after it. No overlapping printf input or temporary buffer.
                    constexpr cstring prefix        = "Migration ";
                    constexpr size_t  prefix_length = sizeof("Migration ") - 1;
                    const size_t      key_length    = std::min<size_t>(127, Helpers::secure_strlen(entry->Message));
                    Helpers::secure_memmove(entry->Message + prefix_length, sizeof(entry->Message) - prefix_length, entry->Message, key_length);
                    Helpers::secure_memcpy(entry->Message, sizeof(entry->Message), prefix, prefix_length);
                    const size_t offset = prefix_length + key_length;
                    std::snprintf(entry->Message + offset, sizeof(entry->Message) - offset, " %u -> %u: %s", from, to, reason);
                }
            }
            return false;
        }
        bool DocumentVersion(const SceneYAML::Node& root, uint32_t& version)
        {
            return root.IsMap() && root.size() == 1 && root["scene"].IsMap() && ReadSceneYAMLUInt(root["scene"]["schema_version"], version) && version;
        }
        bool InitializeScratch(SceneYAML::Storage& storage, const SceneDocumentLimits& limits, Core::Memory::IAllocationRoute* allocation_route)
        {
            return storage.Initialize(limits.MaxDecodedBytes, limits.MaxNodes, limits.MaxCollectionItems, limits.MaxScalarBytes, allocation_route);
        }
    } // namespace

    size_t SceneDocumentDetail::MigrationScratchWorkingBytes() noexcept
    {
        return sizeof(MigrationScratchState) + sizeof(SceneYAML::Node) + sizeof(Core::Memory::WorkingMemoryCharge);
    }

    bool SceneMigrationRegistry::Initialize(Core::Memory::ArenaAllocator* arena, uint32_t capacity)
    {
        if (IsInitialized() || !arena || !arena->m_memory || !capacity || capacity > MaxMigrations)
        {
            return false;
        }
        m_entries.m_allocator = arena;
        if (!m_entries.try_reserve(capacity))
        {
            return false;
        }
        m_capacity = capacity;
        return true;
    }
    bool SceneMigrationRegistry::Freeze()
    {
        if (m_frozen)
        {
            return true;
        }
        if (!IsInitialized())
        {
            return false;
        }
        m_frozen = true;
        return true;
    }

    bool SceneMigrationRegistry::IsInitialized() const
    {
        return m_capacity != 0;
    }
    uint32_t SceneMigrationRegistry::Count() const
    {
        return static_cast<uint32_t>(m_entries.size());
    }
    void SceneMigrationRegistry::ForEachContext(void (*visitor)(void*, const void*), void* state) const noexcept
    {
        if (visitor)
        {
            for (const auto& entry : m_entries)
            {
                visitor(state, entry.Context);
            }
        }
    }
    const SceneMigrationRegistry::Entry* SceneMigrationRegistry::Find(cstring key, uint32_t from) const
    {
        for (const auto& entry : m_entries)
        {
            if (entry.From == from && Helpers::secure_strcmp(entry.Key, key) == 0)
            {
                return &entry;
            }
        }
        return nullptr;
    }
    bool SceneMigrationRegistry::RegisterDocumentMigration(uint32_t from, uint32_t to, void* context, SceneMigrationFn migrate, SceneDiagnostics* diagnostics)
    {
        return Register("", from, to, context, migrate, diagnostics);
    }
    bool SceneMigrationRegistry::RegisterComponentMigration(cstring key, uint32_t from, uint32_t to, void* context, SceneMigrationFn migrate, SceneDiagnostics* diagnostics)
    {
        if (!key || !IsValidSceneSchemaKey(key) || std::string_view(key) == "UUIDComponent")
        {
            return Reject(diagnostics, "Migration requires a portable component schema key; UUID identity is document-owned.");
        }
        return Register(key, from, to, context, migrate, diagnostics);
    }
    bool SceneMigrationRegistry::Register(cstring key, uint32_t from, uint32_t to, void* context, SceneMigrationFn migrate, SceneDiagnostics* diagnostics)
    {
        if (m_frozen || !IsInitialized() || !from || to <= from || !migrate || m_entries.size() == m_capacity || Find(key, from))
        {
            return RejectStep(diagnostics, key, from, to, "invalid, duplicate/branching or full registration");
        }
        Entry entry = {.From = from, .To = to, .Context = context, .Migrate = migrate};
        if (Helpers::secure_strncpy(entry.Key, sizeof(entry.Key), key, Helpers::secure_strlen(key)) != Helpers::MEMORY_OP_SUCCESS || !m_entries.try_push(entry))
        {
            return RejectStep(diagnostics, key, from, to, "bounded registry allocation failed");
        }
        return true;
    }
    bool SceneMigrationRegistry::Apply(cstring key, uint32_t from, uint32_t to, const SceneYAML::Node& source, SceneYAML::Node* out, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits, Core::Memory::IAllocationRoute* allocation_route) const
    {
        // Prove the entire unique increasing route exists before any callback.
        uint32_t version = from, steps = 0;
        while (version < to)
        {
            const auto* entry = Find(key, version);
            if (!entry || entry->To > to || ++steps > MaxSteps)
            {
                return RejectStep(diagnostics, key, from, to, "no complete bounded migration route", SceneDiagnosticCode::UnsupportedVersion);
            }
            version = entry->To;
        }
        if (version != to || !out || !out->Owner())
        {
            return RejectStep(diagnostics, key, from, to, "invalid migration destination/version");
        }
        Core::Memory::WorkingMemoryCharge fixed = {};
        if (!fixed.TryAcquire(allocation_route ? allocation_route->WorkingMemoryBudget() : Core::Serialization::YAMLWorkingMemoryBudget(), SceneDocumentDetail::MigrationScratchWorkingBytes()))
        {
            return RejectStep(diagnostics, key, from, to, "migration snapshot state exceeds the working-memory budget", SceneDiagnosticCode::LimitExceeded);
        }
        MigrationScratchState scratch = {};
        auto*                 input   = &scratch.Slots[scratch.InputIndex].Tree.emplace();
        if (!InitializeScratch(*input, limits, allocation_route))
        {
            return RejectStep(diagnostics, key, from, to, "cannot allocate bounded input snapshot", SceneDiagnosticCode::AllocationFailed);
        }
        scratch.Slots[scratch.InputIndex].Empty = input->Mark();
        scratch.Current                         = input->Clone(source, limits.MaxDepth);
        if (!scratch.Current.IsDefined())
        {
            return RejectStep(diagnostics, key, from, to, "input exceeds value-tree limits", input->Failure());
        }
        input->Freeze();
        version = from;
        while (version < to)
        {
            const auto* entry        = Find(key, version);
            const auto  output_index = scratch.InputIndex ^ 1u;
            auto&       slot         = scratch.Slots[output_index];
            if (!slot.Tree)
            {
                auto& tree = slot.Tree.emplace();
                if (!InitializeScratch(tree, limits, allocation_route))
                {
                    return RejectStep(diagnostics, key, version, entry->To, "cannot allocate bounded output", SceneDiagnosticCode::AllocationFailed);
                }
                slot.Empty = tree.Mark();
            }
            auto* output = &*slot.Tree;
            // Only this no-longer-referenced snapshot is rewound. Reuse the same
            // two owners' stable blocks for arbitrarily many bounded steps.
            // Growth depends on peak tree size, not the migration step count.
            output->Rollback(slot.Empty);
            scratch.Next          = output->Destination();
            scratch.Nodes         = 0;
            scratch.ActualVersion = 0;
            scratch.Failure       = SceneDiagnosticCode::InvalidData;
            if (!entry->Migrate(entry->Context, scratch.Current, &scratch.Next, diagnostics) || !input->Good() || !output->Good() || scratch.Next.Owner() != output || !SceneDocumentDetail::CountCapturedNodes(scratch.Next, limits, scratch.Nodes, 1, &scratch.Failure, true) || (key[0] ? !scratch.Next.IsMap() : (!DocumentVersion(scratch.Next, scratch.ActualVersion) || scratch.ActualVersion != entry->To)))
            {
                if (!input->Good())
                {
                    scratch.Failure = input->Failure();
                }
                else if (!output->Good())
                {
                    scratch.Failure = output->Failure();
                }
                return RejectStep(diagnostics, key, version, entry->To, "callback failed, mutated input or returned invalid/budget-exceeding output", scratch.Failure);
            }
            // At most two scratch trees survive a step, each bounded by the
            // document limits. No chain-length-proportional retained snapshots.
            output->Freeze();
            scratch.Current    = scratch.Next;
            scratch.InputIndex = output_index;
            input              = output;
            version            = entry->To;
        }
        const auto result = out->Owner()->Clone(scratch.Current, limits.MaxDepth);
        if (!result.IsDefined())
        {
            return RejectStep(diagnostics, key, from, to, "owned result exceeds document storage budget or contains duplicate keys", out->Owner()->Failure());
        }
        *out = result;
        return true;
    }

    bool SceneCompatibilityService::PrepareForLoad(SceneYAML::Node* root, const SceneComponentSchemaRegistry& schemas, const SceneMigrationRegistry* migrations, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits, Core::Memory::IAllocationRoute* allocation_route)
    {
        if (!root || !root->Owner() || !root->Owner()->Good() || !schemas.IsInitialized() || !SceneDocumentDetail::LimitsValid(limits) || (migrations && !migrations->IsInitialized()))
        {
            return Reject(diagnostics, "Scene compatibility requires an unpublished owned tree, initialized registries and valid limits.");
        }
        uint32_t            version = 0, nodes = 0;
        SceneDiagnosticCode failure = SceneDiagnosticCode::InvalidData;
        if (!SceneDocumentDetail::CountCapturedNodes(*root, limits, nodes, 1, &failure))
        {
            return Reject(diagnostics, "Scene source exceeds its value-tree limits.", failure);
        }
        if (!DocumentVersion(*root, version))
        {
            return Reject(diagnostics, "Scene source has no valid document version.");
        }
        if (version > SceneDocument::SchemaVersion)
        {
            return Reject(diagnostics, "Scene document version is unsupported.", SceneDiagnosticCode::UnsupportedVersion);
        }
        if (version < SceneDocument::SchemaVersion)
        {
            auto upgraded = root->Owner()->Destination();
            if (!migrations)
            {
                return RejectStep(diagnostics, "", version, SceneDocument::SchemaVersion, "no migration registry configured", SceneDiagnosticCode::UnsupportedVersion);
            }
            if (!migrations->Apply("", version, SceneDocument::SchemaVersion, *root, &upgraded, diagnostics, limits, allocation_route))
            {
                return false;
            }
            *root = upgraded;
        }
        auto&      storage  = *root->Owner();
        const auto entities = (*root)["scene"]["entities"];
        if (!entities.IsSequence())
        {
            return Reject(diagnostics, "Scene compatibility requires an entity sequence.");
        }
        if (entities.size() > limits.MaxEntities || entities.size() > limits.MaxCollectionItems)
        {
            return Reject(diagnostics, "Scene entity sequence exceeds its limits.", SceneDiagnosticCode::LimitExceeded);
        }
        for (const auto& entity : entities.Sequence())
        {
            const auto components = entity["components"];
            if (!components.IsMap())
            {
                return Reject(diagnostics, "Scene compatibility requires a component map.");
            }
            if (components.size() > limits.MaxComponentsPerEntity)
            {
                return Reject(diagnostics, "Scene component map exceeds its limits.", SceneDiagnosticCode::LimitExceeded);
            }
            for (const auto& component : components.Mapping())
            {
                const auto envelope          = component.Value;
                uint32_t   component_version = 0;
                if (!envelope.IsMap() || envelope.size() != 2 || !ReadSceneYAMLUInt(envelope["schema_version"], component_version) || !component_version || !envelope["fields"].IsMap())
                {
                    return Reject(diagnostics, "Scene compatibility found a malformed component envelope.");
                }
                const auto* schema = schemas.FindByKey(component.Key.data());
                if (!schema || component_version >= schema->Version)
                {
                    continue; // unknown/newer data remains intact, never dropped
                }
                if (!migrations)
                {
                    return RejectStep(diagnostics, schema->Key, component_version, schema->Version, "no migration registry configured", SceneDiagnosticCode::UnsupportedVersion);
                }
                auto upgraded = storage.Destination();
                if (!migrations->Apply(schema->Key, component_version, schema->Version, envelope["fields"], &upgraded, diagnostics, limits, allocation_route))
                {
                    return false;
                }
                if (!storage.Set(envelope, "fields", upgraded) || !storage.Set(envelope, "schema_version", storage.UInt(schema->Version)))
                {
                    return Reject(diagnostics, "Migrated component value storage failed.", storage.Failure());
                }
            }
        }
        nodes = 0;
        return SceneDocumentDetail::CountCapturedNodes(*root, limits, nodes, 1, &failure) || Reject(diagnostics, "Migrated scene exceeds document node, depth or scalar limits.", failure);
    }
    bool SceneCompatibilityService::PrepareForSave(const SceneDocument& document, SceneCompatibilityUse use, SceneDiagnostics* diagnostics)
    {
        if (use != SceneCompatibilityUse::EditorSource && use != SceneCompatibilityUse::CookedRuntime)
        {
            return Reject(diagnostics, "Unknown scene compatibility use.");
        }
        if (!document.Settings() || document.ReadOnly())
        {
            return Reject(diagnostics, use == SceneCompatibilityUse::CookedRuntime ? "Cooked output rejects unknown or unsupported scene component data; migrate source before cooking." : "Read-only/empty scene cannot be saved; unsupported data would be lost.", SceneDiagnosticCode::UnsupportedVersion);
        }
        return true;
    }
} // namespace ZEngine::ECS
