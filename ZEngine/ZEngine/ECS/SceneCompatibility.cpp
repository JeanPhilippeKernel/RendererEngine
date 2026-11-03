#include <ZEngine/ECS/SceneCompatibility.h>
#include <ZEngine/ECS/SceneDocumentInternal.h>
#include <ZEngine/ECS/SceneSchemaKey.h>
#include <ZEngine/ECS/SceneYamlValue.h>
#include <cstdio>
#include <new>

namespace ZEngine::ECS
{
    namespace
    {
        bool Reject(SceneDiagnostics* diagnostics, cstring message)
        {
            SceneDiagnosticError(diagnostics, message);
            return false;
        }
        bool RejectStep(SceneDiagnostics* diagnostics, cstring key, uint32_t from, uint32_t to, cstring reason)
        {
            char message[SceneDiagnostics::MAX_MESSAGE_LEN] = {};
            std::snprintf(message, sizeof(message), "Migration %.127s %u -> %u: %s", key && key[0] ? key : "scene", from, to, reason);
            return Reject(diagnostics, message);
        }
        bool DocumentVersion(const SceneYAML::Node& root, uint32_t& version)
        {
            return root.IsMap() && root.size() == 1 && root["scene"].IsMap() && ReadSceneYAMLUInt(root["scene"]["schema_version"], version) && version;
        }
        bool UniqueTreeKeys(const SceneYAML::Node& node)
        {
            if (node.IsMap())
            {
                if (!node.Owner()->UniqueKeys(node))
                    return false;
                for (const auto& entry : node.Mapping())
                    if (!UniqueTreeKeys(entry.Value))
                        return false;
            }
            else if (node.IsSequence())
                for (const auto& item : node.Sequence())
                    if (!UniqueTreeKeys(item))
                        return false;
            return true;
        }
        Helpers::Scope<SceneYAML::Storage> Scratch(const SceneDocumentLimits& limits)
        {
            auto storage = Helpers::Scope<SceneYAML::Storage>(new (std::nothrow) SceneYAML::Storage{});
            if (!storage || !storage->Initialize(limits.MaxDecodedBytes, limits.MaxNodes, limits.MaxCollectionItems, limits.MaxScalarBytes))
                return {};
            return storage;
        }
    } // namespace

