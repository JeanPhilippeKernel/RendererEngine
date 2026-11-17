#pragma once
#include <ZEngine/Cook/CookManifest.h>
#include <ZEngine/Core/VFS/VFSRootLease.h>
#include <ZEngine/ECS/SceneComponentSchemaRegistry.h>
#include <ZEngine/ECS/SceneDocument.h>
#include <ZEngine/Managers/CookedObjectSnapshot.h>

namespace ZEngine::Managers
{
    /// One selected scene's immutable parsed manifest and retained read root.
    /// The supplied allocator and frozen schema registry outlive this owner and
    /// its borrowers. Object bytes are captured and verified separately: a root
    /// lease alone does not make files immutable. Use on one load worker unless
    /// the caller provides synchronization for the rooted backend and allocator.
    class CookedPackageSnapshot final
    {
    public:
        using AssetVisitor      = Core::VFS::VFSResult<void> (*)(void*, const Cook::CookManifestAsset&);
        CookedPackageSnapshot() = default;
        ~CookedPackageSnapshot();
        CookedPackageSnapshot(const CookedPackageSnapshot&)            = delete;
        CookedPackageSnapshot& operator=(const CookedPackageSnapshot&) = delete;
        CookedPackageSnapshot(CookedPackageSnapshot&& other) noexcept;
        CookedPackageSnapshot&                          operator=(CookedPackageSnapshot&& other) noexcept;

        /// Reads a bounded private copy of manifest.json, validates its target,
        /// schemas and complete typed ledger, then retains the selected closure.
        /// Rebuild is rejected. Failure preserves root and output; success consumes
        /// root. Limits and allocator are runtime admission policy, not a cooker run.
        [[nodiscard]] static Core::VFS::VFSResult<void> Capture(Core::VFS::VFSReadRootLease&& root, const uuids::uuid& scene, const ECS::SceneComponentSchemaRegistry& schemas, const Cook::CookLimits& limits, Cook::ICookAllocator& allocator, Cook::CookDiagnostics* diagnostics, CookedPackageSnapshot* output, const std::atomic<bool>* cancelled = nullptr);

        void                                            Reset() noexcept;
        [[nodiscard]] bool                              IsValid() const noexcept
        {
            return m_root.IsValid() && m_manifest.IsValid();
        }
        [[nodiscard]] const Cook::Digest256& ManifestDigest() const noexcept
        {
            return m_digest;
        }
        [[nodiscard]] const ECS::SceneComponentSchemaRegistry* Schemas() const noexcept
        {
            return IsValid() ? m_schemas : nullptr;
        }
        [[nodiscard]] const Cook::CookManifestScene*                          Scene() const noexcept;
        [[nodiscard]] const Cook::CookManifestAsset*                          FindSelectedAsset(const Cook::CookAssetIdentity& identity) const noexcept;
        /// Visits each selected version once in canonical manifest order. Views
        /// are borrowed until reset; visitor errors and cancellation stop the walk.
        [[nodiscard]] Core::VFS::VFSResult<void>                              VisitSelectedAssets(AssetVisitor visitor, void* context, const std::atomic<bool>* cancelled = nullptr) const;
        [[nodiscard]] Core::Containers::ArrayView<const Cook::CookDependency> Dependencies(const Cook::CookAssetIdentity& identity) const noexcept;
        /// Compare the decoded scene's complete typed root set and UUID with the
        /// retained manifest before any of its references are used for rendering.
        [[nodiscard]] Core::VFS::VFSResult<void>                              ValidateScene(const ECS::SceneDocument& document, const ECS::SceneComponentSchemaRegistry& schemas, ECS::SceneDiagnostics* diagnostics, const std::atomic<bool>* cancelled = nullptr) const;
        [[nodiscard]] Core::VFS::VFSResult<void>                              CaptureScene(Core::Memory::IAllocationRoute& route, size_t max_bytes, CookedObjectSnapshot* output, const std::atomic<bool>* cancelled = nullptr);
        [[nodiscard]] Core::VFS::VFSResult<void>                              CaptureAsset(const Cook::CookAssetIdentity& identity, Core::Memory::IAllocationRoute& route, size_t max_bytes, CookedObjectSnapshot* output, const std::atomic<bool>* cancelled = nullptr);

    private:
        Core::VFS::VFSReadRootLease              m_root      = {};
        Cook::CookManifest                       m_manifest  = {};
        Cook::ICookAllocator*                    m_allocator = nullptr;
        const ECS::SceneComponentSchemaRegistry* m_schemas   = nullptr;
        uint8_t*                                 m_selected  = nullptr;
        uuids::uuid                              m_scene     = {};
        Cook::Digest256                          m_digest    = {};
    };
} // namespace ZEngine::Managers
