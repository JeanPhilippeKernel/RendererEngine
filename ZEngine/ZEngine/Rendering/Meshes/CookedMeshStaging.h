#pragma once
#include <ZEngine/Managers/CookedCpuClosureCache.h>
#include <ZEngine/Rendering/RenderResourceManager.h>

namespace ZEngine::Rendering::Meshes
{
    /// One immutable cooked mesh and its unpublished host staging allocation.
    /// The closure lease retains the exact decoded geometry and metadata. This
    /// object is exclusive; synchronize handoffs between its worker and render
    /// owner. The cache, resource manager and device outlive it.
    class CookedMeshStaging final
    {
    public:
        enum class Phase : uint8_t
        {
            Empty,
            SourceReady,
            Staging,
            Staged
        };

        CookedMeshStaging()                                     = default;
        ~CookedMeshStaging()                                    = default;
        CookedMeshStaging(const CookedMeshStaging&)             = delete;
        CookedMeshStaging&  operator=(const CookedMeshStaging&) = delete;

        /// Consumes the ready closure lease only after validating its identity
        /// and decoded layout. No source asset or registry is consulted.
        [[nodiscard]] bool  Capture(Managers::CookedCpuClosureCache::Lease&& lease, const Cook::CookAssetIdentity& identity);
        /// Render-owner allocation. Failure leaves the captured source intact.
        [[nodiscard]] bool  Prepare(RenderResourceManager& manager, VkResult* status = nullptr);
        /// Copies at most one staging slice; vertices precede indices. Failure
        /// preserves the cursor. No geometry upload is submitted by this call.
        [[nodiscard]] bool  StageOne(VkResult* status = nullptr);
        void                Reset() noexcept;

        [[nodiscard]] Phase State() const noexcept
        {
            return m_phase;
        }
        [[nodiscard]] VkDeviceSize StagedBytes() const noexcept
        {
            return m_staging.Written();
        }
        [[nodiscard]] size_t VertexBytes() const noexcept
        {
            return m_vertex_bytes;
        }
        [[nodiscard]] size_t IndexBytes() const noexcept
        {
            return m_index_bytes;
        }
        [[nodiscard]] const Cook::CookAssetIdentity& Identity() const noexcept
        {
            return m_identity;
        }
        [[nodiscard]] const Cook::Digest256& Digest() const noexcept
        {
            return m_digest;
        }
        /// Borrowed only while this staging owner remains alive and unchanged.
        [[nodiscard]] const Importers::AssetMesh* Mesh() const noexcept
        {
            return m_mesh;
        }

    private:
        friend class ZEngine::Rendering::RenderResourceManager;
        friend struct ::RRMUploadBatchTestHelper;
        Managers::CookedCpuClosureCache::Lease m_lease        = {};
        const Importers::AssetMesh*            m_mesh         = nullptr;
        RenderResourceManager::UploadStaging   m_staging      = {};
        Cook::CookAssetIdentity                m_identity     = {};
        Cook::Digest256                        m_digest       = {};
        size_t                                 m_vertex_bytes = 0;
        size_t                                 m_index_bytes  = 0;
        Phase                                  m_phase        = Phase::Empty;
    };
} // namespace ZEngine::Rendering::Meshes
