#pragma once
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <ZEngine/ECS/SceneComponentSchemaRegistry.h>
#include <ZEngine/ECS/SceneGridSettings.h>
#include <ZEngine/Helpers/IntrusivePtr.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>
#include <memory>
#include <string>

namespace ZEngine::ECS
{
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

    struct SceneDocumentSettings
    {
        uuids::uuid                  UUID = {};
        std::string                  Name = {};
        SceneGridSettings            Grid = {};
        Rendering::Scenes::SkyConfig Sky  = {};
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
        SceneDocument(const SceneDocument&)                                        = delete;
        SceneDocument&                             operator=(const SceneDocument&) = delete;

        [[nodiscard]] static bool                  Capture(const Scene& scene, const SceneDocumentSettings& settings, const SceneComponentSchemaRegistry& schemas, SceneDocument* out, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits = {});
        [[nodiscard]] static bool                  Parse(cstring source, size_t size, const SceneComponentSchemaRegistry& schemas, SceneDocument* out, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits = {});
        [[nodiscard]] static bool                  Read(Core::VFS::IVFSContext& vfs, const Core::VFS::VFSPath& path, const SceneComponentSchemaRegistry& schemas, SceneDocument* out, SceneDiagnostics* diagnostics, const SceneDocumentLimits& limits = {});

        [[nodiscard]] const SceneDocumentSettings* Settings() const;
        [[nodiscard]] uint32_t                     EntityCount() const;
        [[nodiscard]] bool                         ReadOnly() const;
        /// Visits immutable semantic references for one supported runtime component
        /// kind without creating an ECS world. Visitor false stops/fails the walk.
        /// No payload/registry pointers are retained; scratch is fixed-size.
        [[nodiscard]] bool                         EnumerateComponentReferences(ComponentTypeID type, const SceneComponentSchemaRegistry& schemas, SceneReferenceVisitorFn visitor, void* context, SceneDiagnostics* diagnostics) const;
        [[nodiscard]] bool                         Emit(std::string* out, SceneDiagnostics* diagnostics) const;
        [[nodiscard]] Core::VFS::VFSResult<void>   Save(Core::VFS::IVFSContext& vfs, const Core::VFS::VFSPath& path, SceneDiagnostics* diagnostics) const;

        /// Only accepts an empty, initialized, unpublished candidate. The scene owner must discard
        /// it on failure and rebuild render/actor bindings before publishing it to any consumer.
        [[nodiscard]] bool                         PopulateCandidate(Scene& candidate, const SceneComponentSchemaRegistry& schemas, SceneDiagnostics* diagnostics) const;

    private:
        struct Impl;
        ZEngine::Helpers::Scope<Impl> m_impl;
    };
} // namespace ZEngine::ECS