    bool SceneMigrationRegistry::Initialize(Core::Memory::ArenaAllocator* arena, uint32_t capacity)
    {
        if (IsInitialized() || !arena || !arena->m_memory || !capacity || capacity > MaxMigrations)
            return false;
        m_entries.m_allocator = arena;
        if (!m_entries.try_reserve(capacity))
            return false;
        m_capacity = capacity;
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
    const SceneMigrationRegistry::Entry* SceneMigrationRegistry::Find(cstring key, uint32_t from) const
    {
        for (const auto& entry : m_entries)
            if (entry.From == from && Helpers::secure_strcmp(entry.Key, key) == 0)
                return &entry;
        return nullptr;
    }
    bool SceneMigrationRegistry::RegisterDocumentMigration(uint32_t from, uint32_t to, void* context, SceneMigrationFn migrate, SceneDiagnostics* diagnostics)
    {
        return Register("", from, to, context, migrate, diagnostics);
    }
    bool SceneMigrationRegistry::RegisterComponentMigration(cstring key, uint32_t from, uint32_t to, void* context, SceneMigrationFn migrate, SceneDiagnostics* diagnostics)
    {
        if (!key || !IsValidSceneSchemaKey(key) || std::string_view(key) == "UUIDComponent")
            return Reject(diagnostics, "Migration requires a portable component schema key; UUID identity is document-owned.");
        return Register(key, from, to, context, migrate, diagnostics);
    }
    bool SceneMigrationRegistry::Register(cstring key, uint32_t from, uint32_t to, void* context, SceneMigrationFn migrate, SceneDiagnostics* diagnostics)
    {
        if (!IsInitialized() || !from || to <= from || !migrate || m_entries.size() == m_capacity || Find(key, from))
            return RejectStep(diagnostics, key, from, to, "invalid, duplicate/branching or full registration");
        Entry entry = {.From = from, .To = to, .Context = context, .Migrate = migrate};
        if (Helpers::secure_strncpy(entry.Key, sizeof(entry.Key), key, Helpers::secure_strlen(key)) != Helpers::MEMORY_OP_SUCCESS || !m_entries.try_push(entry))
            return RejectStep(diagnostics, key, from, to, "bounded registry allocation failed");
        return true;
    }
    bool SceneMigrationRegistry::Apply(cstring key, uint32_t from, uint32_t to, const SceneYAML::Node& source, SceneYAML::Node* out, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits) const
    {
        // Prove the entire unique increasing route exists before any callback.
        uint32_t version = from, steps = 0;
        while (version < to)
        {
            const auto* entry = Find(key, version);
            if (!entry || entry->To > to || ++steps > MaxSteps)
                return RejectStep(diagnostics, key, from, to, "no complete bounded migration route");
            version = entry->To;
        }
        if (version != to || !out || !out->Owner())
            return RejectStep(diagnostics, key, from, to, "invalid migration destination/version");
        auto input = Scratch(limits);
        if (!input)
            return RejectStep(diagnostics, key, from, to, "cannot allocate bounded input snapshot");
        auto current = input->Clone(source, limits.MaxDepth);
        if (!current.IsDefined())
            return RejectStep(diagnostics, key, from, to, "input exceeds value-tree limits");
        input->Freeze();
        version = from;
        while (version < to)
        {
            const auto* entry  = Find(key, version);
            auto        output = Scratch(limits);
            if (!output)
                return RejectStep(diagnostics, key, version, entry->To, "cannot allocate bounded output");
            auto     next  = output->Destination();
            uint32_t nodes = 0, actual_version = 0;
            if (!entry->Migrate(entry->Context, current, &next, diagnostics) || !input->Good() || !output->Good() || next.Owner() != output.get() || !SceneDocumentDetail::CountCapturedNodes(next, limits, nodes) || !UniqueTreeKeys(next) || (key[0] ? !next.IsMap() : (!DocumentVersion(next, actual_version) || actual_version != entry->To)))
                return RejectStep(diagnostics, key, version, entry->To, "callback failed, mutated input or returned invalid/budget-exceeding output");
            // At most two scratch trees survive a step, each bounded by the
            // document limits. No chain-length-proportional retained snapshots.
            output->Freeze();
            current = next;
            input   = std::move(output);
            version = entry->To;
        }
        const auto result = out->Owner()->Clone(current, limits.MaxDepth);
        if (!result.IsDefined())
            return RejectStep(diagnostics, key, from, to, "owned result exceeds document storage budget or contains duplicate keys");
        *out = result;
        return true;
    }

    bool SceneCompatibilityService::PrepareForLoad(SceneYAML::Node* root, const SceneComponentSchemaRegistry& schemas, const SceneMigrationRegistry* migrations, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits)
    {
        if (!root || !root->Owner() || !root->Owner()->Good() || !schemas.IsInitialized() || !SceneDocumentDetail::LimitsValid(limits) || (migrations && !migrations->IsInitialized()))
            return Reject(diagnostics, "Scene compatibility requires an unpublished owned tree, initialized registries and valid limits.");
        uint32_t version = 0, nodes = 0;
        if (!SceneDocumentDetail::CountCapturedNodes(*root, limits, nodes) || !DocumentVersion(*root, version) || version > SceneDocument::SchemaVersion)
            return Reject(diagnostics, "Unsupported scene document version or invalid source tree limits.");
        if (version < SceneDocument::SchemaVersion)
        {
            auto upgraded = root->Owner()->Destination();
            if (!migrations)
                return RejectStep(diagnostics, "", version, SceneDocument::SchemaVersion, "no migration registry configured");
            if (!migrations->Apply("", version, SceneDocument::SchemaVersion, *root, &upgraded, diagnostics, limits))
                return false;
            *root = upgraded;
        }
        auto&      storage  = *root->Owner();
        const auto entities = (*root)["scene"]["entities"];
        if (!entities.IsSequence() || entities.size() > limits.MaxEntities || entities.size() > limits.MaxCollectionItems)
            return Reject(diagnostics, "Scene compatibility requires a bounded entity sequence.");
        for (const auto& entity : entities.Sequence())
        {
            const auto components = entity["components"];
            if (!components.IsMap() || components.size() > limits.MaxComponentsPerEntity)
                return Reject(diagnostics, "Scene compatibility requires a bounded component map.");
            for (const auto& component : components.Mapping())
            {
                const auto envelope          = component.Value;
                uint32_t   component_version = 0;
                if (!envelope.IsMap() || envelope.size() != 2 || !ReadSceneYAMLUInt(envelope["schema_version"], component_version) || !component_version || !envelope["fields"].IsMap())
                    return Reject(diagnostics, "Scene compatibility found a malformed component envelope.");
                const auto* schema = schemas.FindByKey(component.Key.data());
                if (!schema || component_version >= schema->Version)
                    continue; // unknown/newer data remains intact, never dropped
                if (!migrations)
                    return RejectStep(diagnostics, schema->Key, component_version, schema->Version, "no migration registry configured");
                auto upgraded = storage.Destination();
                if (!migrations->Apply(schema->Key, component_version, schema->Version, envelope["fields"], &upgraded, diagnostics, limits))
                    return false;
                if (!storage.Set(envelope, "fields", upgraded) || !storage.Set(envelope, "schema_version", storage.UInt(schema->Version)))
                    return Reject(diagnostics, "Migrated component exceeds the decoded-memory budget.");
            }
        }
        nodes = 0;
        return SceneDocumentDetail::CountCapturedNodes(*root, limits, nodes) || Reject(diagnostics, "Migrated scene exceeds document node, depth or scalar limits.");
    }
    bool SceneCompatibilityService::PrepareForSave(const SceneDocument& document, SceneCompatibilityUse use, SceneDiagnostics* diagnostics)
    {
        if (use != SceneCompatibilityUse::EditorSource && use != SceneCompatibilityUse::CookedRuntime)
            return Reject(diagnostics, "Unknown scene compatibility use.");
        if (!document.Settings() || document.ReadOnly())
            return Reject(diagnostics, use == SceneCompatibilityUse::CookedRuntime ? "Cooked output rejects unknown or unsupported scene component data; migrate source before cooking." : "Read-only/empty scene cannot be saved; unsupported data would be lost.");
        return true;
    }
} // namespace ZEngine::ECS
