#pragma once
#include <ZEngine/Rendering/Meshes/CookedMeshStaging.h>

namespace ZEngine::Rendering::Meshes
{
    /// Exclusive scene mesh upload transaction. Initialize on a worker, then
    /// transfer to the render owner. Each Step performs one bounded staging
    /// slice, admission or completion poll. Cache, route, manager and metadata
    /// slab outlive this owner and all resulting scene pins.
    class CookedSceneMeshBatch final
    {
    public:
        enum class State : uint8_t
        {
            Empty,
            Pending,
            Draining,
            Ready,
            Failed,
            Cancelled
        };
        CookedSceneMeshBatch() = default;
        ~CookedSceneMeshBatch();
        CookedSceneMeshBatch(const CookedSceneMeshBatch&)            = delete;
        CookedSceneMeshBatch& operator=(const CookedSceneMeshBatch&) = delete;

        /// Copies sorted unique mesh identities using the caller's route. A
        /// refusal preserves the lease and leaves the batch empty.
        [[nodiscard]] bool    Initialize(Managers::CookedCpuClosureCache::Lease&&, std::span<const uuids::uuid>, Core::Memory::IAllocationRoute&, RenderResourceManager&, Core::Memory::TLSFSlab&, RenderResourceManager::SceneResourcePreparationId);
        [[nodiscard]] State   Step(uint8_t frame_index);
        /// Cancellation can be observed before accepted GPU work is retired.
        /// Keep calling Step until Cancelled/Failed before resetting the owner.
        void                  Cancel() noexcept;
        /// Joined-owner shutdown only, after device idle or terminal loss and
        /// after preventing all further submissions.
        [[nodiscard]] bool    AbandonAfterDeviceIdle() noexcept;
        [[nodiscard]] bool    Reset() noexcept;
        [[nodiscard]] State   Status() const noexcept
        {
            return m_state;
        }
        /// Render-owner observation; the recorded upload still owns its staging.
        [[nodiscard]] bool HasRecordedUpload() const noexcept
        {
            return m_upload.Serial != 0;
        }

    private:
        void                                              Fail() noexcept;
        Managers::CookedCpuClosureCache::Lease            m_lease       = {};
        Core::Memory::IAllocationRoute*                   m_route       = nullptr;
        RenderResourceManager*                            m_manager     = nullptr;
        Core::Memory::TLSFSlab*                           m_metadata    = nullptr;
        RenderResourceManager::SceneResourcePreparationId m_scene       = {};
        uuids::uuid*                                      m_meshes      = nullptr;
        uint32_t                                          m_count       = 0;
        uint32_t                                          m_cursor      = 0;
        CookedMeshStaging                                 m_staging     = {};
        RenderResourceManager::CookedMeshUploadId         m_upload      = {};
        State                                             m_state       = State::Empty;
        State                                             m_after_drain = State::Cancelled;
    };
} // namespace ZEngine::Rendering::Meshes
