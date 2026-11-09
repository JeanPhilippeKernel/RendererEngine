#pragma once
#include <ZEngine/Core/Memory/AllocationRoute.h>
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <ZEngine/ECS/SceneComponentSchemaRegistry.h>
#include <ZEngine/ECS/SceneGridSettings.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>
#include <memory>
#include <string>
#include <string_view>

namespace ZEngine::ECS
{
    class SceneMigrationRegistry;
    struct SceneDocumentLimits
    {
        size_t   MaxFileBytes           = 32 * 1024 * 1024;
        uint32_t MaxEntities            = 65536;
        uint32_t MaxComponentsPerEntity = 64;
        uint32_t MaxNodes               = 1048576;
        uint32_t MaxDepth               = 32;
        uint32_t MaxCollectionItems     = 65536;
        uint32_t MaxScalarBytes         = 4096;
        size_t   MaxDecodedBytes        = ZMega(128);
    };

    struct SceneDocumentReadMemory
    {
        // Exclusive temporary input storage. Read rewinds only its own allocation
        // after parsing, including on failure. Never retains source-buffer views.
        Core::Memory::ArenaAllocator*   SourceArena     = nullptr;
        // Optional document/parser route with the same lifetime rules as Parse.
        // It must not allocate from SourceArena: retained document blocks must
        // survive the input rewind. Without SourceArena, this also owns the input.
        Core::Memory::IAllocationRoute* AllocationRoute = nullptr;
    };

    /// Call-borrowed input, or a view into an immutable SceneDocument. Document
    /// names are NUL-terminated tree-owned text, never a second heap string.
    struct SceneDocumentSettingsView
    {
        uuids::uuid                  UUID = {};
        std::string_view             Name = {};
        SceneGridSettings            Grid = {};
        Rendering::Scenes::SkyConfig Sky  = {};
    };

    /// Convenience owner for editor/capture inputs. Cooking uses the borrowed
    /// view and does not construct this heap-string-containing input type.
    struct SceneDocumentSettings
    {
        uuids::uuid                  UUID = {};
        std::string                  Name = {};
        SceneGridSettings            Grid = {};
        Rendering::Scenes::SkyConfig Sky  = {};

                                     operator SceneDocumentSettingsView() const noexcept
        {
            return {.UUID = UUID, .Name = Name, .Grid = Grid, .Sky = Sky};
        }
    };

    /// Immutable, owned authoring snapshot. No ECS pointers, runtime handles or registry pointers.
    /// Capture/load are explicit (not per-frame) operations; workers may emit/save a captured snapshot.
    class SceneDocument
    {
    public:
        static constexpr uint32_t SchemaVersion = 2;
        static constexpr size_t   MaxNameBytes  = 127;

        SceneDocument();
        ~SceneDocument();
        SceneDocument(SceneDocument&&) noexcept;
        SceneDocument& operator=(SceneDocument&&) noexcept;
        SceneDocument(const SceneDocument&)                                            = delete;
        SceneDocument&                                 operator=(const SceneDocument&) = delete;

        [[nodiscard]] static bool                      Capture(const Scene& scene, const SceneDocumentSettingsView& settings, const SceneComponentSchemaRegistry& schemas, SceneDocument* out, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits = {});
        /// Pure registered source upgrades precede current codec/graph validation.
        /// Unknown/newer component schemas remain inspect-only; old known schemas
        /// need a complete migration route. Failure preserves out.
        /// With a route, the document/header and initial tree share one small
        /// checked block. Stable tree growth, libyaml and migration scratch use
        /// that route too; MaxDecodedBytes is a cap, not an up-front allocation. It must
        /// outlive the returned document (including moves). No fallback heap/OS
        /// allocation is used by this path; callback-owned resources are caller policy.
        [[nodiscard]] static bool                      Parse(cstring source, size_t size, const SceneComponentSchemaRegistry& schemas, SceneDocument* out, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits = {}, const SceneMigrationRegistry* migrations = nullptr, Core::Memory::IAllocationRoute* allocation_route = nullptr);
        [[nodiscard]] static bool                      Read(Core::VFS::IVFSContext& vfs, const Core::VFS::VFSPath& path, const SceneComponentSchemaRegistry& schemas, SceneDocument* out, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits = {}, const SceneMigrationRegistry* migrations = nullptr, const SceneDocumentReadMemory& memory = {});

        /// All views, including Name, expire when the document is destroyed or replaced.
        [[nodiscard]] const SceneDocumentSettingsView* Settings() const;
        [[nodiscard]] uint32_t                         EntityCount() const;
        [[nodiscard]] bool                             ReadOnly() const;
        /// Visits immutable semantic references for one supported runtime component
        /// kind without creating an ECS world. Visitor false stops/fails the walk.
        /// No payload/registry pointers are retained; scratch is fixed-size.
        [[nodiscard]] bool                             EnumerateComponentReferences(ComponentTypeID type, const SceneComponentSchemaRegistry& schemas, SceneReferenceVisitorFn visitor, void* context, SceneDiagnostics* diagnostics) const;
        /// Visits every supported component reference and scene-owned settings,
        /// including retained inactive sky environments. Requires a writable-format
        /// document (no unknown/newer schemas). Occurrences are call-borrowed;
        /// fixed scratch is reused and false immediately terminates the walk.
        [[nodiscard]] bool                             EnumerateReferences(const SceneComponentSchemaRegistry& schemas, SceneReferenceOccurrenceVisitorFn visitor, void* context, SceneDiagnostics* diagnostics) const;
        [[nodiscard]] bool                             Emit(std::string* out, SceneDiagnostics* diagnostics) const;
        [[nodiscard]] Core::VFS::VFSResult<void>       Save(Core::VFS::IVFSContext& vfs, const Core::VFS::VFSPath& path, SceneDiagnostics* diagnostics) const;

        /// Only accepts an empty, initialized, unpublished candidate. The scene owner must discard
        /// it on failure and rebuild render/actor bindings before publishing it to any consumer.
        /// Entity resolver IDs are included in the candidate arena budget and retained
        /// until that arena is reset; reconstruction creates no separate scratch arena.
        [[nodiscard]] bool                             PopulateCandidate(Scene& candidate, const SceneComponentSchemaRegistry& schemas, SceneDiagnostics* diagnostics) const;

    private:
        friend class BinarySceneCodec;
        struct Impl;
        struct ImplDeleter
        {
            Core::Memory::IAllocationRoute* Route = nullptr;
            void                            operator()(Impl* pointer) const noexcept;
        };
        std::unique_ptr<Impl, ImplDeleter> m_impl = {nullptr, ImplDeleter{}};
        [[nodiscard]] bool                 Initialize(const SceneDocumentLimits& limits, Core::Memory::IAllocationRoute* allocation_route = nullptr);
    };
} // namespace ZEngine::ECS
