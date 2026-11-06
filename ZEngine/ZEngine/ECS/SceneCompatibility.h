#pragma once
#include <ZEngine/ECS/SceneDocument.h>
#include <ZEngine/ECS/SceneYAML.h>

namespace ZEngine::ECS
{
    // Source is an immutable, owned value tree, not YAML text or a live ECS.
    // Callbacks must be deterministic, have no external effects, and write a
    // complete value into out->Owner(). Borrowed output/input mutation fails.
    using SceneMigrationFn = bool (*)(void* context, const SceneYAML::Node& source, SceneYAML::Node* out, SceneDiagnostics* diagnostics);

    class SceneMigrationRegistry
    {
    public:
        static constexpr uint32_t                         MaxMigrations = 256;
        static constexpr uint32_t                         MaxSteps      = 32;

        [[nodiscard]] bool                                Initialize(Core::Memory::ArenaAllocator* arena, uint32_t capacity = 32);
        [[nodiscard]] bool                                IsInitialized() const;
        [[nodiscard]] bool                                RegisterDocumentMigration(uint32_t from, uint32_t to, void* context, SceneMigrationFn migrate, SceneDiagnostics* diagnostics);
        [[nodiscard]] bool                                RegisterComponentMigration(cstring key, uint32_t from, uint32_t to, void* context, SceneMigrationFn migrate, SceneDiagnostics* diagnostics);
        [[nodiscard]] uint32_t                            Count() const;
        // Borrowed read-only roots only. No callback execution or allocation.
        void                                              ForEachContext(void (*visitor)(void*, const void*), void* state) const noexcept;
        [[nodiscard]] const Core::Memory::ArenaAllocator* StorageArena() const noexcept
        {
            return m_entries.m_allocator;
        }

        // Register at startup; readers require the registry and callback contexts
        // to remain immutable/alive until all source-load workers have retired.
    private:
        friend class SceneCompatibilityService;
        struct Entry
        {
            char             Key[128] = {}; // empty means document migration
            uint32_t         From     = 0;
            uint32_t         To       = 0;
            void*            Context  = nullptr;
            SceneMigrationFn Migrate  = nullptr;
        };
        Core::Containers::Array<Entry> m_entries  = {};
        uint32_t                       m_capacity = 0;
        [[nodiscard]] bool             Register(cstring key, uint32_t from, uint32_t to, void* context, SceneMigrationFn migrate, SceneDiagnostics* diagnostics);
        [[nodiscard]] const Entry*     Find(cstring key, uint32_t from) const;
        [[nodiscard]] bool             Apply(cstring key, uint32_t from, uint32_t to, const SceneYAML::Node& source, SceneYAML::Node* out, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits, Core::Memory::IAllocationRoute* allocation_route) const;
    };

    enum class SceneCompatibilityUse : uint8_t
    {
        EditorSource  = 0,
        CookedRuntime = 1
    };

    class SceneCompatibilityService
    {
    public:
        // Only for unpublished source trees; discard the tree on failure. All
        // resulting current payloads still pass normal codec/graph validation.
        // Known older schemas require a complete registered route. Unknown or
        // newer component schemas remain intact for read-only inspection.
        // Optional route supplies migration backing too. Only two inline tree
        // owners/backing blocks survive a step, reused without per-step backing
        // allocation. All temporary backing is released before return.
        [[nodiscard]] static bool PrepareForLoad(SceneYAML::Node* root, const SceneComponentSchemaRegistry& schemas, const SceneMigrationRegistry* migrations, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits = {}, Core::Memory::IAllocationRoute* allocation_route = nullptr);
        // One fixed policy: unknown source data cannot be saved; shipping output
        // also rejects it. No serializer may silently omit an opaque component.
        [[nodiscard]] static bool PrepareForSave(const SceneDocument& document, SceneCompatibilityUse use, SceneDiagnostics* diagnostics);
        // Cooked wire versions remain strict/current-only. Upgrade authoring
        // source and recook; these source migrations never guess old binary layouts.
    };
} // namespace ZEngine::ECS
